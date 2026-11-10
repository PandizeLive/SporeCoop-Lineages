// Primary-button clicks on the game window, taken from the window's own messages (BUG-0090,
// BUG-0145). Polling GetAsyncKeyState(VK_LBUTTON) read the physical left button, wherever the
// cursor and the focus were: a click in another window over the game clicked our galaxy-map tags,
// and with swapped mouse buttons the player's primary click did nothing while the secondary one
// (camera drags) did. WM_LBUTTONDOWN is the logical primary button and only reaches the game
// window when the click is on it. A thread hook (WH_GETMESSAGE) on the game's main thread sees it
// as the game takes it from its queue; the tests' input.click posts the same message.
#pragma once

#include <cstdint>

namespace mouse {

// tests: false = the old polling of the physical left button (coop.space map clickfix 0)
extern bool g_fix;

// install the hook (main thread; again after a hot reload). Safe to call every frame.
void install(const void* game_window);
// Return true to consume the Ctrl+Scroll event instead of native zoom.
void set_fov_wheel(bool (*handler)(int delta));
// The creature editor depth square (BUG-9103): client coordinates; true consumes the message.
void set_depth_input(bool (*handler)(unsigned message, uintptr_t wparam, int x, int y));
void set_graphics_input(bool (*handler)(unsigned message, uintptr_t value, int x, int y));
void set_list_input(bool (*handler)(unsigned message, uintptr_t value, int x, int y));
// remove it (on_unload: the hook procedure lives in this DLL)
void shutdown();
// primary clicks seen so far; a consumer keeps the last value it handled
uint32_t clicks();
// where the mouse last moved over the game window (client pixels; -1 before any move)
void last_move(int& x, int& y);
// where the last one was, in the game window's client pixels
void last_click(int& x, int& y);

} // namespace mouse
