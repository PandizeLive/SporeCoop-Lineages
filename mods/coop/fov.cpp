#include "fov.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

#include "game.h"
#include "modes.h"
#include "mouse.h"
#include "sporekit_game.hpp"

namespace fov {

static const SkHost* H;
static SkMod* M;
static bool (*g_input_allowed)();
static void (*g_notify)(const std::string&);
static uint32_t g_mode;
static uintptr_t g_mode_manager, g_camera_manager, g_active_camera, g_perspective, g_view_window;
static bool (__thiscall* original_begin)(void*);
static void* (__thiscall* original_end)(void*);
static bool (__thiscall* original_camera_to_point)(void*, float, float, void*, void*);
static int g_steps, g_wheel_delta;
static bool g_eligible;
static float g_vanilla, g_effective;

struct Projection {
    void* viewer = nullptr;
    void* camera = nullptr;
    float base_x = 0, base_y = 0;
};
static Projection g_projection;

static void* active_manager()
{
    void* manager = skg::cdecl_call<void*>(g_mode_manager);
    return manager ? skg::thiscall<void*>(g_camera_manager, manager) : nullptr;
}

static void* viewer_of(void* manager)
{
    // App::cCameraManager::Update passes the viewer at +0xB0 to the active camera.
    return manager ? skg::field<void*>(manager, 0xB0) : nullptr;
}

static void* perspective_camera(void* viewer)
{
    void* camera = viewer ? skg::field<void*>(viewer, 0x170) : nullptr;
    return camera && skg::field<int>(camera, 0x6C) == 1 ? camera : nullptr;
}

static void restore(void* viewer)
{
    if (viewer && viewer == g_projection.viewer) {
        if (perspective_camera(viewer) == g_projection.camera) {
            skg::thiscall<void>(g_view_window, viewer, g_projection.base_x, g_projection.base_y);
        }
        g_projection = {};
    }
}

static bool third_person(void* manager)
{
    const auto mode = game::mode_id();
    const auto id = skg::thiscall<uint32_t>(g_active_camera, manager);
    // Creature and stock Adventure play both use this follow camera. The
    // Adventure briefing uses a separate camera and must ignore Ctrl+Scroll.
    constexpr uint32_t follow_camera = 0xB03AF999u;
    return (mode == modes::kCreature || mode == modes::kScenario) && id == follow_camera;
}

static void set_steps(int steps, bool announce)
{
    steps = std::clamp(steps, 0, 6);
    if (steps == g_steps) return;
    g_steps = steps;
    g_wheel_delta = 0;
    H->log(M, SK_INFO, "fov: steps=%d (+%d degrees)", g_steps, 3 * g_steps);
    if (!announce || !g_notify) return;
    g_notify(g_steps ? "Field of view +" + std::to_string(3 * g_steps) + " degrees (Ctrl+Scroll up to restore)"
                     : std::string("Field of view: normal"));
}

// BUG-9099: the wide lens lasted the whole session and nothing showed it. It now ends
// when the player leaves Creature or Adventure play, except for an editor visit.
static void track_mode()
{
    const auto mode = game::mode_id();
    if (mode == g_mode) return;
    g_mode = mode;
    const bool keep = mode == modes::kCreature || mode == modes::kScenario ||
                      mode == modes::kEditor || mode == modes::kEdit;
    if (!keep && g_steps) {
        H->log(M, SK_INFO, "fov: mode %08X resets the field of view", mode);
        set_steps(0, false);
    }
}

static bool __fastcall begin_view(void* viewer, void*)
{
    track_mode();
    void* manager = active_manager();
    // A draw whose EndUpdate never reached us must not leave the wide lens behind.
    if (g_projection.viewer && viewer == g_projection.viewer) restore(viewer);
    if (!manager || viewer != viewer_of(manager) || g_projection.viewer)
        return original_begin(viewer);
    // Scope the projection to drawing. Native zoom, camera switches and
    // respawn updates run with their original view window between draws.
    void* camera = perspective_camera(viewer);
    g_eligible = camera && third_person(manager);
    g_vanilla = g_effective = 0;
    if (!g_eligible) {
        g_wheel_delta = 0;
        return original_begin(viewer);
    }
    const float x = skg::field<float>(camera, 0x5C);
    const float y = skg::field<float>(camera, 0x60);
    if (!(x > 0) || !(y > 0) || !std::isfinite(x) || !std::isfinite(y)) {
        g_eligible = false;
        return original_begin(viewer);
    }
    constexpr float half_rad_to_deg = 114.591559f;
    g_vanilla = g_effective = std::atan(y) * half_rad_to_deg;
    if (!g_steps) return original_begin(viewer);
    // Preserve projection aspect and clipping planes through the native setter.
    g_effective = std::min(150.f, g_vanilla + 3.f * g_steps);
    if (g_effective <= g_vanilla) {
        g_effective = g_vanilla;
        return original_begin(viewer);
    }
    g_projection = {viewer, camera, x, y};
    skg::thiscall<void>(g_perspective, viewer, g_effective);
    const bool begun = original_begin(viewer);
    if (!begun) restore(viewer);
    return begun;
}

static void* __fastcall end_view(void* viewer, void*)
{
    void* result = original_end(viewer);
    restore(viewer);
    return result;
}

static bool __fastcall camera_to_point(void* viewer, void*, float x, float y, void* origin, void* direction)
{
    // The scene uses a wider lens only while drawing. Match that lens when
    // Spore turns a cursor position into a ray between draws.
    if (g_steps && !g_projection.viewer) {
        void* manager = active_manager();
        void* camera = perspective_camera(viewer);
        if (camera && viewer == viewer_of(manager) && third_person(manager)) {
            constexpr float deg_to_half_rad = 0.00872664626f;
            const float base_y = skg::field<float>(camera, 0x60);
            if (base_y > 0 && std::isfinite(base_y)) {
                const float wide = std::min(150.f, std::atan(base_y) / deg_to_half_rad + 3.f * g_steps);
                const float scale = std::max(1.f, std::tan(wide * deg_to_half_rad) / base_y);
                const float cx = skg::field<uint16_t>(camera, 0x78) + skg::field<uint16_t>(camera, 0x80) * 0.5f;
                const float cy = skg::field<uint16_t>(camera, 0x7C) + skg::field<uint16_t>(camera, 0x84) * 0.5f;
                x = cx + (x - cx) * scale;
                y = cy + (y - cy) * scale;
            }
        }
    }
    return original_camera_to_point(viewer, x, y, origin, direction);
}

static bool fov_wheel(int delta)
{
    track_mode();
    void* manager = active_manager();
    if (!g_eligible || !manager || !third_person(manager) || !g_input_allowed()) return false;
    g_wheel_delta -= delta;
    const int steps = g_steps + g_wheel_delta / WHEEL_DELTA;
    const int rest = g_wheel_delta % WHEEL_DELTA;
    set_steps(steps, true);
    g_wheel_delta = rest;
    return true;
}

bool init(const SkHost* host, SkMod* mod, bool (*input_allowed)(), void (*notify)(const std::string&))
{
    H = host;
    M = mod;
    g_input_allowed = input_allowed;
    g_notify = notify;
    g_mode_manager = H->sym("App::IGameModeManager::Get");
    g_camera_manager = H->sym("App::cGameModeManager::GetCameraManager");
    g_active_camera = H->sym("App::cCameraManager::GetActiveCameraID");
    g_perspective = H->sym("App::cViewer::SetPerspectiveProjection");
    g_view_window = H->sym("App::cViewer::SetViewWindow");
    const auto begin = H->sym("App::cViewer::BeginUpdate");
    const auto end = H->sym("App::cViewer::EndUpdate");
    const auto point = H->sym("App::cViewer::GetCameraToPoint");
    if (!g_mode_manager || !g_camera_manager || !g_active_camera || !g_perspective || !g_view_window || !begin || !end || !point)
        return false;
    if (H->hook_create(mod, reinterpret_cast<void*>(point), reinterpret_cast<void*>(camera_to_point),
                       reinterpret_cast<void**>(&original_camera_to_point)) != 0 ||
        H->hook_create(mod, reinterpret_cast<void*>(end), reinterpret_cast<void*>(end_view),
                       reinterpret_cast<void**>(&original_end)) != 0 ||
        H->hook_create(mod, reinterpret_cast<void*>(begin), reinterpret_cast<void*>(begin_view),
                       reinterpret_cast<void**>(&original_begin)) != 0)
        return false;
    mouse::set_fov_wheel(fov_wheel);
    H->command_add(mod, "coop.fov", "coop.fov [reset|0-6] - show or set the local third-person FOV steps (3 degrees each)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
        if (argc > 1) {
            char* end = nullptr;
            const long steps = std::strcmp(argv[1], "reset") ? std::strtol(argv[1], &end, 10) : 0;
            if (end && (*end || steps < 0 || steps > 6)) {
                H->out_printf(out, "usage: coop.fov [reset|0-6]");
                return 1;
            }
            set_steps((int)steps, true);
        }
        void* manager = active_manager();
        const auto id = manager ? skg::thiscall<uint32_t>(g_active_camera, manager) : 0u;
        H->out_printf(out, "camera=%08X eligible=%d allowed=%d steps=%d vanilla=%.2f effective=%.2f",
                      id, g_eligible, g_input_allowed(), g_steps, g_vanilla, g_effective);
        return 0;
    }, nullptr, SK_CMD_MAIN_THREAD);
    return true;
}

void shutdown()
{
    mouse::set_fov_wheel(nullptr);
    if (g_projection.viewer) restore(viewer_of(active_manager()));
}

} // namespace fov
