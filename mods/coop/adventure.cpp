#include "adventure.h"
#include "adventure_sync.h"
#include <algorithm>
#include <windows.h>
#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"

namespace adventure {
namespace {
using namespace adventure_sync;
const SkHost* H;
SkMod* M;
net::Session* session;
using Credit = void(SK_THISCALL*)(void*, void*, void*, void*);
Credit original;
void* current_play;
std::array<uint32_t,3> mission{};
uint32_t nonce, host_run, applied;
uint32_t serial = GetTickCount() ^ (GetCurrentProcessId() << 16);
std::array<uint32_t,net::kMaxPlayers> visitors{};
std::vector<Goal> local;
std::vector<Packet> incoming, journal;
bool scheduled, replaying;
double hello_at;
int observed_act=-1, observed_completed=-1;

bool context(uint8_t*& play, std::array<uint32_t,3>& key)
{
    play = modes::adventure_play();
    if (!play || !game::avatar()) return false;
    auto address = H->sym("App::cScenarioMode::ptr");
    if (!address || !skg::readable((void*)address,4)) return false;
    auto mode = *(uint8_t**)address;
    if (!mode || !skg::readable(mode,0x7C)) return false;
    auto data = *(uint8_t**)(mode+0x74);
    if (!data || !skg::readable(data,0x14)) return false;
    auto resource = *(uint8_t**)(data+0x10);
    if (!resource || !skg::readable(resource,0x14)) return false;
    memcpy(key.data(),resource+8,12);
    return key[0] || key[1] || key[2];
}

uint8_t* goal_at(uint8_t* play, uint32_t slot)
{
    uintptr_t first=*(uintptr_t*)(play+0x64), end=*(uintptr_t*)(play+0x68);
    if (!first || end<first || (end-first)%0x1AC || (end-first)/0x1AC>8 ||
        slot >= (end-first)/0x1AC || !skg::readable((void*)first,end-first)) return nullptr;
    return (uint8_t*)(first+slot*0x1AC);
}

Goal describe(uint8_t* play, uint8_t* goal, uint32_t slot)
{
    return {*(uint32_t*)(play+0xB8),slot,*(uint32_t*)(goal+0x24),
            *(uint32_t*)(goal+0x28),*(uint32_t*)(goal+0x2C),*(uint32_t*)(goal+0x30)};
}

bool live(uint8_t*& play)
{
    std::array<uint32_t,3> key;
    return context(play,key) && play==current_play && key==mission && nonce;
}

Packet packet(Kind kind, uint32_t visitor)
{
    Packet p; p.kind=kind; p.mission=mission; p.run=host_run; p.visitor=visitor;
    return p;
}

void send(uint8_t to, const Packet& p)
{
    net::Writer w; p.write(w); session->send_reliable_to(to,net::MSG_ADVENTURE,w);
}

// Keep values only: an act transition can rebuild both goal and actor lists.
void SK_THISCALL credit(void* self, void* raw_goal, void* spatial, void* second)
{
    auto play=(uint8_t*)self;
    if (!replaying && session->connected() && self==current_play && nonce &&
        *(int*)(play+0x90)==3) {
        for (uint32_t slot=0; slot<8; ++slot) {
            auto goal=goal_at(play,slot);
            if (!goal) break;
            if (goal!=raw_goal) continue;
            Goal description=describe(play,goal,slot);
            if (!goal[0] && *(int*)(goal+4)==0 && description.supported()) {
                if (std::find(local.begin(),local.end(),description)==local.end() && local.size()<8)
                    local.push_back(description);
                return; // Host commits before any captain advances this goal.
            }
        }
    }
    original(self,raw_goal,spatial,second);
}

// 0: wait for native transition, 1: applied/already present, -1: incompatible.
int apply(const Goal& description)
{
    uint8_t* play;
    if (!live(play)) return -1;
    uint32_t act=*(uint32_t*)(play+0xB8);
    if (description.act<act) return 1;
    if (description.act>act || *(int*)(play+0x90)!=3) return 0;
    auto goal=goal_at(play,description.slot);
    if (!goal || describe(play,goal,description.slot)!=description) return -1;
    if (goal[0]) return 1;
    if (*(int*)(goal+4)!=0) return -1;
    replaying=true;
    original(play,goal,nullptr,nullptr);
    replaying=false;
    // Credit may replace the vector. Reacquire before inspecting it.
    if (!live(play)) return -1;
    if (*(uint32_t*)(play+0xB8)>description.act) return 1;
    goal=goal_at(play,description.slot);
    return goal && describe(play,goal,description.slot)==description && goal[0] ? 1 : -1;
}

void commit(const Goal& goal)
{
    if (std::any_of(journal.begin(),journal.end(),[&](const Packet& p){return p.goal==goal;}) ||
        journal.size()>=kMaxCredits) return;
    uint8_t* play;
    if (!live(play) || *(uint32_t*)(play+0xB8)!=goal.act) return;
    auto native=goal_at(play,goal.slot);
    if (!native || describe(play,native,goal.slot)!=goal || native[0]) return;
    if (apply(goal)!=1) return;
    Packet p=packet(Commit,nonce); p.goal=goal; p.sequence=uint32_t(journal.size()+1);
    journal.push_back(p);
    H->log(M,SK_INFO,"adventure: committed %u act=%u slot=%u type=%u",p.sequence,goal.act,goal.slot,goal.kind);
    for (auto player:session->players()) {
        if (!player.id || player.id>=visitors.size() || !visitors[player.id]) continue;
        p.visitor=visitors[player.id]; send(player.id,p);
    }
}

void work(void*)
{
    scheduled=false;
    if (!session->connected()) { reset(); return; }
    uint8_t* play;
    std::array<uint32_t,3> key;
    if (!context(play,key)) { reset(); return; }
    int act=*(int*)(play+0xB8), completed=*(int*)(play+0x28);
    if (play!=current_play || mission!=key || !nonce ||
        act<observed_act || completed<observed_completed) {
        reset(); current_play=play; mission=key;
        if (++serial==0) ++serial;
        nonce=serial;
        if (session->role()==net::Role::Host) host_run=nonce;
    }
    observed_act=act; observed_completed=completed;
    bool host=session->role()==net::Role::Host;
    if (!host && H->time_seconds()>=hello_at) {
        auto p=packet(Hello,nonce); p.sequence=applied;
        send(0,p); hello_at=H->time_seconds()+1;
    }
    if (host) {
        auto pending=std::move(local); local.clear();
        for (const auto& goal:pending) commit(goal);
    } else if (host_run) {
        for (const auto& goal:local) {
            auto p=packet(Propose,nonce); p.goal=goal; send(0,p);
        }
        local.clear();
        std::sort(incoming.begin(),incoming.end(),[](const Packet& a,const Packet& b){return a.sequence<b.sequence;});
        while (!incoming.empty()) {
            auto p=incoming.front();
            if (p.sequence<=applied) { incoming.erase(incoming.begin()); continue; }
            if (p.sequence!=applied+1) break;
            int result=apply(p.goal);
            if (result!=1) break;
            applied=p.sequence; incoming.erase(incoming.begin());
        }
    }
}
}

void init(const SkHost* host, SkMod* mod, net::Session* s)
{
    H=host; M=mod; session=s;
    auto address=H->sym("SporeCoop::ScenarioCreditGoal");
    if (!address || H->hook_create(M,(void*)address,(void*)credit,(void**)&original)!=0)
        H->log(M,SK_ERROR,"adventure: shared objective hook unavailable");
}

void reset()
{
    current_play=nullptr; mission={}; nonce=host_run=applied=0; visitors={};
    local.clear(); incoming.clear(); journal.clear(); hello_at=0;
    observed_act=observed_completed=-1;
}

net::AdventureGate outgoing() { return {}; } // Legacy tail remains wire-compatible.

void tick()
{
    if (!original || scheduled) return;
    scheduled=true;
    H->run_outside_frame(work,nullptr);
}

void message(uint8_t from, net::Reader& r)
{
    Packet p;
    if (!original || !session->connected() || !p.read(r) || p.mission!=mission || !nonce ||
        from>=net::kMaxPlayers || from==session->my_id()) return;
    bool host=session->role()==net::Role::Host;
    if (host && p.kind==Hello) {
        visitors[from]=p.visitor;
        auto reply=packet(Welcome,p.visitor); send(from,reply);
        uint32_t acknowledged=p.run==host_run ? p.sequence : 0;
        for (auto credit:journal) if (credit.sequence>acknowledged) {
            credit.visitor=p.visitor; send(from,credit);
        }
    } else if (host && p.kind==Propose && p.run==host_run && visitors[from]==p.visitor) {
        if (local.size()<8 && std::find(local.begin(),local.end(),p.goal)==local.end()) local.push_back(p.goal);
    } else if (!host && !from && p.visitor==nonce) {
        if (p.kind==Welcome) {
            if (host_run && host_run!=p.run) { applied=0; incoming.clear(); }
            host_run=p.run;
        } else if (p.kind==Commit && p.run==host_run && p.sequence>applied && incoming.size()<kMaxCredits &&
            std::none_of(incoming.begin(),incoming.end(),[&](const Packet& q){return q.sequence==p.sequence;})) {
            incoming.push_back(p);
        }
    }
}
}
