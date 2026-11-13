#include "evolve.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "creations.h"
#include "fixes.h"
#include "world.h"
#include "sporekit_game.hpp"
#include "game.h"
#include "modes.h"

namespace evolve {
namespace {
const SkHost* H;
SkMod* M;
bool g_cocoon = true;
COOP_FIX("creature.cocoon", "BUG-9160", g_cocoon,
         "creature: a partner in their own editor stays in the game's cocoon where they stood and comes out with the new body");
bool g_personal = true;
COOP_FIX("creature.personal_editor", "BUG-9160", g_personal,
         "creature: the host's mating call opens the host's own editor (no evolution vote); everyone evolves on their own");
constexpr double kEnterSeconds = 2.4; // gen_evolve_cocoon, then the hold (runtime 2026-11-12)
constexpr double kHoldReplay = 6.0;   // the hold is played again this often (it does not loop by itself)
constexpr double kBodyWait = 15.0;    // back in the world with a new design: until the puppet wears it, this long at most
constexpr double kNoDesignWait = 5.0; // back in the world and no new design came (left without saving): this long
constexpr double kSacAgain = 0.5;     // the glow is made again this soon after it faded
constexpr double kShellEvery = 1.8;   // the woven shell is laid again this often
bool g_egg_watch = true;
COOP_FIX("creature.egg_watch", "BUG-9161", g_egg_watch,
         "creature: an egg scene after a mating that does not end by itself is finished as Escape does (the editor opens)");
constexpr uint32_t kLaidEgg = 0xB6A1DBE1u; // CRG_LaidEgg
constexpr double kEggWatch = 12.0;
double g_egg_since = -1;
void* g_own_wrapped; // the own creature held in the cocoon pose until the editor opens
bool g_egg_pressed;
uint32_t g_egg_finished;
bool g_family = true;
COOP_FIX("creature.family_species", "BUG-9161", g_family,
         "creature: the own nest family and herd wear the own creature's species (as in the game), so mating and the egg scene work");
uint32_t g_family_herds, g_family_members;
double g_family_at;
uint32_t g_wraps, g_emerges, g_own_emerges;
uint32_t g_prev_mode;
bool g_own_due;
double g_own_due_at;
} // namespace

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
}

bool cocoon_on() { return g_cocoon; }
bool personal_editor() { return g_personal; }

// one of the game's cocoon effects where the creature stands (world effects: the creature does not move meanwhile),
// its up (local z) along the planet's up there (the position from the planet's centre): with the world's z instead
// the sac lay on its side (seen 2026-11-12). `rows`: the 3x3 holds the local axes as rows (else as columns).
float g_fx_scale = 1.f, g_fx_up = 0.f;
bool g_fx_rows = true;
static void* effect_at_pos(const net::Vec3& at, uint32_t id, float scale, float up_by, bool rows)
{
    float len = sqrtf(at.x * at.x + at.y * at.y + at.z * at.z);
    if (!(len > 1.f)) len = 1.f;
    const float u[3] = {at.x / len, at.y / len, at.z / len};
    float r[3] = {u[1], -u[0], 0.f}; // up x z, then normalised (any axis not along up)
    float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (rl < 1e-3f) { r[0] = 1.f; r[1] = 0.f; r[2] = 0.f; rl = 1.f; }
    for (float& v : r) v /= rl;
    const float fw[3] = {u[1] * r[2] - u[2] * r[1], u[2] * r[0] - u[0] * r[2], u[0] * r[1] - u[1] * r[0]};
    uint8_t xf[0x44] = {};
    float* f = (float*)(xf + 4);
    f[0] = at.x + u[0] * up_by; f[1] = at.y + u[1] * up_by; f[2] = at.z + u[2] * up_by;
    f[3] = scale;
    float* m = f + 4;
    const float* axes[3] = {r, fw, u};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[rows ? i * 3 + j : j * 3 + i] = axes[i][j];
    return game::create_world_effect(id, xf);
}
// sized to the creature (2026-11-13: at scale 1 a creature of bounding radius 1.24 fitted in the shell, one of 1.40
// with a long neck stuck its head out): the creature's radius / kShellFits, never below 0.8
constexpr float kShellFits = 0.9f;
static void* effect_at(void* c, uint32_t id)
{
    const float r = game::bounding_radius(c);
    const float size = std::isfinite(r) && r > 0 ? std::clamp(r / kShellFits, 0.8f, 4.f) : 1.f;
    return effect_at_pos(game::position(c), id, g_fx_scale * size, g_fx_up, g_fx_rows);
}
static void end_sac(Cocoon& c)
{
    if (!c.fx) return;
    if (game::effect_active(c.fx)) game::stop_world_effect(c.fx);
    game::release_world_effect(c.fx);
    c.fx = nullptr;
}

void drop(Cocoon& c, bool keep_state)
{
    end_sac(c);
    if (!keep_state) c = {};
}

// the cocoon on puppet `puppet` (again: a new body is a new puppet): the pose held, inside, its sac around it
static void wrap(void* puppet, Cocoon& c, double now, bool fresh)
{
    game::hold_pose(puppet, true);
    game::play_animation(puppet, fresh ? kCocoonEnter : kCocoonHold, -1, true);
    end_sac(c);
    if (void* fx = effect_at(puppet, kFxCreate)) game::release_world_effect(fx); // the woven shell, ~2 s, ends by itself
    c.shell_at = now;
    if (!fresh) {
        c.fx = effect_at(puppet, kFxHold);
        c.fx_at = now;
    }
    c.puppet = puppet;
    c.played = now;
}
// the sac fades after a few seconds and the hold pose ends: both again (the owner, 2026-11-12: "the cocoons weren't
// visible to MSI, even after a minute")
static void keep(void* puppet, Cocoon& c, double now)
{
    if (c.puppet != puppet) { wrap(puppet, c, now, false); return; }
    // the woven shell lasts ~2 s (runtime 2026-11-13: full 0.2 s after it was made, gone at 2.0 s): the next one
    // before it fades, so the cocoon stays closed while the partner edits
    if (now - c.shell_at >= kShellEvery) {
        if (void* fx = effect_at(puppet, kFxCreate)) game::release_world_effect(fx);
        c.shell_at = now;
    }
    if ((!c.fx || !game::effect_active(c.fx)) && now - c.fx_at >= kSacAgain) {
        end_sac(c);
        c.fx = effect_at(puppet, kFxHold);
        c.fx_at = now;
    }
    uint32_t anim = 0;
    if (now - c.played >= kHoldReplay && (!game::drawn_state(puppet, nullptr, &anim) || anim != kCocoonHold)) {
        game::play_animation(puppet, kCocoonHold, -1, true);
        c.played = now;
    }
}

void puppet_editing(uint8_t player, void* puppet, Cocoon& c, double now)
{
    if (!g_cocoon || !puppet) return;
    if (c.state == 0 || c.state == 3) { // in (again: back to its editor before it came out)
        const bool fresh = c.state == 0;
        const uint32_t key = fresh ? creations::player_key(player, creations::kCreature).instance : c.key;
        wrap(puppet, c, now, fresh);
        c.state = fresh ? 1 : 2;
        c.at = now;
        c.key = key;
        if (fresh) ++g_wraps;
        return;
    }
    if (c.state == 1 && now - c.at >= kEnterSeconds) {
        c.puppet = nullptr; // the hold and its sac now
        keep(puppet, c, now);
        c.state = 2;
        c.at = now;
        return;
    }
    if (c.state == 2) keep(puppet, c, now);
}

void puppet_back(uint8_t player, void* puppet, Cocoon& c, double now)
{
    if (!c.state) return;
    if (!puppet || !g_cocoon) {
        if (puppet) game::hold_pose(puppet, false);
        drop(c);
        return;
    }
    if (c.state != 3) { // back: still in the cocoon until the new body is there
        c.state = 3;
        c.at = now;
    }
    const uint32_t key = creations::player_key(player, creations::kCreature).instance;
    const bool designed = key && key != c.key;
    const bool worn = designed && game::species_instance(puppet) == key;
    if (!worn && !(designed ? now - c.at >= kBodyWait : now - c.at >= kNoDesignWait)) {
        keep(puppet, c, now);
        return;
    }
    drop(c);
    game::hold_pose(puppet, false);
    game::play_animation(puppet, kEmerge, -1, true);
    if (void* fx = effect_at(puppet, kFxHatch)) game::release_world_effect(fx);
    ++g_emerges;
}

void own_tick(uint32_t mode, void* avatar, bool busy, double now)
{
    const bool editor = mode == modes::kEdit || mode == modes::kEditor;
    if (g_prev_mode && (g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor) && mode == modes::kCreature) {
        g_own_due = true;
        g_own_due_at = now;
    }
    if (mode) g_prev_mode = mode;
    if (editor || mode != modes::kCreature) return;
    if (!g_own_due) return;
    if (!g_cocoon || now - g_own_due_at > 60) { g_own_due = false; return; }
    if (busy || !avatar || now - g_own_due_at < 1.0) return; // after the game's own fade from the editor
    g_own_due = false;
    game::play_animation(avatar, kEmerge, -1, true);
    if (void* fx = effect_at(avatar, kFxHatch)) game::release_world_effect(fx);
    ++g_own_emerges;
}

void egg_tick(uint32_t mode, double now)
{
    if (mode != modes::kCreature || world::cinematic_id() != kLaidEgg) {
        g_egg_since = -1;
        g_egg_pressed = false;
        return;
    }
    if (g_egg_since < 0) {
        g_egg_since = now;
        // owner decision 2D: the own creature goes into the game's cocoon instead of the egg, then the editor opens
        if (g_egg_watch && g_cocoon)
            if (void* av = game::avatar()) {
                game::hold_pose(av, true);
                game::play_animation(av, kCocoonEnter, -1, true);
                if (void* fx = effect_at(av, kFxCreate)) game::release_world_effect(fx);
                g_own_wrapped = av;
            }
    }
    if (!g_egg_watch || g_egg_pressed || now - g_egg_since < (g_cocoon ? kEnterSeconds : kEggWatch)) return;
    g_egg_pressed = true;
    if (g_own_wrapped) { game::hold_pose(g_own_wrapped, false); g_own_wrapped = nullptr; }
    HWND w = (HWND)H->game_window();
    if (!w) return;
    const LPARAM lp = 1 | (MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC) << 16);
    PostMessageW(w, WM_KEYDOWN, VK_ESCAPE, lp);
    PostMessageW(w, WM_KEYUP, VK_ESCAPE, lp | 0xC0000000);
    ++g_egg_finished;
    H->log(M, SK_INFO, "evolve: the egg scene ends after %.1f s (the cocoon instead, BUG-9160 / BUG-9161): Escape for the player",
           now - g_egg_since);
}

void family_tick(uint32_t mode, void* avatar, bool busy, double now)
{
    if (!g_family || mode != modes::kCreature || !avatar || busy || now - g_family_at < 2.0) return;
    g_family_at = now;
    void* herd = game::herd_of(avatar);
    void* prof = game::species_profile(avatar);
    if (!herd || !prof || !game::herd_nest(herd)) return;
    static const uintptr_t set_species = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    if (!set_species) return;
    struct Ctx {
        void *herd, *prof, *avatar;
        uintptr_t set;
        uint32_t changed;
    } x{herd, prof, avatar, set_species, 0};
    // the herd's own species (+0xA4) stays: with it on the own creature's the game offered no mate any more (2026-11-13:
    // every member class 7 after the mating call; the herd's older species back: a member class 9 again)
    const bool herd_old = false;
    game::for_each_object([](void* creature, uint32_t noun, void* p) {
        auto& x = *static_cast<Ctx*>(p);
        if (noun != 0x18EB45Eu || creature == x.avatar || game::is_puppet(creature) || !game::object_alive(creature) ||
            game::is_dead(creature) || game::herd_of(creature) != x.herd || game::species_profile(creature) == x.prof)
            return true;
        const int age = *(int*)((uint8_t*)creature + 0xB34);
        skg::thiscall<bool>(x.set, creature, x.prof, age, true);
        ++x.changed;
        return true;
    }, &x);
    if (herd_old || x.changed) {
        g_family_herds += herd_old;
        g_family_members += x.changed;
        H->log(M, SK_INFO, "evolve: the own family follows the own creature's species %08X: herd %s, %u member(s) (BUG-9161)",
               game::species_key(avatar).instance, herd_old ? "changed" : "kept", x.changed);
    }
}

std::string debug()
{
    char b[260];
    snprintf(b, sizeof b,
             "evolve: cocoon=%d personal_editor=%d wraps=%u emerges=%u own_emerges=%u own_due=%d egg_watch=%d finished=%u fx scale %.2f up %.2f rows %d\n",
             (int)g_cocoon, (int)g_personal, g_wraps, g_emerges, g_own_emerges, (int)g_own_due, (int)g_egg_watch,
             g_egg_finished, g_fx_scale, g_fx_up, (int)g_fx_rows);
    std::string out = b;
    snprintf(b, sizeof b, "family: fix=%d herds=%u members=%u\n", (int)g_family, g_family_herds, g_family_members);
    out += b;
    return out;
}

static uint32_t fnv1(const char* s)
{
    uint32_t h = 0x811C9DC5u;
    for (; *s; ++s) h = (h * 0x01000193u) ^ (uint8_t)tolower((unsigned char)*s);
    return h;
}

int command(int argc, const char* const* argv, std::string& out)
{
    // tests: an effect by name (FNV-1 of the lower-case name) or 0x id at the own creature: world (`fx`) or on it (`cfx`)
    if (argc > 2 && (!strcmp(argv[1], "fx") || !strcmp(argv[1], "cfx")) && game::avatar()) {
        std::string res;
        for (int i = 2; i < argc; ++i) {
            const uint32_t id = !strncmp(argv[i], "0x", 2) ? (uint32_t)strtoul(argv[i], nullptr, 16) : fnv1(argv[i]);
            void* fx = nullptr;
            if (!strcmp(argv[1], "cfx")) fx = game::create_creature_effect(game::avatar(), -1, id);
            else fx = effect_at(game::avatar(), id);
            char b[96];
            snprintf(b, sizeof b, "%s %08X: %s\n", argv[i], id, fx ? "made" : "none");
            res += b;
        }
        out = res;
        return 0;
    }
    // tests: an animation on the own creature (`hold`: kept in its pose), `release`: the pose free again
    void* avatar = game::avatar();
    if (argc > 2 && !strcmp(argv[1], "calls")) { // RE: every `call rel32` / `jmp rel32` to a .sym address in the game's code
        const uint32_t target = (uint32_t)strtoul(argv[2], nullptr, 16);
        auto* base = (uint8_t*)GetModuleHandleA(nullptr);
        auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        const uintptr_t live = (uintptr_t)base + target - 0x400000;
        std::string res;
        int hits = 0;
        for (unsigned s = 0; s < nt->FileHeader.NumberOfSections && hits < 40; ++s, ++sec) {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const uint8_t* a = base + sec->VirtualAddress;
            const size_t n = sec->Misc.VirtualSize;
            for (size_t i = 0; i + 5 <= n && hits < 40; ++i) {
                if (a[i] != 0xE8 && a[i] != 0xE9) continue;
                int32_t rel;
                memcpy(&rel, a + i + 1, 4);
                if ((uintptr_t)(a + i + 5) + rel != live) continue;
                char b[40];
                snprintf(b, sizeof b, "%s %08X\n", a[i] == 0xE8 ? "call" : "jmp", (unsigned)(a + i - base + 0x400000));
                res += b;
                ++hits;
            }
        }
        out = res.empty() ? "none\n" : res;
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "find")) { // RE: a byte pattern (hex, ?? any byte) in the game's code, as .sym addresses
        std::vector<int> pat;
        for (const char* p = argv[2]; p[0] && p[1]; p += 2)
            pat.push_back(p[0] == '?' ? -1 : (int)strtoul(std::string(p, 2).c_str(), nullptr, 16));
        auto* base = (uint8_t*)GetModuleHandleA(nullptr);
        auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        std::string res;
        int hits = 0;
        for (unsigned s = 0; s < nt->FileHeader.NumberOfSections && hits < 40; ++s, ++sec) {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const uint8_t* a = base + sec->VirtualAddress;
            const size_t n = sec->Misc.VirtualSize;
            for (size_t i = 0; i + pat.size() <= n && hits < 40; ++i) {
                size_t k = 0;
                while (k < pat.size() && (pat[k] < 0 || a[i + k] == pat[k])) ++k;
                if (k != pat.size()) continue;
                char b[32];
                snprintf(b, sizeof b, "%08X\n", (unsigned)(a + i - base + 0x400000));
                res += b;
                ++hits;
            }
        }
        out = res.empty() ? "none\n" : res;
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "fxset")) { // tests: the cocoon effects' scale, lift (m) and axes (rows 1 / cols 0)
        g_fx_scale = (float)atof(argv[2]);
        g_fx_up = (float)atof(argv[3]);
        g_fx_rows = atoi(argv[4]) != 0;
        out = debug();
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "anim") && avatar) {
        const uint32_t id = (uint32_t)strtoul(argv[2], nullptr, 16);
        const bool hold = argc > 3 && !strcmp(argv[3], "hold");
        game::hold_pose(avatar, hold);
        game::play_animation(avatar, id, -1, true);
        char b[96];
        snprintf(b, sizeof b, "played %08X on the own creature%s\n", id, hold ? " (held)" : "");
        out = b + debug();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "release") && avatar) {
        game::hold_pose(avatar, false);
        out = "released\n" + debug();
        return 0;
    }
    out = debug();
    return 0;
}

} // namespace evolve
