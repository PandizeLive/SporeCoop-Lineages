#include "adventure.h"
#include "adventure_sync.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <windows.h>
#include "fixes.h"
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

// ---- BUG-9089 (44): combat adventures. Every objective type is shared through the host journal
// (a credit for a defeated object counts once, whichever game saw it die); a scenario object (a
// building or creature placed by the adventure) that dies in one game dies in every game; the host
// decides the mission's end (a guest's game never fails on its own; the host's success / failure
// reaches every guest). `coop.fix adventure.combat off`: move-to and talk only, as in 43.
bool g_fix_combat = true;
COOP_FIX("adventure.combat", "BUG-9089", g_fix_combat,
         "adventure: every objective is shared, an adventure object that dies in one game dies in all, the host decides the mission's end");
constexpr uint32_t kBuildingNoun = 0x70703B3u, kCreatureNoun = 0x18EB45Eu, kSpatialType = 0x1186577u;
constexpr uintptr_t kBuildingCombatant = 0x120, kCombatantHp = 0x38;
using SetStateFn = void(SK_THISCALL*)(void*, int);
SetStateFn o_SetState;
bool host_ending;                 // guest: applying the host's outcome now
int host_outcome;                 // guest: the host's ended state (5 / 6), 0 none
unsigned blocked_failures, mirrored_kills, deaths_out, deaths_in, credits_held, outcomes_out;
struct Known { uint32_t id; bool alive; float hp, top; };
std::map<std::pair<void*,uint32_t>,Known> known;      // (object, serial) -> its id, alive last poll
std::unordered_map<void*,uint32_t> spatial_ids;      // spatial subobject -> id (credits name it)
std::map<uint32_t,uint32_t> deaths_local, deaths_wanted, deaths_told;
std::set<std::tuple<uint32_t,uint32_t,uint32_t>> applied_objects; // (act, slot, object) applied here
std::set<std::pair<void*,uint32_t>> killed_here; // objects this game destroyed for another game's death (once each)
double poll_at;

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
bool same_goal(const Goal& a, const Goal& b)
{
    return a.act==b.act && a.slot==b.slot && a.kind==b.kind && a.target==b.target && a.secondary==b.secondary &&
           a.required==b.required;
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

void to_guests(Packet p)
{
    for (auto player:session->players()) {
        if (!player.id || player.id>=visitors.size() || !visitors[player.id]) continue;
        p.visitor=visitors[player.id]; send(player.id,p);
    }
}

// ---- scenario objects (BUG-9089) ----
// The id of an adventure object, the same in every game: its class (+0x1C) and its scenario marker
// (+0x18: type, spawn position and orientation, docs/re/space.md#space-duo-adventure-combat). 0 = not one.
uint32_t object_id(void* o)
{
    if (!o || !skg::readable(o,0x24)) return 0;
    auto marker=*(uint8_t**)((uint8_t*)o+0x18);
    if (!marker || !skg::readable(marker,0x24)) return 0;
    uint32_t h=0x811C9DC5u;
    auto mix=[&](const uint8_t* p,size_t n){ for (size_t i=0;i<n;++i){ h^=p[i]; h*=0x01000193u; } };
    mix(marker,0x24);
    mix((uint8_t*)o+0x1C,4);
    return h?h:1;
}
float object_hp(void* o, uint32_t noun)
{
    if (noun==kBuildingNoun) {
        auto c=(uint8_t*)o+kBuildingCombatant;
        return skg::readable(c,kCombatantHp+4) ? *(float*)(c+kCombatantHp) : 0.f;
    }
    return game::health(o);
}
void kill(void* o, uint32_t noun)
{
    float hp=object_hp(o,noun);
    if (!(hp>0)) return;
    if (noun==kBuildingNoun) {
        auto c=(uint8_t*)o+kBuildingCombatant;
        auto vt=skg::readable(c,4) ? *(uintptr_t**)c : nullptr;
        if (!vt || !skg::readable(vt,0x1C)) return;
        static const float dir[3]={0.f,0.f,1.f}; // TakeDamage needs a direction (CLAUDE.md 2)
        // cBuilding::TakeDamage (combatant vt+0x18): thiscall(combatant, dmg, attacker political id, type, dir*, attacker) ret 0x14
        skg::thiscall<int>((uintptr_t)vt[6],c,hp+100000.f,0xFFFFFFFFu,0,(const void*)dir,(void*)nullptr);
    } else {
        game::apply_damage(o,hp+100000.f,0,nullptr);
    }
    ++mirrored_kills;
}

void poll(double now)
{
    if (now<poll_at) return;
    poll_at=now+0.25;
    struct Q { std::map<std::pair<void*,uint32_t>,Known> seen; std::vector<std::pair<void*,uint32_t>> alive; void* av; } q;
    q.av=game::avatar();
    spatial_ids.clear();
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if ((noun!=kBuildingNoun && noun!=kCreatureNoun) || o==q->av || game::is_puppet(o)) return true;
        uint32_t id=object_id(o);
        if (!id) return true;
        float hp=object_hp(o,noun);
        bool alive=game::object_alive(o) && hp>0;
        q->seen[{o,game::object_serial(o)}]={id,alive,hp,hp};
        if (alive) q->alive.push_back({o,noun});
        if (void* sp=game::cast(o,kSpatialType)) spatial_ids[sp]=id;
        return true;
    },&q);
    // a death: alive before and now dead; or gone after it had been hurt (a destroyed building can leave the
    // list at once). An object that leaves unhurt was removed by the adventure (an act change, a load): no death.
    std::map<uint32_t,uint32_t> fresh;
    for (auto& [key,k]:known) {
        auto it=q.seen.find(key);
        if (it!=q.seen.end()) it->second.top=std::max(it->second.top,k.top);
        if (!k.alive) continue;
        if (it!=q.seen.end() ? !it->second.alive : (k.top>0 && k.hp<k.top*0.95f)) ++fresh[k.id];
    }
    known=std::move(q.seen);
    for (auto& [id,n]:fresh) deaths_local[id]+=n;
    bool host=session->role()==net::Role::Host;
    for (auto& [id,n]:deaths_local) {
        if (deaths_told[id]>=n) continue;
        deaths_told[id]=n;
        auto p=packet(Death,nonce); p.object=id; p.deaths=std::min<uint32_t>(n,64);
        if (host) to_guests(p); else send(0,p);
        ++deaths_out;
    }
    // what another game saw die: the same objects die here (any alive one of that id)
    for (auto& [id,want]:deaths_wanted) {
        uint32_t have=deaths_local[id];
        for (auto& [o,noun]:q.alive) {
            if (have>=want) break;
            if (object_id(o)!=id || !killed_here.insert({o,game::object_serial(o)}).second) continue;
            kill(o,noun);
            ++have; // counted as a death by the next poll
        }
    }
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
            description.count=uint32_t(std::max(0,*(int*)(goal+4)));
            if (g_fix_combat && spatial) {
                auto it=spatial_ids.find(spatial);
                if (it!=spatial_ids.end()) description.object=it->second;
            }
            // A defeat (kind 3) of an adventure object is credited natively, at once, in every game: the
            // object dies in every game (deaths are mirrored), so each game counts it once by itself.
            // Held back, the game saw the target gone and the count short and failed the mission
            // ("You can no longer complete this goal: Destroy Big Evil Keep (0 of 5 destroyed)").
            if (g_fix_combat && description.kind==3 && description.object) break;
            const bool old_rule=!g_fix_combat && (description.kind==1 || description.kind==2) && description.required==1;
            if (!goal[0] && description.supported() && (g_fix_combat || old_rule) &&
                (g_fix_combat || description.count==0)) {
                if (std::none_of(local.begin(),local.end(),[&](const Goal& g){return g.same_credit(description);}) &&
                    local.size()<32)
                    local.push_back(description);
                ++credits_held;
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
    if (!goal || !same_goal(describe(play,goal,description.slot),description)) return -1;
    if (goal[0]) return 1;
    int count=*(int*)(goal+4);
    if (description.object) {
        if (applied_objects.count({description.act,description.slot,description.object})) return 1;
    } else {
        if (count>int(description.count)) return 1;
        if (count<int(description.count)) return 0;
    }
    replaying=true;
    original(play,goal,nullptr,nullptr);
    replaying=false;
    if (description.object) applied_objects.insert({description.act,description.slot,description.object});
    // Credit may replace the vector. Reacquire before inspecting it.
    if (!live(play)) return -1;
    if (*(uint32_t*)(play+0xB8)>description.act) return 1;
    goal=goal_at(play,description.slot);
    return goal && same_goal(describe(play,goal,description.slot),description) &&
           (goal[0] || *(int*)(goal+4)>count) ? 1 : -1;
}

// true: done with this credit (committed, a duplicate or stale); false: keep it until this game can take it
bool commit(const Goal& goal)
{
    if (std::any_of(journal.begin(),journal.end(),[&](const Packet& p){return p.goal.same_credit(goal);}) ||
        journal.size()>=kMaxCredits) return true;
    uint8_t* play;
    if (!live(play)) return false;
    uint32_t act=*(uint32_t*)(play+0xB8);
    if (goal.act<act) return true;
    if (goal.act>act || *(int*)(play+0x90)!=3) return false; // BUG-9089: not playing yet (an intro): later
    auto native=goal_at(play,goal.slot);
    if (!native || !same_goal(describe(play,native,goal.slot),goal) || native[0]) return true;
    Goal entry=goal;
    if (!entry.object) entry.count=uint32_t(std::max(0,*(int*)(native+4))); // the host's own count orders it
    int result=apply(entry);
    if (result==0) return false;
    if (result!=1) return true;
    Packet p=packet(Commit,nonce); p.goal=entry; p.sequence=uint32_t(journal.size()+1);
    journal.push_back(p);
    H->log(M,SK_INFO,"adventure: committed %u act=%u slot=%u type=%u object=%08X count=%u",p.sequence,entry.act,
           entry.slot,entry.kind,entry.object,entry.count);
    to_guests(p);
    return true;
}

// BUG-9089: the mission's end is the host's. A guest's game does not fail on its own (its captain
// down, a timer of its own simulation); the host's end reaches every guest.
void SK_THISCALL set_state(void* play, int state)
{
    if (g_fix_combat && session && session->connected() && play && play==current_play && nonce &&
        (state==5 || state==6)) {
        if (session->role()==net::Role::Host) {
            o_SetState(play,state);
            auto p=packet(Outcome,nonce); p.state=uint32_t(state); to_guests(p);
            ++outcomes_out;
            H->log(M,SK_INFO,"adventure: the mission ended here (state %d): told the guests",state);
            return;
        }
        if (!host_ending && state==6) {
            ++blocked_failures;
            H->log(M,SK_INFO,"adventure: a failure of this game alone was held (the host decides the mission's end)");
            return;
        }
    }
    o_SetState(play,state);
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
    if (g_fix_combat && (host || host_run) && *(int*)(play+0x90)==3) poll(H->time_seconds()); // only while it plays
    if (!host && H->time_seconds()>=hello_at) {
        auto p=packet(Hello,nonce); p.sequence=applied;
        send(0,p); hello_at=H->time_seconds()+1;
    }
    if (host) {
        auto pending=std::move(local); local.clear();
        for (const auto& goal:pending)
            if (!commit(goal) && local.size()<32) local.push_back(goal); // BUG-9089: kept, not lost
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
        // the host's end, once the credits before it are in
        if (host_outcome && o_SetState && live(play) && *(int*)(play+0x90)==3 && incoming.empty()) {
            host_ending=true;
            o_SetState(play,host_outcome);
            host_ending=false;
            H->log(M,SK_INFO,"adventure: the host's mission ended (state %d): this game too",host_outcome);
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
    auto set=H->sym("Simulator::cScenarioPlayMode::SetState");
    if (!set || H->hook_create(M,(void*)set,(void*)set_state,(void**)&o_SetState)!=0)
        H->log(M,SK_ERROR,"adventure: mission end hook unavailable");
}

void reset()
{
    current_play=nullptr; mission={}; nonce=host_run=applied=0; visitors={};
    local.clear(); incoming.clear(); journal.clear(); hello_at=0;
    observed_act=observed_completed=-1;
    known.clear(); spatial_ids.clear(); killed_here.clear(); deaths_local.clear(); deaths_wanted.clear(); deaths_told.clear();
    applied_objects.clear(); host_outcome=0; host_ending=false; poll_at=0;
}

net::AdventureGate outgoing() { return {}; } // Legacy tail remains wire-compatible.

void tick()
{
    if (!original || scheduled) return;
    scheduled=true;
    H->run_outside_frame(work,nullptr);
}

// tests: destroy the nearest object of a scenario class in this game only (a player's kill)
std::string debug_kill(uint32_t cls)
{
    struct Q { uint32_t cls; void* best; uint32_t noun; float d; net::Vec3 me; } q{cls,nullptr,0,1e9f,{}};
    if (void* av=game::avatar()) q.me=game::position(av);
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if ((noun!=kBuildingNoun && noun!=kCreatureNoun) || !skg::readable(o,0x20) || *(uint32_t*)((uint8_t*)o+0x1C)!=q->cls ||
            !game::object_alive(o) || !(object_hp(o,noun)>0)) return true;
        net::Vec3 p;
        if (!game::object_position(o,p)) return true;
        float d=net::dist(p,q->me);
        if (d<q->d) { q->d=d; q->best=o; q->noun=noun; }
        return true;
    },&q);
    if (!q.best) return "no live object of that class";
    kill(q.best,q.noun);
    char b[96]; snprintf(b,sizeof b,"destroyed %p (id %08X) %.0f m away",q.best,object_id(q.best),q.d);
    return b;
}

std::string status()
{
    char b[300];
    snprintf(b,sizeof b,"adventure: run=%08X journal=%u applied=%u held=%u objects=%u deaths_out=%u deaths_in=%u "
             "mirrored_kills=%u failures_held=%u outcomes_out=%u host_outcome=%d",
             host_run,(unsigned)journal.size(),applied,credits_held,(unsigned)known.size(),deaths_out,deaths_in,
             mirrored_kills,blocked_failures,outcomes_out,host_outcome);
    return b;
}

void message(uint8_t from, net::Reader& r)
{
    Packet p;
    if (!original || !session->connected() || !p.read(r) || p.mission!=mission || !nonce ||
        from>=net::kMaxPlayers || from==session->my_id()) return;
    bool host=session->role()==net::Role::Host;
    if (host && p.kind==Hello) {
        const bool fresh=visitors[from]!=p.visitor; // a guest new to this run (Hello repeats every second)
        visitors[from]=p.visitor;
        auto reply=packet(Welcome,p.visitor); send(from,reply);
        uint32_t acknowledged=p.run==host_run ? p.sequence : 0;
        for (auto credit:journal) if (credit.sequence>acknowledged) {
            credit.visitor=p.visitor; send(from,credit);
        }
        if (fresh) for (auto& [id,n]:deaths_local) { // a guest that comes (back) gets every death so far
            auto d=packet(Death,p.visitor); d.object=id; d.deaths=std::min<uint32_t>(n,64); send(from,d);
        }
    } else if (host && p.kind==Propose && p.run==host_run && visitors[from]==p.visitor) {
        if (local.size()<32 && std::none_of(local.begin(),local.end(),[&](const Goal& g){return g.same_credit(p.goal);}))
            local.push_back(p.goal);
    } else if (host && p.kind==Death && p.run==host_run && visitors[from]==p.visitor && g_fix_combat) {
        auto& w=deaths_wanted[p.object]; w=std::max(w,p.deaths); ++deaths_in;
    } else if (!host && !from && p.visitor==nonce) {
        if (p.kind==Welcome) {
            if (host_run && host_run!=p.run) { applied=0; incoming.clear(); }
            host_run=p.run;
        } else if (p.kind==Commit && p.run==host_run && p.sequence>applied && incoming.size()<kMaxCredits &&
            std::none_of(incoming.begin(),incoming.end(),[&](const Packet& q){return q.sequence==p.sequence;})) {
            incoming.push_back(p);
        } else if (p.kind==Death && p.run==host_run && g_fix_combat) {
            auto& w=deaths_wanted[p.object]; w=std::max(w,p.deaths); ++deaths_in;
        } else if (p.kind==Outcome && p.run==host_run && g_fix_combat) {
            host_outcome=int(p.state);
        }
    }
}
}
