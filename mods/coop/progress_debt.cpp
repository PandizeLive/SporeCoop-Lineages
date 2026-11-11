#include "progress_debt.h"

#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "cellsync.h"
#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "sui.h"

namespace progress_debt {
namespace {
// `coop.fix core.progress_debt off`: the old loss (Creature: 10% of the DNA taken away; Cell: none)
bool g_fix = true;
COOP_FIX("core.progress_debt", "BUG-9119", g_fix,
         "creature / cell: a downed player who was not revived leaves a progress debt (gains repay it first) instead of losing DNA");

const SkHost* H;
net::Session* S;
Ledger g_ledger;           // host: the party's debt
Ledger g_heard;            // guest: the host's last word
double g_last_sent = -1e9;
float g_sent = -1;
uint32_t g_mode = 0, g_world = 0;
void (*g_notice)(const std::string&, int);
bool g_repaid = false; // host: the last gain cleared the debt (a notice on the next tick)

// the bar: the HUD's progress strip (control 0x051B5478, its fill window 0x0652A628) in screen
// pixels, found outside the frame (UI reads); the fill inside it is the stage's progress 0..1
// (Creature: cPlayer +0x10F0 / +0x10F4, Cell: food S+0x1C / 1000), docs/re/creature.md#creature-progress-bar
// The Creature bar is not one straight line: each brain level has a stretch of the same width and
// its own DNA, so the fill's end is read from the game's own marker (0x051B5168, a zero-width window
// at the fill's end, the green arrow) and the debt is drawn back from it at the bar's average rate.
constexpr uint32_t kBarId = 0x051B5478, kFillId = 0x0652A628, kMarkerId = 0x051B5168;
struct Bar {
    bool shown = false;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float edge = -1; // the marker's x (-1: not found)
};
Bar g_bar;
double g_bar_asked = -1e9;
bool g_bar_busy = false;
uint32_t g_draws = 0;

bool here(uint32_t mode) { return mode == modes::kCreature || mode == modes::kCell; }
bool is_host() { return S && S->connected() && S->role() == net::Role::Host; }
bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }

const Ledger& current() { return is_host() ? g_ledger : g_heard; }

void send(double now)
{
    net::Writer w;
    w.u32(g_ledger.mode);
    w.u32(g_ledger.world);
    w.f32(g_ledger.owed);
    S->broadcast_reliable(net::MSG_PROGRESS_DEBT, w);
    g_sent = g_ledger.owed;
    g_last_sent = now;
}

// the stage's progress now: done / total (false outside Creature / Cell)
bool progress(uint32_t mode, float& done, float& total)
{
    if (mode == modes::kCreature) {
        done = game::evolution_bar();
        total = game::goal_total();
        return std::isfinite(done) && std::isfinite(total) && total > 0;
    }
    if (mode == modes::kCell) {
        int food = 0;
        if (!cellsync::food_total(food))
            return false;
        done = (float)food;
        total = 1000.f;
        return true;
    }
    return false;
}

// outside the frame: where the bar is now (the HUD can be hidden: editors, cinematics, menus)
void find_bar(void*)
{
    Bar b;
    std::vector<sui::Win> stack{sui::main_window()};
    sui::Win bar = nullptr;
    while (!stack.empty() && !bar) { // only through visible parents (hidden copies are skipped)
        sui::Win w = stack.back();
        stack.pop_back();
        if (!sui::visible(w))
            continue;
        if (sui::control_id(w) == kBarId)
            bar = w;
        else
            for (sui::Win c : sui::children(w))
                stack.push_back(c);
    }
    sui::Win fill = bar ? sui::find(bar, kFillId) : nullptr;
    if (fill) {
        sui::Rect r = sui::screen_rect(fill);
        // the green fill inside the window, measured on screenshots at 1440x1080 (both stages:
        // Creature 237..1354 in 236..1357, Cell 231..1345 in 231..1349)
        b.x1 = r.x1 + (g_mode == modes::kCreature ? 1.f : 0.f);
        b.x2 = r.x2 - (g_mode == modes::kCreature ? 3.f : 4.f);
        b.y1 = std::max(r.y1, r.y1 + (g_mode == modes::kCreature ? 1.f : 0.f));
        b.y2 = r.y2 - (g_mode == modes::kCreature ? 2.f : 0.f);
        b.shown = b.x2 - b.x1 > 50 && b.y2 - b.y1 > 4;
        if (sui::Win marker = sui::find(bar, kMarkerId)) {
            sui::Rect m = sui::screen_rect(marker);
            if (m.x1 >= b.x1 - 2 && m.x1 <= b.x2 + 4)
                b.edge = std::clamp(m.x1, b.x1, b.x2);
        }
    }
    g_bar = b;
    g_bar_busy = false;
}
} // namespace

void init(const SkHost* host, net::Session* session, void (*notify)(const std::string&, int))
{
    H = host;
    S = session;
    g_notice = notify;
}

void reset()
{
    g_ledger = {};
    g_heard = {};
    g_sent = -1;
    g_bar = {};
}

bool enabled() { return g_fix; }

void tick(uint32_t mode, uint32_t world, double now)
{
    // only a stage moves the ledger: an editor, a load screen or a menu between two moments of the
    // same stage and world keeps the debt (another save or another stage clears it)
    if (here(mode) || mode == modes::kTribe || mode == modes::kCiv || mode == modes::kSpace || mode == modes::kScenario) {
        g_mode = mode;
        g_world = world;
    }
    if (!S || !S->connected()) {
        if (g_ledger.owed > 0 || g_heard.owed > 0)
            reset();
        return;
    }
    if (is_host()) {
        if (g_ledger.place(g_mode, g_world) && g_sent > 0)
            send(now); // the old debt is gone
        if (g_ledger.owed != g_sent && now - g_last_sent > 0.25)
            send(now);
        else if (g_ledger.owed > 0 && now - g_last_sent > 2)
            send(now); // a late joiner sees it too
        if (g_repaid) {
            g_repaid = false;
            if (g_notice && g_ledger.owed <= 0)
                g_notice(g_mode == modes::kCell ? "Progress debt repaid: food counts again." : "Progress debt repaid: DNA counts again.", -1);
        }
    }
    if (g_fix && here(g_mode) && current().owed > 0 && !g_bar_busy && now - g_bar_asked > 0.25) {
        g_bar_busy = true;
        g_bar_asked = now;
        H->run_outside_frame(find_bar, nullptr);
    } else if (current().owed <= 0)
        g_bar.shown = false;
}

float add(float amount)
{
    if (!g_fix || !is_host() || !here(g_mode) || !(amount > 0))
        return 0;
    g_ledger.place(g_mode, g_world);
    float done = 0, total = 0;
    if (progress(g_mode, done, total)) // never more than the bar holds: the red stays on the bar
        amount = std::min(amount, std::max(0.f, done - g_ledger.owed));
    amount = std::floor(amount + 0.5f);
    if (amount <= 0)
        return 0;
    g_ledger.add(amount);
    return amount;
}

float absorb(float gain)
{
    if (!g_fix || !is_host() || gain <= 0 || g_ledger.owed <= 0 || g_ledger.mode != g_mode || !here(g_mode))
        return gain;
    float left = g_ledger.absorb(gain);
    if (g_ledger.owed <= 0)
        g_repaid = true;
    return left;
}

int absorb_food(int gain)
{
    float left = absorb((float)gain);
    return (int)std::lround(left);
}

bool owing(uint32_t mode)
{
    const Ledger& l = current();
    return g_fix && l.owed > 0 && l.mode == mode && mode == g_mode;
}

float owed()
{
    const Ledger& l = current();
    return g_fix && l.mode == g_mode && here(g_mode) ? l.owed : 0.f;
}

void message(uint8_t from, net::Reader& r)
{
    uint32_t mode = r.u32(), world = r.u32();
    float debt = r.f32();
    if (!r.ok() || from != 0 || !is_guest() || !std::isfinite(debt) || debt < 0 || debt > 100000)
        return;
    g_heard.mode = mode;
    g_heard.world = world;
    g_heard.owed = debt;
}

void draw()
{
    if (!g_fix || !g_bar.shown || !here(g_mode))
        return;
    const float debt = owed();
    float done = 0, total = 0;
    if (debt <= 0 || !progress(g_mode, done, total))
        return;
    auto* device = static_cast<IDirect3DDevice9*>(H->d3d_device());
    if (!device)
        return;
    const float w = g_bar.x2 - g_bar.x1;
    const float end = g_bar.edge >= 0 ? g_bar.edge : g_bar.x1 + w * std::clamp(done / total, 0.f, 1.f);
    if (end - g_bar.x1 < 2.f || done <= 0)
        return;
    const float rate = (end - g_bar.x1) / done; // pixels per point, the bar's average up to here
    // never thinner than 4 px (a few DNA on a 1000-point bar), never past the bar's start
    const float start = std::max(g_bar.x1, end - std::max(4.f, debt * rate));
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)))
        return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    struct Vertex { float x, y, z, rhw; DWORD color; };
    // a deep, slightly desaturated red, shaded like the bar (lighter in the middle, darker at the rims)
    const DWORD rim = 0xFF4E1A1D, mid = 0xFF8A2E33, top = 0xFF6E2428;
    const float y0 = g_bar.y1, y3 = g_bar.y2, h = y3 - y0;
    const float y1 = y0 + h * 0.30f, y2 = y0 + h * 0.62f;
    auto band = [&](float ya, float yb, DWORD ca, DWORD cb) {
        Vertex q[] = {{start, ya, 0, 1, ca}, {end, ya, 0, 1, ca}, {start, yb, 0, 1, cb}, {end, yb, 0, 1, cb}};
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vertex));
    };
    band(y0, y1, top, mid);
    band(y1, y2, mid, mid);
    band(y2, y3, mid, rim);
    // a thin darker line where the debt starts: the green that refills it grows from there
    Vertex edge[] = {{start, y0, 0, 1, 0xFF2A0E10}, {start + 1.f, y0, 0, 1, 0xFF2A0E10},
                     {start, y3, 0, 1, 0xFF2A0E10}, {start + 1.f, y3, 0, 1, 0xFF2A0E10}};
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, edge, sizeof(Vertex));
    saved->Apply();
    saved->Release();
    g_draws++;
}

std::string debug()
{
    char b[320];
    const Ledger& l = current();
    float done = 0, total = 0;
    bool p = progress(g_mode, done, total);
    std::snprintf(b, sizeof b,
                  "progress debt: %s owed=%.1f (mode %08X world %08X; stage now %08X world %08X) progress=%s%.1f/%.1f "
                  "bar=%d [%.0f %.0f %.0f %.0f] edge=%.0f draws=%u fix=%d\n",
                  is_host() ? "host" : is_guest() ? "guest" : "offline", l.owed, l.mode, l.world, g_mode, g_world,
                  p ? "" : "(none) ", done, total, g_bar.shown ? 1 : 0, g_bar.x1, g_bar.y1, g_bar.x2, g_bar.y2, g_bar.edge, g_draws,
                  g_fix ? 1 : 0);
    return b;
}

int command(int argc, const char* const* argv, std::string& out)
{
    if (argc > 2 && !std::strcmp(argv[1], "add")) { // tests: a debt as a timeout makes it
        float got = add((float)std::atof(argv[2]));
        out = "added " + std::to_string((int)got) + "\n" + debug();
        return got > 0 ? 0 : 1;
    }
    if (argc > 1 && !std::strcmp(argv[1], "clear")) {
        if (is_host())
            g_ledger.owed = 0;
        out = debug();
        return 0;
    }
    out = debug();
    return 0;
}

} // namespace progress_debt
