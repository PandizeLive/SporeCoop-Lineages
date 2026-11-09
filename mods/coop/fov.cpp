#include "fov.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <windows.h>

#include "game.h"
#include "modes.h"
#include "mouse.h"
#include "sporekit_game.hpp"

namespace fov {

static const SkHost* H;
static bool (*g_input_allowed)();
static uintptr_t g_mode_manager, g_camera_manager, g_active_camera, g_perspective, g_view_window;
static bool (__thiscall* original_begin)(void*);
static void* (__thiscall* original_end)(void*);
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
    // Adventure briefing uses a separate camera and must ignore Alt+Scroll.
    constexpr uint32_t follow_camera = 0xB03AF999u;
    return (mode == modes::kCreature || mode == modes::kScenario) && id == follow_camera;
}

static bool __fastcall begin_view(void* viewer, void*)
{
    void* manager = active_manager();
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
    if (!g_steps || !g_input_allowed()) return original_begin(viewer);
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

static bool alt_wheel(int delta)
{
    void* manager = active_manager();
    if (!g_eligible || !manager || !third_person(manager) || !g_input_allowed()) return false;
    g_wheel_delta -= delta;
    g_steps = std::clamp(g_steps + g_wheel_delta / WHEEL_DELTA, 0, 6);
    g_wheel_delta %= WHEEL_DELTA;
    return true;
}

bool init(const SkHost* host, SkMod* mod, bool (*input_allowed)())
{
    H = host;
    g_input_allowed = input_allowed;
    g_mode_manager = H->sym("App::IGameModeManager::Get");
    g_camera_manager = H->sym("App::cGameModeManager::GetCameraManager");
    g_active_camera = H->sym("App::cCameraManager::GetActiveCameraID");
    g_perspective = H->sym("App::cViewer::SetPerspectiveProjection");
    g_view_window = H->sym("App::cViewer::SetViewWindow");
    const auto begin = H->sym("App::cViewer::BeginUpdate");
    const auto end = H->sym("App::cViewer::EndUpdate");
    if (!g_mode_manager || !g_camera_manager || !g_active_camera || !g_perspective || !g_view_window || !begin || !end)
        return false;
    if (H->hook_create(mod, reinterpret_cast<void*>(end), reinterpret_cast<void*>(end_view),
                       reinterpret_cast<void**>(&original_end)) != 0 ||
        H->hook_create(mod, reinterpret_cast<void*>(begin), reinterpret_cast<void*>(begin_view),
                       reinterpret_cast<void**>(&original_begin)) != 0)
        return false;
    mouse::set_alt_wheel(alt_wheel);
    H->command_add(mod, "coop.fov", "coop.fov - show the local third-person FOV", [](void*, int, const char* const*, SkOut* out) {
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
    mouse::set_alt_wheel(nullptr);
    if (g_projection.viewer) restore(viewer_of(active_manager()));
}

} // namespace fov
