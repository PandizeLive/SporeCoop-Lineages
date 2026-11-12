// Primary-button clicks on the game window (mouse.h).
#include "mouse.h"
#include "fixes.h"

#include <windows.h>
#include <string>

namespace mouse {

bool g_fix = true;
COOP_FIX("map.click", "BUG-0090 BUG-0145", g_fix, "space: map name tags react only to clicks in the game window");
static HHOOK g_hook;
static HWND g_wnd;
static bool (*g_fov_wheel)(int);
static bool (*g_graphics_input)(unsigned, uintptr_t, int, int);
static bool (*g_list_input)(unsigned, uintptr_t, int, int);
static bool (*g_depth_input)(unsigned, uintptr_t, int, int);
static bool (*g_tribe_input)(unsigned, uintptr_t, int, int); // BUG-9154: right presses on a party member's hut
static volatile uint32_t g_clicks;
static volatile int g_x, g_y;
static volatile int g_mx = -1, g_my = -1; // the last mouse move over the game window (client pixels)
static std::u16string g_chars; // BUG-0482: WM_CHAR seen since the last take_chars (main thread)
static size_t g_click_pos = std::u16string::npos; // BUG-0551: where in g_chars the last click came
static bool g_own_armed, g_last_own;
static int g_own_x, g_own_y;
static ULONGLONG g_own_until;

static bool ours(HWND w)
{
    return w && g_wnd && (w == g_wnd || IsChild(g_wnd, w));
}

static LRESULT CALLBACK proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE && lp) {
        MSG* m = (MSG*)lp;
        if (g_list_input && ours(m->hwnd) &&
            (m->message == WM_MOUSEWHEEL || m->message == WM_LBUTTONDOWN ||
             m->message == WM_LBUTTONUP || m->message == WM_MOUSEMOVE)) {
            POINT p{(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
            if (m->message == WM_MOUSEWHEEL)
                ScreenToClient(g_wnd, &p);
            else if (m->hwnd != g_wnd)
                MapWindowPoints(m->hwnd, g_wnd, &p, 1);
            if (g_list_input(m->message, m->wParam, p.x, p.y))
                m->message = WM_NULL;
        }
        // BUG-9103: the creature editor's depth square takes its own presses, drags and releases
        if (g_depth_input && ours(m->hwnd) &&
            (m->message == WM_LBUTTONDOWN || m->message == WM_LBUTTONUP || m->message == WM_MOUSEMOVE)) {
            POINT p{(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
            if (m->hwnd != g_wnd)
                MapWindowPoints(m->hwnd, g_wnd, &p, 1);
            if (g_depth_input(m->message, m->wParam, p.x, p.y))
                m->message = WM_NULL;
        }
        if (g_tribe_input && ours(m->hwnd) && (m->message == WM_RBUTTONDOWN || m->message == WM_RBUTTONUP)) {
            POINT p{(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
            if (m->hwnd != g_wnd)
                MapWindowPoints(m->hwnd, g_wnd, &p, 1);
            if (g_tribe_input(m->message, m->wParam, p.x, p.y))
                m->message = WM_NULL;
        }
        if (g_graphics_input && ours(m->hwnd) &&
            g_graphics_input(m->message, m->wParam, (short)LOWORD(m->lParam), (short)HIWORD(m->lParam)))
            m->message = WM_NULL;
        // BUG-9099: the key must be physically down now. The queue's key state (MK_CONTROL,
        // GetKeyState) stays "down" when Ctrl was released in another window, and then every
        // plain scroll widened the lens instead of zooming.
        if (m->message == WM_MOUSEWHEEL && ours(m->hwnd) && g_fov_wheel &&
            (LOWORD(m->wParam) & MK_CONTROL) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            g_fov_wheel((short)HIWORD(m->wParam)))
            m->message = WM_NULL;
        if (m->message == WM_MOUSEMOVE && ours(m->hwnd)) {
            POINT p{(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
            if (m->hwnd != g_wnd)
                MapWindowPoints(m->hwnd, g_wnd, &p, 1);
            g_mx = p.x;
            g_my = p.y;
        }
        if ((m->message == WM_LBUTTONDOWN || m->message == WM_LBUTTONDBLCLK) && ours(m->hwnd)) {
            POINT p{(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
            if (m->hwnd != g_wnd)
                MapWindowPoints(m->hwnd, g_wnd, &p, 1);
            g_x = p.x;
            g_y = p.y;
            g_clicks = g_clicks + 1;
            g_click_pos = g_chars.size();
            g_last_own = g_own_armed && p.x == g_own_x && p.y == g_own_y && GetTickCount64() < g_own_until;
            if (g_last_own)
                g_own_armed = false;
        }
        if (m->message == WM_CHAR && ours(m->hwnd) && g_chars.size() < 256)
            g_chars.push_back((char16_t)m->wParam);
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

void install(const void* game_window)
{
    g_wnd = (HWND)game_window;
    if (g_hook || !g_wnd)
        return;
    g_hook = SetWindowsHookExW(WH_GETMESSAGE, proc, nullptr, GetCurrentThreadId());
}

void set_fov_wheel(bool (*handler)(int delta)) { g_fov_wheel = handler; }
void set_tribe_input(bool (*handler)(unsigned, uintptr_t, int, int)) { g_tribe_input = handler; }
void set_graphics_input(bool (*handler)(unsigned, uintptr_t, int, int)) { g_graphics_input = handler; }
void set_list_input(bool (*handler)(unsigned, uintptr_t, int, int)) { g_list_input = handler; }
void set_depth_input(bool (*handler)(unsigned, uintptr_t, int, int)) { g_depth_input = handler; }

void shutdown()
{
    if (g_hook)
        UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

uint32_t clicks() { return g_clicks; }

void last_move(int& x, int& y)
{
    x = g_mx;
    y = g_my;
}

std::u16string take_chars(std::u16string* after_click)
{
    if (after_click)
        *after_click = g_click_pos != std::u16string::npos && g_click_pos < g_chars.size() ? g_chars.substr(g_click_pos)
                                                                                         : std::u16string();
    g_click_pos = std::u16string::npos;
    std::u16string r;
    r.swap(g_chars);
    return r;
}

void own_click(int x, int y)
{
    g_own_x = x;
    g_own_y = y;
    g_own_until = GetTickCount64() + 5000;
    g_own_armed = true;
}

bool last_click_own() { return g_last_own; }

void last_click(int& x, int& y)
{
    x = g_x;
    y = g_y;
}

} // namespace mouse
