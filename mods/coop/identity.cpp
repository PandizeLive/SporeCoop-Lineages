#include "identity.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <random>

#include "creations.h"
#include "net/protocol.h"

namespace identity {

static const SkHost* H;
static SkMod* M;
static net::Session* S;
static uint64_t g_own;
static std::map<uint8_t, uint64_t> g_of; // host: player id -> identity
static uint32_t g_sent_conn;              // guest: the join (my id + 1) the identity went out for
static uint32_t g_rx, g_tx;

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
}

static uint64_t make_id()
{
    std::random_device rd;
    uint64_t v = (uint64_t(rd()) << 32) ^ rd();
    v ^= (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count() * 0x9E3779B97F4A7C15ull;
    v ^= (uint64_t)GetCurrentProcessId() << 17;
    return v ? v : 0x5C0DE5C0DEull;
}

uint64_t own()
{
    if (g_own)
        return g_own;
    std::wstring dir = creations::coop_dir();
    if (dir.empty())
        return 0;
    std::wstring path = dir + L"\\identity.bin";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        uint8_t b[12] = {};
        DWORD got = 0;
        bool ok = ReadFile(f, b, sizeof b, &got, nullptr) && got == sizeof b && !memcmp(b, "SCID", 4);
        CloseHandle(f);
        if (ok)
            memcpy(&g_own, b + 4, 8);
        if (g_own)
            return g_own;
    }
    const uint64_t id = make_id();
    uint8_t b[12];
    memcpy(b, "SCID", 4);
    memcpy(b + 4, &id, 8);
    std::wstring temp = path + L".tmp";
    f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD wrote = 0;
    bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, b, sizeof b, &wrote, nullptr) && wrote == sizeof b;
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    // a second game of the same profile may have made one first: its file wins (MoveFileEx without replace)
    if (ok && !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        g_own = 0;
        return own();
    }
    if (!ok)
        return 0;
    g_own = id;
    H->log(M, SK_INFO, "identity: this profile's player identity was made (%08X...)", (unsigned)(id >> 32));
    return g_own;
}

uint64_t of(uint8_t player)
{
    if (S && player == S->my_id())
        return own();
    auto it = g_of.find(player);
    return it == g_of.end() ? 0 : it->second;
}

uint8_t player_of(uint64_t id)
{
    if (!id || !S)
        return 0xFF;
    for (const auto& p : S->players())
        if (of(p.id) == id)
            return p.id;
    return 0xFF;
}

void tick(double)
{
    if (!S || S->role() != net::Role::Client || !S->connected()) {
        g_sent_conn = 0; // every join sends it again (a rejoin may keep the same player id)
        return;
    }
    const uint32_t conn = S->my_id() + 1u;
    if (g_sent_conn == conn)
        return;
    const uint64_t id = own();
    if (!id)
        return;
    net::Writer w;
    w.u64(id);
    if (S->send_reliable_to(0, net::MSG_IDENTITY, w)) {
        g_sent_conn = conn;
        g_tx++;
    }
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint64_t id = r.u64();
    if (!r.ok() || !id || !S || S->role() != net::Role::Host || from == 0)
        return;
    for (auto it = g_of.begin(); it != g_of.end();) // one game per identity: a rejoin under a new id
        it = it->second == id && it->first != from ? g_of.erase(it) : std::next(it);
    g_of[from] = id;
    g_rx++;
    H->log(M, SK_INFO, "identity: player %u is %08X...", from, (unsigned)(id >> 32));
}

void on_player_left(uint8_t player) { g_of.erase(player); }

void on_session_end()
{
    g_of.clear();
    g_sent_conn = 0;
}

std::string status()
{
    char b[160];
    snprintf(b, sizeof b, "identity: own=%08X%08X known=%zu sent=%u received=%u", (unsigned)(own() >> 32), (unsigned)own(),
             g_of.size(), g_tx, g_rx);
    std::string out = b;
    for (auto& [p, id] : g_of) {
        snprintf(b, sizeof b, " p%u=%08X", p, (unsigned)(id >> 32));
        out += b;
    }
    return out + "\n";
}

} // namespace identity
