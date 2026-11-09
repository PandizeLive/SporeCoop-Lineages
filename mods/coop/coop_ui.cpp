#include "coop_ui.h"
#include "trace.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "modes.h"
#include "mouse.h"
#include "net/fixes.h"
#include "notify.h"
#include "net/udp.h"
#include "notify_text.h"
#include "space_ext.h"
#include "sui.h"
#include "world.h"
#include "tribe.h"
#include "civ.h"
#include "fixes.h"
#include "slotfs.h"
#include "lineages_graphics.h"

namespace coop_ui {

static uint32_t g_text_font;

using sui::Rect;
using sui::Win;

static const SkHost* H;
static SkMod* M;
static Api A;
static bool g_ok;      // Spore UI available
static bool g_ru;      // Russian captions
static bool g_want_open, g_pending, g_dirty = true;
static double g_last_refresh;
static std::string g_status;
static uint32_t g_status_color = 0xFFFFD866;
static double g_status_until; // the status goes away then (0 = stays until the next session event)
bool g_status_fix = true;     // BUG-0104 (`coop.corefix ui_status 0`: the status of the last button stays)
COOP_FIX("core.ui_status", "BUG-0104", g_status_fix, "ui: the co-op window shows the session's status, not the last button's");
// BUG-0597: "Send my world" pressed in the main menu (no world loaded) or with nobody joined did nothing while
// the window said "Sending the world to everyone...": players read it as "solo does not start". The button is
// greyed out then and the lobby says why (a host can start or load a game alone, friends join any time and get
// the world by themselves). Off: the button always sends and says so (0.7.7).
static bool g_share_explain_fix = true;
COOP_FIX("core.ui_share_explain", "BUG-0597", g_share_explain_fix, "ui: Send my world is greyed out with no world or nobody joined, and the lobby says why");
// BUG-0598: a guest without own creations of the stage's kind saw "My cell: the world's own" with no arrows and
// no hint, and could not tell that a choice exists (a player asked where to pick the own creation). The row says
// whose it is ("the host's species") and how to get a choice (make one in an editor). Off: "the world's own" (0.7.7).
static bool g_creation_hint_fix = true;
COOP_FIX("core.ui_creation_hint", "BUG-0598", g_creation_hint_fix, "ui: the creation row says whose creation it is and how to get a choice");
static bool g_f9_down;
static bool g_lb_down;      // the left mouse button in the last frame (the Co-op item during the menu intro)
static uint32_t g_lb_seen;  // mouse::clicks() handled (BUG-0145)
static double g_skip_since; // the Co-op item was clicked during the menu intro: open once it is over
static double g_intro_over; // when that intro was over
static bool g_raise = true; // move the window above all UI layers when it opens
bool g_log_msgs; // debug: log UI messages that reach our handlers

constexpr uint32_t kIdMenuButton = 0xC00F0001, kIdDialog = 0xC00F0100;
constexpr uint32_t kIdMainMenu = 0x0615A5FF, kIdShare = 0x0615B616;
constexpr uint32_t kDialogLayout = 0x34A6339E; // the game's account dialog (frame, logo, close)
constexpr uint32_t kGold = 0xFFFFCC4D, kWhite = 0xFFFFFFFF, kDim = 0xFF9DB8CC, kGreen = 0xFF8CF08C,
                   kRed = 0xFFFF7A6A, kYellow = 0xFFFFD866;
constexpr float kW = 760, kH = 600;
constexpr uint32_t kDangerButtonShade = 0xFFAD535D;
constexpr int kSpaceRows = 12;
static bool g_space_page; // the lobby shows the Space page instead
static int g_lan_first, g_lan_max, g_lan_wheel_delta;
static bool g_lan_dragging;
constexpr int kLanRows = 6;
constexpr size_t kServerHistoryLimit = 24;
bool g_lan_pages_fix = true;
COOP_FIX("core.lan_pages", "BUG-0183", g_lan_pages_fix, "ui: every network game is accessible through the server list scrollbar");
static int g_space_first; // the first row shown (pages of kSpaceRows rows)

static const char* T(const char* en, const char* ru) { return g_ru ? ru : en; }

// ---------------- widgets with cached state ----------------
struct Cache {
    std::string text;
    uint32_t color = 0;
    int vis = -1;
    bool left = false; // left aligned label: the box is sized to the text (labels center their text)
    Rect box;
    int enabled = -1;
};
static std::map<Win, Cache> g_cache;
static int g_chat_scroll;            // chat lines scrolled up from the newest (BUG-0129)
constexpr size_t kChatCols = 70;     // letter cells of a chat line (680 px)

// width of a text in the dialog font (average glyph width, measured at 1080p)
static float text_width(const std::string& t)
{
    int n = 0;
    for (unsigned char ch : t)
        n += (ch & 0xC0) != 0x80;
    return n * 9.3f + 30;
}

static void fit(Win w, const Cache& c)
{
    Rect r = c.box;
    r.x2 = std::min(r.x2, r.x1 + text_width(c.text));
    sui::set_area(w, r);
}

static void text(Win w, const std::string& t)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.text != t) {
        c.text = t;
        sui::set_caption(w, t);
        if (c.left)
            fit(w, c);
    }
}
static void color(Win w, uint32_t argb)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.color != argb) {
        c.color = argb;
        sui::set_label_color(w, argb);
    }
}
static void enable(Win w, bool on)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.enabled != (int)on) {
        c.enabled = on;
        sui::set_enabled(w, on);
    }
}
static void show(Win w, bool on)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.vis != (int)on) {
        c.vis = on;
        sui::set_visible(w, on);
    }
}

// ---------------- main menu item ----------------
static Win g_menu_btn, g_menu_parent;
// BUG-0163: the item is a child of the galaxy menu window, which the game frees when it leaves the
// menu; the pointer was dropped only by the next refresh (up to 1 s later), and a click meanwhile
// made a virtual call on the freed item. It goes as soon as the mode is not the menu.
// `coop.corefix menu_drop 0` = the old behaviour (tests).
bool g_menu_drop_fix = true;
bool g_recenter_fix = true;
COOP_FIX_UNSAFE("core.menu_drop", "BUG-0163", g_menu_drop_fix, "ui: the galaxy menu's Co-op item pointer goes with the menu");
COOP_FIX("core.window_recenter", "BUG-0113", g_recenter_fix, "ui: the co-op window is placed again after a resolution change");

// The window is centred on the screen it opens on (BUG-0113: it is kept for the whole run, and was
// centred once; after a smaller resolution in the options its bottom row was off the screen).
static int g_placed_w = -1, g_placed_h = -1;
static void place_window(Win root)
{
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    if (sw <= 0 || sh <= 0 || (sw == g_placed_w && sh == g_placed_h))
        return;
    g_placed_w = sw;
    g_placed_h = sh;
    float x = std::max(0.f, (sw - kW) / 2), y = std::max(0.f, (sh - kH) / 2);
    sui::set_area(root, Rect{x, y, x + kW, y + kH});
}
static bool in_menu() { return A.mode && A.mode() == modes::kMenu; }
static void* g_menu_drawable;
static Win g_menu_drawable_for; // the menu window the drawable was made for

static void on_menu_click() { open(true); }

static void ensure_menu_button()
{
    Win root = sui::main_window();
    Win menu = A.mode && A.mode() == modes::kMenu ? sui::find(root, kIdMainMenu) : nullptr;
    if (!menu) {
        g_menu_btn = g_menu_parent = nullptr;
        return;
    }
    if (g_menu_btn && g_menu_parent == menu)
        return;
    Win share = sui::find(menu, kIdShare);
    if (!share)
        return;
    // a stale item from before a hot reload: remove it, a fresh one gets our handler
    if (Win old = sui::find(menu, kIdMenuButton))
        sui::remove_child(menu, old);
    Rect a = sui::area(share);
    float dy = 95; // below "Galactic Adventures"
    Rect r{a.x1, a.y1 + dy, a.x2, a.y2 + dy};
    // built detached and added to the menu last: changing the drawable of a window that is already
    // in the galaxy menu made the game render its planet view synchronously from here (outside the
    // frame), and DXVK deadlocked the main thread right after a stage was left (frames stopped)
    Win b = sui::button_like(nullptr, share, r, T("Co-op", "Кооператив"));
    if (!b)
        return;
    sui::set_ids(b, kIdMenuButton, kIdMenuButton);
    // a fresh drawable for every galaxy menu: it is a copy of the "Share" button's drawable (its
    // images and current state), and the copy from an earlier menu, set on the new button after a
    // few stage switches, hung the main thread inside SetDrawable (a render from the planet view
    // waiting in DXVK, docs/known-issues.md history). The old one stays with the old button.
    if (g_menu_drawable_for != menu) {
        g_menu_drawable = nullptr;
        g_menu_drawable_for = menu;
    }
    if (!g_menu_drawable) {
        void* icon = sui::image(sui::fnv("coop_icon"), 0x2F7D0004, sui::fnv("sporecoop"));
        g_menu_drawable = sui::std_drawable_with_icon(sui::drawable(share), icon);
        if (!icon)
            H->log(M, SK_WARN, "ui: coop_icon not found (Data/SporeCoop.package missing?)");
    }
    if (g_menu_drawable)
        sui::set_drawable(b, g_menu_drawable);
    sui::add_child(menu, b);
    sui::add_handler(b, [](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg 0x%X src=%p %08X %08X %08X %08X", m.type, m.source, m.a, m.b, m.c, m.d);
        if (sui::is_click(m)) {
            on_menu_click();
            return true;
        }
        return false;
    });
    g_menu_btn = b;
    g_menu_parent = menu;
    // the version under the item: a player's screenshot of the menu tells which SporeCoop runs
    if (Win old = sui::find(menu, kIdMenuButton + 1))
        sui::remove_child(menu, old);
    if (A.version_line) {
        std::string v = A.version_line();
        v = v.substr(0, v.find("  -  "));
        if (Win l = sui::label(menu, {r.x1 + 6, r.y2 - 4, r.x2 + 90, r.y2 + 14}, v, sui::font(share), kDim,
                               sui::kAlignLeft))
            sui::set_ids(l, kIdMenuButton + 1, 0);
    }
    H->log(M, SK_INFO, "ui: co-op item added to the main menu");
}

// ---------------- dialog ----------------
struct Dialog {
    Win root = nullptr, layer = nullptr;
    uint32_t font_body = 0, font_button = 0;
    Win like_button = nullptr; // gold "Login" button of the layout
    Win subtitle = nullptr;
    Win version = nullptr, connect_version = nullptr, report_btn = nullptr, report_caption = nullptr;
    // connect page
    std::vector<Win> page_connect;
    Win transfer_btn[2]{}, seat_btn[4]{}, difficulty_value[5]{};
    Win name_edit = nullptr, addr_edit = nullptr, port_edit = nullptr, join_btn = nullptr, host_btn = nullptr;
    Win scaling_edit = nullptr, join_port = nullptr, join_password = nullptr, host_password = nullptr;
    Win selected_info = nullptr;
    unsigned seats = 2;
    struct SavedServer { std::string address, name; };
    std::vector<SavedServer> saved_servers;
    bool servers_loaded = false;
    struct Privacy { Win button, eye, crossed; };
    std::map<std::string, Privacy> privacy;
    bool show_host_addresses = false;
    Win next_seats_edit = nullptr, queue_seats = nullptr;
    Win lan_hdr = nullptr, host_hint = nullptr, status = nullptr, update_btn = nullptr;
    Win lan_rows[kLanRows] = {};
    std::string lan_addr[kLanRows];
    std::map<Win, bool> hidden_fields;
    Win lan_track = nullptr, lan_thumb = nullptr;
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win invite_btn = nullptr, rules = nullptr, transfer_mode = nullptr, preview_status = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr, leave_caption = nullptr;
    Win row_name[4] = {}, row_stage[4] = {}, row_ping[4] = {}, row_ready[4] = {};
    Win chat_lines[5] = {};
    Win chat_up = nullptr, chat_down = nullptr;
    Win creation = nullptr, creation_prev = nullptr, creation_next = nullptr, give_btn = nullptr, give_next = nullptr, jump_btn = nullptr;
    Win space_btn = nullptr; // lobby -> the Space page (Space stage only)
    // Space page: the rows of the space features (space_ext.h)
    std::vector<Win> page_space;
    Win space_title = nullptr, space_back = nullptr, space_prev = nullptr, space_next = nullptr, space_pageno = nullptr;
    Win space_text[kSpaceRows] = {}, space_button[kSpaceRows] = {};
    std::function<void()> space_action[kSpaceRows];
};
static Dialog D;
static std::map<std::string, Win> g_named; // for tests (coop.uistate)

static Win add_label(std::vector<Win>& page, const Rect& r, const std::string& t, uint32_t argb,
                     int align = sui::kAlignLeft)
{
    Win w = sui::label(D.root, r, t, D.font_body, argb, align);
    if (!w)
        return nullptr;
    page.push_back(w);
    Cache c{t, argb, 1, align == sui::kAlignLeft, r};
    if (c.left)
        fit(w, c);
    g_cache[w] = c;
    return w;
}

static Win shade_danger_button(std::vector<Win>& page, Win button, const std::string& title)
{
    if (!button) return nullptr;
    // Native window shading multiplies the caption color as well as the skin.
    // Draw the caption above the button with a mouse-transparent native label.
    Win caption = add_label(page, sui::area(button), title, kWhite, sui::kAlignCenter);
    if (caption) {
        sui::set_shade_color(button, kDangerButtonShade);
        sui::set_label_color(button, 0);
    }
    return caption;
}

// BUG-0273: while a cinematic or a caption plays, the game holds its input lock
// (cGameInputManager +0x110 above 0) and drops every click on the UI, also on our window: a
// guest's "I'm ready" during the brain-upgrade captions of a vote did nothing, the vote ran out of
// time. The click is taken from the window's messages instead (mouse.h) and given to the button
// under it, unless the game delivered it itself within kLockedClickWait. Off: the clicks are
// dropped while the lock is held (0.7.3.0).
bool g_locked_clicks_fix = true;
COOP_FIX("core.ui_locked_clicks", "BUG-0273", g_locked_clicks_fix, "ui: co-op window buttons take clicks while a caption holds the game's input lock");
static bool g_tribal_clicks_fix = true;
COOP_FIX("core.ui_tribal_clicks", "BUG-9018", g_tribal_clicks_fix, "ui: Tribal co-op buttons recover clicks swallowed without a native input lock");
static std::map<Win, std::function<void()>> g_btn_fn; // every button of the window and its action
static std::map<Win, double> g_btn_native;            // when the game last delivered a click to it
static Win g_locked_btn;                              // a click under the lock waits for the game
static double g_locked_at;
static unsigned g_locked_clicks, g_locked_native;     // tests: clicks given / the game delivered itself
constexpr double kLockedClickWait = 0.5;

static int input_lock()
{
    uintptr_t get = H ? H->sym("Simulator::cGameInputManager::Get") : 0;
    using Get_t = uint8_t*(__cdecl*)();
    uint8_t* im = get ? ((Get_t)get)() : nullptr;
    return im && !IsBadReadPtr(im + 0x110, 4) ? *(int*)(im + 0x110) : 0;
}

static Win add_button(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t,
                      std::function<void()> fn)
{
    Win w = sui::button_like(D.root, D.like_button, r, t);
    if (!w)
        return nullptr;
    sui::set_font(w, D.font_body); // the dialog text font reads better than the small button font
    page.push_back(w);
    g_named[name] = w;
    g_cache[w] = Cache{t, 0, 1};
    sui::add_handler(w, [fn, w, name = std::string(name)](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg %s 0x%X src=%p %08X %08X %08X", name.c_str(), m.type, m.source, m.a, m.b, m.c);
        if (sui::is_click(m)) {
            trace::line("ui", "click %s", name.c_str());
            g_btn_native[w] = H->time_seconds();
            fn();
            return true;
        }
        return false;
    });
    g_btn_fn[w] = fn;
    return w;
}

// a click under the game's input lock on one of our visible buttons (BUG-0273); main thread
static void locked_click_tick(bool clicked)
{
    double now = H->time_seconds();
    if (g_locked_btn && now - g_locked_at >= kLockedClickWait) {
        Win w = g_locked_btn;
        g_locked_btn = nullptr;
        auto f = g_btn_fn.find(w);
        auto n = g_btn_native.find(w);
        if (n != g_btn_native.end() && n->second >= g_locked_at - 0.05)
            g_locked_native++; // the game delivered it after all: no second press
        else if (f != g_btn_fn.end() && D.root && sui::visible(D.root) && sui::visible(w)) {
            g_locked_clicks++;
            std::string nm;
            for (auto& [n, win] : g_named)
                if (win == w)
                    nm = n;
            H->log(M, SK_INFO, "ui: a click on the co-op window while the game's input is locked (a caption): pressed %s",
                   nm.c_str());
            static std::function<void()> s_fn;
            s_fn = f->second;
            H->run_outside_frame([](void*) { if (s_fn) s_fn(); }, nullptr);
        }
    }
    const bool tribal_fallback = g_tribal_clicks_fix && A.mode && A.mode() == modes::kTribe && tribe::village_orders();
    if (!clicked || !g_locked_clicks_fix || !mouse::g_fix || !g_want_open || !D.root || !sui::visible(D.root) ||
        g_locked_btn || (input_lock() <= 0 && !tribal_fallback))
        return;
    int cx = 0, cy = 0;
    mouse::last_click(cx, cy);
    for (auto& [w, fn] : g_btn_fn) {
        if (!sui::visible(w))
            continue;
        Rect r = sui::screen_rect(w);
        if (cx >= r.x1 && cx < r.x2 && cy >= r.y1 && cy < r.y2) {
            g_locked_btn = w;
            g_locked_at = now;
            return;
        }
    }
}

static Win add_edit(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t, int max_len,
                    bool draw_background = true)
{
    Win w = sui::text_edit(D.root, r, t, D.font_body, max_len, draw_background);
    if (w) {
        page.push_back(w);
        g_named[name] = w;
    }
    return w;
}

// ttl: seconds the text stays (0: until the session event that ends it, session_event)
static void set_status(const std::string& s, uint32_t c, double ttl = 0)
{
    g_status = s;
    g_status_color = c;
    g_status_until = ttl > 0 ? H->time_seconds() + ttl : 0;
    g_dirty = true;
}

// BUG-0104: "Connecting to ..." stayed for the whole session (the lobby showed it as the world line)
// and hid the update line on the connect page for good; the end of the join or of the session now
// replaces it
void session_event(bool connected, const std::string& why)
{
    if (!g_status_fix || !g_ok)
        return;
    if (connected)
        set_status("", kYellow);
    else
        set_status(std::string(T("Disconnected: ", "Отключено: ")) + why, kRed, 30);
}

static void save_str(const char* section, const char* key, const std::string& v)
{
    // BUG-0245: SporeKit refuses a value that is not ASCII when the file could not be made Unicode
    // (it became "????" before): say so instead of losing the typed text silently at the next start
    if (H->config_set_str && H->config_set_str(M, section, key, v.c_str()) != 0)
        notify::show(std::string("could not save the ") + key + ", try again", "danger");
}
static std::string load_str(const char* section, const char* key, const char* def)
{
    char b[256];
    H->config_str(M, section, key, def, b, sizeof b);
    return b;
}

static void apply_name()
{
    std::string n = sui::edit_text(D.name_edit);
    if (!n.empty() && n != A.name()) {
        A.set_name(n);
        save_str("coop", "name", n);
    }
}

static void do_join(const std::string& address)
{
    apply_name();
    std::string addr = address;
    if (net::g_fix.addr_check) // BUG-0138: a leading space or the whole pasted hint line
        addr = net::Endpoint::clean(addr);
    while (!addr.empty() && addr.back() == ' ')
        addr.pop_back();
    if (addr.empty()) {
        set_status(T("Enter the host address, e.g. 192.168.1.5:47810", "Введите адрес хоста, например 192.168.1.5:47810"),
                   kRed);
        return;
    }
    const auto port = sui::edit_text(D.join_port);
    if (addr.find(':') == std::string::npos && !port.empty())
        addr += ":" + port;
    std::string err;
    if (!A.join(addr, err, sui::edit_text(D.join_password))) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed, g_status_fix ? 30 : 0);
        return;
    }
    save_str("ui", "last_address", addr);
    set_status(T("Connecting...", "Подключение..."), kYellow);
}

static void eye_button(std::vector<Win>& page, const char* name, Rect area, std::function<void()> toggle)
{
    const auto group = sui::fnv("sporecoop");
    Win button = add_button(page, name, area, "", std::move(toggle));
    Win eye = nullptr, crossed = nullptr;
    if (sui::has_image(sui::fnv("lineages_eye"), group) && sui::has_image(sui::fnv("lineages_eye_off"), group)) {
        const float x = area.x1 + (area.w() - 20.f) / 2;
        const float y = area.y1 + (area.h() - 14.f) / 2;
        eye = sui::image_window(D.root, sui::fnv("lineages_eye"), group, x, y);
        crossed = sui::image_window(D.root, sui::fnv("lineages_eye_off"), group, x, y);
        for (Win icon : {eye, crossed}) {
            if (!icon) continue;
            sui::set_flag(icon, sui::kFlagIgnoreMouse, true);
            page.push_back(icon);
        }
        if (button && eye && crossed)
            sui::set_drawable(button, nullptr); // the field itself is the button background
    }
    D.privacy[name] = {button, eye, crossed};
}

static void show_eye(const char* name, bool hidden)
{
    const auto& eye = D.privacy.at(name);
    if (eye.eye && eye.crossed) {
        show(eye.eye, !hidden);
        show(eye.crossed, hidden);
    } else
        text(eye.button, hidden ? "Show" : "Hide");
}

static void set_field_hidden(Win field, bool hidden)
{
    D.hidden_fields[field] = hidden;
    sui::set_edit_masked(field, hidden);
}

static void privacy_button(std::vector<Win>& page, const char* name, Rect area, Win field, bool hidden)
{
    set_field_hidden(field, hidden);
    eye_button(page, name, area, [field] {
        set_field_hidden(field, !D.hidden_fields[field]);
        g_dirty = true;
    });
}

static std::string connection_address()
{
    auto address = sui::edit_text(D.addr_edit);
    const auto port = sui::edit_text(D.join_port);
    if (!address.empty() && address.find(':') == std::string::npos && !port.empty())
        address += ":" + port;
    return address;
}

static void load_servers()
{
    if (D.servers_loaded) return;
    D.servers_loaded = true;
    for (size_t i = 0; i < kServerHistoryLimit; ++i) {
        const auto key = std::to_string(i);
        auto address = load_str("servers", ("address" + key).c_str(), "");
        if (!address.empty())
            D.saved_servers.push_back({std::move(address), load_str("servers", ("name" + key).c_str(), "")});
    }
}

static void persist_servers(size_t previous_size)
{
    for (size_t i = 0; i < std::max(previous_size, D.saved_servers.size()); ++i) {
        const auto key = std::to_string(i);
        const auto saved = i < D.saved_servers.size() ? D.saved_servers[i] : Dialog::SavedServer{};
        save_str("servers", ("address" + key).c_str(), saved.address);
        save_str("servers", ("name" + key).c_str(), saved.name);
    }
}

void remember_server(const std::string& address, const std::string& name)
{
    if (!g_ok || address.empty()) return;
    load_servers();
    const auto count = D.saved_servers.size();
    std::erase_if(D.saved_servers, [&](const Dialog::SavedServer& server) { return server.address == address; });
    D.saved_servers.insert(D.saved_servers.begin(), {address, name});
    if (D.saved_servers.size() > kServerHistoryLimit) D.saved_servers.pop_back();
    persist_servers(count);
    g_lan_first = 0;
    g_dirty = true;
}

static bool server_list_input(unsigned message, uintptr_t value, int x, int y)
{
    if (!D.root || !g_want_open || A.role() != 0 || !sui::visible(D.root)) {
        g_lan_dragging = false;
        g_lan_wheel_delta = 0;
        return false;
    }
    const auto root = sui::screen_rect(D.root);
    const float local_x = x - root.x1, local_y = y - root.y1;
    const bool over_list = local_x >= 24 && local_x < 360 && local_y >= 171 && local_y < 335;
    if (message == WM_MOUSEWHEEL && over_list) {
        g_lan_wheel_delta += (short)HIWORD(value);
        g_lan_first = std::clamp(g_lan_first - g_lan_wheel_delta / WHEEL_DELTA, 0, g_lan_max);
        g_lan_wheel_delta %= WHEEL_DELTA;
        g_dirty = true;
        return true;
    }
    if (message == WM_LBUTTONDOWN && over_list && local_x >= 338 && g_lan_max > 0)
        g_lan_dragging = true;
    if (!g_lan_dragging) return false;
    if (message == WM_LBUTTONUP || (message == WM_MOUSEMOVE && !(value & MK_LBUTTON))) {
        g_lan_dragging = false;
        return true;
    }
    if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE) {
        const float ratio = std::clamp((local_y - 192.f) / 122.f, 0.f, 1.f);
        g_lan_first = (int)(ratio * g_lan_max + .5f);
        g_dirty = true;
        return true;
    }
    return false;
}

static void do_host()
{
    apply_name();
    int port = atoi(sui::edit_text(D.port_edit).c_str());
    if (port <= 0 || port > 65535)
        port = A.default_port;
    std::string err;
    auto seats = std::to_string(D.seats);
    auto scaling = sui::edit_text(D.scaling_edit);
    if (seats.size() != 1 || seats[0] < '1' || seats[0] > '4' ||
        scaling.empty() || scaling.find_first_not_of("0123456789") != std::string::npos ||
        scaling.size() > 5 || strtoul(scaling.c_str(),nullptr,10) > UINT16_MAX) {
        set_status(T("Use 1-4 founders and a whole scaling percentage.", "Укажите 1–4 основателей и целый процент."),kRed);
        return;
    }
    if (!A.host((uint16_t)port,D.seats,unsigned(strtoul(scaling.c_str(),nullptr,10)),err,
                sui::edit_text(D.host_password))) {
        set_status(std::string(T("Cannot host: ", "Не удалось создать игру: ")) + err, kRed);
        return;
    }
    save_str("ui","new_founders",seats);
    save_str("ui","new_scaling",scaling);
    save_str("ui", "host_port", std::to_string(port));
    set_status("", kYellow);
}

// "Report a problem": SporeKit writes mods\logs\SporeCoop-report-<time>.zip (environment, co-op state,
// the logs, faults, the screenshots of the marks); the folder opens with the file selected so the
// player can attach it. A screenshot of the screen at the click goes in first: SporeKit takes it at
// the next frame, the archive is written once it is there (at most kReportShotWait s later).
static constexpr double kReportShotWait = 4.0;
static double g_report_wait_until;  // > 0: the report waits for its screenshot
static bool g_report_queued;        // finish_report is queued outside the frame
static int g_report_shot;           // what report_shot answered (1 requested, 0 not, -1 off)

static void finish_report(void*)
{
    g_report_queued = false;
    bool shot = g_report_shot > 0 && A.report_shot_state && A.report_shot_state() == 0;
    std::string p = A.report ? A.report() : "";
    if (p.empty()) {
        set_status(T("This SporeKit cannot write reports (update SporeCoop)", "Этот SporeKit не умеет писать отчёты (обновите SporeCoop)"),
                   kRed);
        return;
    }
    set_status(std::string(T("Report saved: ", "Отчёт сохранён: ")) + p.substr(p.find_last_of("\\/") + 1) +
                   T(" - attach it to an issue on GitHub; the other players should send theirs too",
                     " - приложите его к issue на GitHub; другие игроки пусть пришлют свои") +
                   (shot ? T(". A screenshot of the game is included (coop.ini [report] screenshot=0 turns it off)",
                             ". В отчёт входит снимок экрана игры (отключить: coop.ini [report] screenshot=0)")
                         : ""),
               kGreen);
    std::wstring w(p.begin(), p.end()); // the path is ASCII-safe enough for /select; UTF-8 below if not
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    if (n > 0) {
        w.resize(n);
        MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w.data(), n);
        w.resize(n - 1);
    }
    std::wstring args = L"/select,\"" + w + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

static void do_report()
{
    if (g_report_wait_until > 0 || g_report_queued)
        return; // one is being made
    for (Win password : {D.join_password, D.host_password}) {
        if (!password) continue;
        set_field_hidden(password, true);
    }
    g_dirty = true;
    g_report_shot = A.report_shot ? A.report_shot() : 0;
    if (g_report_shot > 0) {
        H->log(M, SK_INFO, "ui: report: waiting for its screenshot");
        g_report_wait_until = H->time_seconds() + kReportShotWait;
        set_status(T("Making the report...", "Создаю отчёт..."), kYellow);
        return;
    }
    finish_report(nullptr);
}

// tick: the report's screenshot is written (or failed, or did not come): the archive, outside the frame
static void report_tick(double now)
{
    if (g_report_wait_until <= 0 || g_report_queued)
        return;
    if (A.report_shot_state && A.report_shot_state() == 1 && now < g_report_wait_until)
        return;
    g_report_wait_until = 0;
    g_report_queued = true;
    H->run_outside_frame(finish_report, nullptr);
}

// BUG-0597: why "Send my world" sends nothing now (0: it sends; 1 no world loaded; 2 nobody joined)
static int share_blocked()
{
    if (!g_share_explain_fix || A.role() != 1 || (A.vote_active && A.vote_active()) || !A.share_state)
        return 0; // (during a vote the button is "Start now")
    return A.share_state();
}

static void do_send_chat()
{
    std::string t = sui::edit_text(D.chat_edit);
    if (t.empty())
        return;
    A.say(t);
    sui::set_edit_text(D.chat_edit, "");
    g_dirty = true;
}

static void build_dialog()
{
    Win root = sui::find(sui::main_window(), kIdDialog);
    if (root) { // left over from before a hot reload: its handlers are dead
        if (Win p = sui::parent(root))
            sui::remove_child(p, root);
    }
    root = sui::load_layout(kDialogLayout);
    if (!root) {
        H->log(M, SK_ERROR, "ui: cannot load the dialog layout %08X", kDialogLayout);
        return;
    }
    D = Dialog{};
    g_named.clear();
    g_btn_fn.clear();
    g_btn_native.clear();
    g_locked_btn = nullptr;
    D.root = root;
    D.layer = sui::parent(root);
    sui::add_ref(root); // ours: the window survives mode changes that clear the layer
    sui::set_ids(root, kIdDialog, 0);
    Win text_win = sui::find(root, 0x0451D381), login = sui::find(root, 0x0519E168);
    D.font_body = sui::font(text_win);
    g_text_font = D.font_body;
    H->log(M, SK_DEBUG, "ui: dialog font %08X", D.font_body);
    D.like_button = login;
    D.font_button = sui::font(login);
    // keep the frame, glow, logo and close button; hide the account dialog's own content
    for (uint32_t id : {0x0451D381u, 0x0519E168u, 0x06555818u, 0x065D20A8u, 0x0519D9B8u, 0x0451A4C0u})
        if (Win w = sui::find(root, id))
            sui::set_visible(w, false);
    if (Win x = sui::find(root, 0xFFFFFFF2u)) {
        g_named["close"] = x;
        sui::add_handler(x, [](Win, const sui::Msg& m) {
            if (sui::is_click(m)) {
                open(false);
                return true;
            }
            return false;
        });
    }
    place_window(root);
    // The account dialog draws the logo and the hex frame in separate widgets.
    for (Win child : sui::children(root)) {
        const Rect r = sui::area(child);
        if (sui::control_id(child) || r.y1 != 10.f || r.y2 - r.y1 != 49.f ||
            r.x2 - r.x1 != 178.f || !sui::drawable(child))
            continue;
        // The native widget and the export both have transparent edge padding.
        constexpr float suffix_offset = 158.f;
        constexpr float logo_x = (kW - suffix_offset - 400.f) / 2;
        if (!sui::has_image(sui::fnv("lineages_wordmark_suffix"), sui::fnv("sporecoop"))) break;
        if (Win suffix = sui::image_window(root, sui::fnv("lineages_wordmark_suffix"), sui::fnv("sporecoop"),
                                           logo_x + suffix_offset, 6.f)) {
            sui::set_flag(suffix, sui::kFlagIgnoreMouse, true);
            const float offset = logo_x - sui::real_area(child).x1;
            sui::set_area(child, {r.x1 + offset, r.y1, r.x2 + offset, r.y2});
        }
        break;
    }

    std::vector<Win> common;
    D.subtitle = add_label(common, {0, 70, kW, 96}, T("CO-OP", "КООПЕРАТИВ"), kGold, sui::kAlignCenter);
    // every page: which SporeCoop and game build run, and a report for bug reports
    D.version = add_label(common, {40, 548, 437, 570}, A.version_line ? A.version_line() : "", kDim);
    D.report_btn = add_button(common, "report", {545, 98, 720, 134}, T("Report Issue", "Сообщить об ошибке"),
                              do_report);
    D.report_caption = shade_danger_button(common, D.report_btn, T("Report Issue", "Сообщить об ошибке"));

    // ---- connect page ----
    auto& P = D.page_connect;
    D.connect_version = add_label(P, {36, 80, 319, 108}, "", kDim);
    const auto panel_id = sui::fnv("lineages_connect_panels");
    const auto ui_group = sui::fnv("sporecoop");
    Win panel = sui::has_image(panel_id, ui_group) ? sui::image_window(root, panel_id, ui_group, 24, 171) : nullptr;
    if (panel) P.push_back(panel);
    add_label(P, {250, 79, 510, 109}, "Username", kGold, sui::kAlignCenter);
    D.name_edit = add_edit(P, "name", {250, 109, 510, 140}, A.name(), 24);
    add_label(P, {36, 142, 225, 171}, "Server List", kGold);
    add_label(P, {393, 142, 718, 171}, "Connection Info", kGold);
    add_button(P, "refreshlan", {243, 142, 352, 171}, "Refresh", [] {
        g_lan_first = 0;
        if (A.lan_scan) { A.lan_scan(false); A.lan_scan(true); }
        g_dirty = true;
    });
    D.lan_hdr = add_label(P, {37, 177, 334, 199}, "", kDim);
    D.lan_track = sui::image_window(root, sui::fnv("lineages_scroll_track"), sui::fnv("sporecoop"), 342, 178);
    D.lan_thumb = sui::image_window(root, sui::fnv("lineages_scroll_thumb"), sui::fnv("sporecoop"), 342, 178);
    for (Win part : {D.lan_track, D.lan_thumb}) {
        if (!part) continue;
        sui::set_flag(part, sui::kFlagIgnoreMouse, true);
        P.push_back(part);
    }
    for (int i = 0; i < kLanRows; ++i) {
        char name[16];
        snprintf(name, sizeof name, "lan%d", i);
        D.lan_rows[i] = add_button(P, name, {35, 177.f + i * 26, 334, 202.f + i * 26}, "", [i] {
            if (D.lan_addr[i].empty()) return;
            const auto& address = D.lan_addr[i];
            auto split = address.rfind(':');
            sui::set_edit_text(D.addr_edit, address.substr(0, split));
            sui::set_edit_text(D.join_port, split == std::string::npos ? "" : address.substr(split + 1));
            sui::set_edit_text(D.join_password, "");
            g_dirty = true;
        });
    }
    add_label(P, {393, 179, 490, 204}, "Address", kWhite);
    add_label(P, {393, 211, 490, 236}, "Port", kWhite);
    add_label(P, {393, 243, 490, 268}, "Password", kWhite);
    const auto last_address = load_str("ui", "last_address", "");
    const auto last_port = last_address.rfind(':');
    D.addr_edit = add_edit(P, "address", {489, 177, 677, 205}, last_address.substr(0, last_port), 64, !panel);
    D.join_port = add_edit(P, "joinport", {489, 209, 677, 237},
                           last_port == std::string::npos ? "47810" : last_address.substr(last_port + 1), 5, !panel);
    D.join_password = add_edit(P, "joinpassword", {489, 241, 677, 269}, "", 48, !panel);
    load_servers();
    privacy_button(P, "hideaddress", {678, 177, 718, 205}, D.addr_edit, false);
    privacy_button(P, "hidejoinport", {678, 209, 718, 237}, D.join_port, false);
    privacy_button(P, "hidejoinpassword", {678, 241, 718, 269}, D.join_password, true);
    D.selected_info = add_label(P, {393, 273, 716, 295}, "", kDim);
    D.join_btn = add_button(P, "join", {393, 298, 533, 331}, "Join",
                            [] { do_join(sui::edit_text(D.addr_edit)); });
    add_button(P, "saveserver", {539, 298, 624, 331}, "Save", [] {
        auto address = connection_address();
        const auto count = D.saved_servers.size();
        if (address.empty() || count == kServerHistoryLimit ||
            std::any_of(D.saved_servers.begin(), D.saved_servers.end(),
                        [&](const Dialog::SavedServer& server) { return server.address == address; })) return;
        D.saved_servers.push_back({std::move(address), {}});
        persist_servers(count);
        g_dirty = true;
    });
    Win remove = add_button(P, "deleteserver", {630, 298, 718, 331}, "Delete", [] {
        const auto address = connection_address();
        auto it = std::find_if(D.saved_servers.begin(), D.saved_servers.end(),
                               [&](const Dialog::SavedServer& server) { return server.address == address; });
        if (it == D.saved_servers.end()) return;
        const auto count = D.saved_servers.size();
        D.saved_servers.erase(it);
        persist_servers(count);
        g_dirty = true;
    });
    Win delete_caption = shade_danger_button(P, remove, "Delete");
    if (delete_caption) sui::set_caption(remove, "");

    add_label(P, {24, 336, 735, 366}, "Host Server", kGold, sui::kAlignCenter);
    add_label(P, {36, 375, 136, 398}, "Port (UDP)", kWhite);
    add_label(P, {36, 404, 136, 427}, "Password", kWhite);
    add_label(P, {36, 433, 136, 456}, "Seats", kWhite);
    add_label(P, {36, 462, 136, 485}, "Transfer", kWhite);
    D.port_edit = add_edit(P, "port", {140, 373, 310, 399}, load_str("ui", "host_port", std::to_string(A.default_port).c_str()), 5, !panel);
    D.host_password = add_edit(P, "hostpassword", {140, 402, 310, 428}, "", 48, !panel);
    privacy_button(P, "hidehostport", {311, 373, 351, 399}, D.port_edit, false);
    privacy_button(P, "hidehostpassword", {311, 402, 351, 428}, D.host_password, true);
    D.seats = std::clamp(atoi(load_str("ui", "new_founders", "2").c_str()), 1, 4);
    for (unsigned i = 0; i < 4; ++i) {
        const auto number = std::to_string(i + 1);
        D.seat_btn[i] = add_button(P, ("seat" + number).c_str(), {140.f + i * 53, 430, 190.f + i * 53, 458},
                                   number, [i] { D.seats = i + 1; g_dirty = true; });
    }
    for (unsigned i = 0; i < 2; ++i) {
        D.transfer_btn[i] = add_button(P, i == 0 ? "optimal" : "fast", {140.f + i * 106, 459, 243.f + i * 106, 489},
                                       i == 0 ? "Optimal" : "Fast", [i] {
            if (A.set_paced_transfer) A.set_paced_transfer(i == 0);
            save_str("coop", "paced_world_transfer", i == 0 ? "1" : "0");
            g_dirty = true;
        });
    }
    D.host_hint = add_label(P, {44, 492, 306, 532}, "", kDim);
    eye_button(P, "hidehostaddresses", {311, 491, 351, 531}, [] {
        D.show_host_addresses = !D.show_host_addresses;
        g_dirty = true;
    });

    add_label(P, {393, 372, 531, 398}, "Difficulty", kGold);
    D.scaling_edit = add_edit(P, "founderscaling", {540, 372, 595, 398}, load_str("ui", "new_scaling", "100"), 5);
    if (Win unit = add_label(P, {604, 373, 718, 397}, "% per extra seat", kDim))
        sui::set_font(unit, D.font_button);
    constexpr const char* labels[] = {"Objective effort", "Enemy HP / damage", "Creature hunger drain",
                                      "City takeover resistance", "Civ military interval"};
    for (int i = 0; i < 5; ++i) {
        const float y = 400.f + i * 19;
        Win label = add_label(P, {393, y, 614, y + 19}, labels[i], kDim);
        D.difficulty_value[i] = add_label(P, {615, y, 712, y + 19}, "", kWhite, sui::kAlignRight);
        if (label) sui::set_font(label, D.font_button);
        if (D.difficulty_value[i]) sui::set_font(D.difficulty_value[i], D.font_button);
    }
    D.host_btn = add_button(P, "host", {393, 497, 718, 534}, "Start Server", do_host);
    D.status = add_label(P, {40, 535, 503, 558}, "", kYellow);
    D.update_btn = add_button(P, "update", {515, 536, 720, 576}, "", [] {
        if (A.update_do) A.update_do();
        g_dirty = true;
    });
    // The smaller native button font keeps narrow actions legible at 800×600.
    for (Win w : P)
        if (g_btn_fn.contains(w) || w == delete_caption)
            sui::set_font(w, D.font_button);
    if (D.connect_version) sui::set_font(D.connect_version, D.font_button);
    // ---- lobby page ----
    auto& L = D.page_lobby;
    if (Win panel = sui::image_window(root, sui::fnv("lineages_lobby_panels"), sui::fnv("sporecoop"), 34, 156))
        L.push_back(panel);
    D.next_seats_edit = add_edit(L,"nextseats",{40,492,82,524},"",1);
    D.queue_seats = add_button(L,"queueseats",{88,488,274,528},T("Next Seats","Места на этап"),[] {
        auto value = sui::edit_text(D.next_seats_edit);
        if (value.size() != 1 || value[0] < '1' || value[0] > '4') {
            set_status(T("Enter 1 to 4 seats.","Укажите от 1 до 4 мест."),kRed,8);
            return;
        }
        std::string message;
        bool ok = A.queue_seats(value[0] - '0',message);
        set_status(message,ok ? kGreen : kRed,10);
        g_dirty = true;
    });
    D.session = add_label(L, {40, 103, 530, 129}, "", kWhite);
    D.invite_btn = add_button(L, "copyinvite", {475, 538, 720, 578}, T("Copy Host Address", "Копировать адрес"), [] {
        const auto addresses = A.invite ? A.invite() : std::string{};
        if (addresses.empty()) {
            set_status(T("No local address available", "Нет доступного адреса"), kYellow, 5);
            return;
        }
        const std::string invite = "Friends connect to: " + addresses;
        bool copied = false;
        if (OpenClipboard((HWND)H->game_window())) {
            if (HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, invite.size() + 1)) {
                if (void* bytes = GlobalLock(data)) {
                    memcpy(bytes, invite.c_str(), invite.size() + 1);
                    GlobalUnlock(data);
                    copied = EmptyClipboard() && SetClipboardData(CF_TEXT, data);
                }
                if (!copied) GlobalFree(data);
            }
            CloseClipboard();
        }
        set_status(copied ? T("Host address copied", "Адрес скопирован") : T("Could not copy host address", "Не удалось скопировать адрес"),
                   copied ? kGreen : kYellow, 5);
    });
    add_label(L, {40, 131, 380, 156}, T("Players", "Игроки"), kGold);
    add_label(L, {416, 131, 720, 156}, T("World & Session", "Мир и сессия"), kGold);
    D.rules = add_label(L, {416, 165, 710, 188}, "", kDim);
    D.transfer_mode = add_label(L, {416, 190, 710, 213}, "", kDim);
    D.preview_status = add_label(L, {40, 529, 438, 548}, "", kDim);
    for (int i = 0; i < 4; i++) {
        float y = 163.f + i * 30;
        D.row_name[i] = add_label(L, {45, y, 185, y + 26}, "", kWhite);
        D.row_stage[i] = add_label(L, {187, y, 320, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {322, y, 374, y + 26}, "", kDim);
        D.row_ready[i] = add_label(L, {375, y, 390, y + 26}, "", kDim);
    }
    D.world = add_label(L, {40, 292, 720, 316}, "", kYellow);
    add_label(L, {40, 316, 720, 338}, T("Session Chat", "Чат сессии"), kGold);
    for (int i = 0; i < 4; i++)
        D.chat_lines[i] = add_label(L, {40, 338.f + i * 17, 720, 355.f + i * 17}, "", kWhite);
    std::vector<Win> arrows; // shown by refresh_lobby only when there is more to see
    D.chat_up = add_button(arrows, "chatup", {722, 337, 756, 367}, "^", [] { g_chat_scroll++; g_dirty = true; });
    D.chat_down = add_button(arrows, "chatdown", {722, 376, 756, 406}, "v", [] { g_chat_scroll--; g_dirty = true; });
    // the own creation of this stage: "My creature: Kirgle  < >"
    D.creation = add_label(L, {40, 408, 560, 434}, "", kGold);
    D.creation_prev = add_button(L, "cprev", {570, 404, 640, 434}, "<", [] {
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle)
            A.civ_role_cycle(-1);
        else if (A.creation_cycle)
            A.creation_cycle(-1);
        g_dirty = true;
    });
    D.jump_btn = add_button(L, "jump", {400, 404, 560, 434}, "", [] {
        if (A.jump_do)
            A.jump_do();
        g_dirty = true;
    });
    D.give_btn = add_button(L, "give", {570, 404, 720, 434}, "", [] {
        if (A.give_do)
            A.give_do();
        g_dirty = true;
    });
    // 3 or 4 players: ">" picks the next other player as the recipient (BUG-0075)
    D.give_next = add_button(L, "givenext", {520, 404, 560, 434}, ">", [] {
        if (A.give_cycle)
            A.give_cycle();
        g_dirty = true;
    });
    D.creation_next = add_button(L, "cnext", {650, 404, 720, 434}, ">", [] {
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle)
            A.civ_role_cycle(1);
        else if (A.creation_cycle)
            A.creation_cycle(1);
        g_dirty = true;
    });
    D.space_btn = add_button(L, "space", {280, 488, 480, 528}, T("Space >", "Космос >"), [] {
        g_space_page = true;
        g_dirty = true;
    });
    D.chat_edit = add_edit(L, "chatedit", {40, 438, 560, 470}, "", 120);
    D.send_btn = add_button(L, "send", {570, 434, 720, 474}, T("Send", "Отправить"), do_send_chat);
    D.ready_btn = add_button(L, "ready", {445, 249, 706, 285}, "", [] {
        A.set_ready(!A.ready());
        g_dirty = true;
    });
    D.share_btn = add_button(L, "share", {445, 212, 706, 248}, T("Send World", "Отправить мир"), [] {
        int why = share_blocked();
        if (why) { // BUG-0597: say why nothing is sent (the button is greyed out, a locked click still comes here)
            set_status(why == 1 ? T("Start or load a game first: friends who join get it by themselves",
                                    "Сначала начните игру: вошедшие друзья получат её сами")
                                : T("Nobody has joined yet: friends who join get your world by themselves",
                                    "Пока никто не вошёл: друзья получат ваш мир сами"),
                       kYellow, 8);
            return;
        }
        A.share_world();
        set_status(T("Sending the world to everyone...", "Мир отправляется всем..."), kYellow, g_status_fix ? 5 : 0);
    });
    D.leave_btn = add_button(L, "leave", {500, 488, 720, 528}, "", [] {
        A.leave();
        set_status(T("Left the session", "Вы вышли из игры"), kYellow, g_status_fix ? 5 : 0);
    });
    D.leave_caption = shade_danger_button(L, D.leave_btn, "");
    // ---- Space page ----
    auto& S = D.page_space;
    D.space_title = add_label(S, {40, 104, 530, 130}, T("Your galaxy together", "Галактика вместе"), kGold);
    for (int i = 0; i < kSpaceRows; i++) {
        float y = 136.f + i * 29;
        char nm[16];
        D.space_text[i] = add_label(S, {40, y + 2, 545, y + 27}, "", kWhite);
        snprintf(nm, sizeof nm, "sbtn%d", i);
        D.space_button[i] = add_button(S, nm, {555, y, 720, y + 27}, "", [i] {
            if (!D.space_action[i])
                return;
            // game calls and UI changes of a feature: outside the frame
            H->run_outside_frame(
                [](void* p) {
                    auto* f = (std::function<void()>*)p;
                    (*f)();
                    delete f;
                },
                new std::function<void()>(D.space_action[i]));
            g_dirty = true;
        });
    }
    D.space_back = add_button(S, "sback", {40, 488, 240, 528}, T("< Lobby", "< Лобби"), [] {
        g_space_page = false;
        g_dirty = true;
    });
    // more rows than fit: pages
    D.space_prev = add_button(S, "sprev", {500, 488, 570, 528}, "<", [] {
        g_space_first = std::max(0, g_space_first - kSpaceRows);
        g_dirty = true;
    });
    D.space_pageno = add_label(S, {575, 494, 645, 522}, "", kDim, sui::kAlignCenter);
    D.space_next = add_button(S, "snext", {650, 488, 720, 528}, ">", [] {
        g_space_first += kSpaceRows;
        g_dirty = true;
    });
    // Enter in the chat field sends the message
    if (D.chat_edit)
        sui::add_handler(D.chat_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_send_chat();
                return true;
            }
            return false;
        });
    if (D.addr_edit)
        sui::add_handler(D.addr_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_join(sui::edit_text(D.addr_edit));
                return true;
            }
            return false;
        });
    for (Win w : common)
        (void)w;
    sui::set_visible(root, false);
    g_cache[root] = Cache{"", 0, 0};
    H->log(M, SK_INFO, "ui: co-op dialog ready");
}

static void refresh_connect()
{
    const bool paced = A.paced_transfer && A.paced_transfer();
    for (unsigned i = 0; i < 2; ++i)
        if (D.transfer_btn[i]) sui::set_shade_color(D.transfer_btn[i], (i == 0) == paced ? 0xFFFFFFFF : 0xFF949CA8);
    for (unsigned i = 0; i < 4; ++i)
        if (D.seat_btn[i]) sui::set_shade_color(D.seat_btn[i], i + 1 == D.seats ? 0xFFFFFFFF : 0xFF949CA8);
    const std::pair<const char*, Win> privacy[] = {{"hideaddress", D.addr_edit}, {"hidejoinport", D.join_port},
        {"hidejoinpassword", D.join_password}, {"hidehostport", D.port_edit}, {"hidehostpassword", D.host_password}};
    for (const auto& [button, field] : privacy) {
        const bool hidden = D.hidden_fields[field];
        show_eye(button, hidden);
    }
    show_eye("hidehostaddresses", !D.show_host_addresses);
    founders::CampaignRules rules{(uint8_t)D.seats,
        (uint16_t)std::clamp(atoi(sui::edit_text(D.scaling_edit).c_str()), 0, 65535)};
    auto setting = [](const char* key, uint16_t value) {
        return (uint16_t)std::clamp(H->config_int(M, "campaign", key, value), 0, 65535);
    };
    rules.extra_health_percent = setting("enemy_health_per_extra_founder", rules.extra_health_percent);
    rules.extra_damage_percent = setting("enemy_damage_per_extra_founder", rules.extra_damage_percent);
    rules.extra_hunger_basis_points = setting("hunger_basis_points_per_extra_founder", rules.extra_hunger_basis_points);
    rules.extra_takeover_percent = setting("takeover_per_extra_founder", rules.extra_takeover_percent);
    auto bonus = [](uint32_t percent) {
        const auto increase = percent - 100u;
        return (increase ? "+" : "") + std::to_string(increase) + "%";
    };
    text(D.difficulty_value[0], bonus(rules.workload_percent()));
    text(D.difficulty_value[1], bonus(rules.enemy_health_percent()) + " / " + bonus(rules.enemy_damage_percent()));
    char hunger[80];
    const double hunger_bonus = (D.seats - 1) * rules.extra_hunger_basis_points / 100.0;
    snprintf(hunger, sizeof hunger, "%s%.2f%%", hunger_bonus ? "+" : "", hunger_bonus);
    text(D.difficulty_value[2], hunger);
    text(D.difficulty_value[3], bonus(rules.takeover_percent()));
    char interval[80];
    const double seconds_less = (founders::CampaignRules::native_military_interval_ms - rules.civ_military_interval_ms()) / 1000.0;
    snprintf(interval, sizeof interval, "%s%.2f s", seconds_less ? "-" : "", seconds_less);
    text(D.difficulty_value[4], interval);
    bool scanning = A.role() == 0;
    auto discovered = A.lan_games ? A.lan_games() : std::vector<LanGame>{};
    std::vector<LanGame> games;
    for (const auto& saved : D.saved_servers) {
        auto found = std::find_if(discovered.begin(), discovered.end(),
                                  [&](const LanGame& game) { return game.address == saved.address; });
        if (found == discovered.end()) {
            games.push_back({saved.name.empty() ? "Saved Server" : saved.name, saved.address, "Offline", 0, 0});
        } else {
            games.push_back(std::move(*found));
            discovered.erase(found);
        }
    }
    games.insert(games.end(), discovered.begin(), discovered.end());
    const auto address = connection_address();
    const auto selected = std::find_if(games.begin(), games.end(), [&](const LanGame& game) { return game.address == address; });
    std::string details = "Server Status: enter an address";
    if (selected != games.end()) {
        if (!selected->max_players) details = "Saved server: offline or undiscovered";
        else if (selected->scaling < 0) details = selected->info;
        else details = std::to_string(selected->scaling) + "% scaling  |  " +
                       (selected->settings & 1 ? "Optimal" : "Fast") +
                       (selected->settings & 2 ? "  |  Password" : "  |  Open");
    } else if (!address.empty()) details = "Server Status: direct connection";
    text(D.selected_info, details);
    g_lan_max = g_lan_pages_fix ? std::max(0, (int)games.size() - kLanRows) : 0;
    g_lan_first = std::clamp(g_lan_first, 0, g_lan_max);
    text(D.lan_hdr, T("Searching your network...", "Поиск игр в вашей сети..."));
    show(D.lan_hdr, games.empty() && scanning);
    show(D.lan_track, scanning);
    show(D.lan_thumb, scanning && g_lan_max > 0);
    if (D.lan_thumb && g_lan_max > 0) {
        const float y = 178.f + 122.f * g_lan_first / g_lan_max;
        sui::set_area(D.lan_thumb, {342, y, 353, y + 28});
    }
    for (int i = 0; i < kLanRows; ++i) {
        int k = g_lan_first + i;
        bool have = k < (int)games.size() && (g_lan_pages_fix || i < 3);
        if (have) {
            const auto& g = games[k];
            auto name = g.name;
            if (name.size() > 22) {
                size_t end = 22;
                while (end > 0 && ((unsigned char)name[end] & 0xC0) == 0x80) --end;
                name.resize(end);
                name += "...";
            }
            if (g.max_players)
                text(D.lan_rows[i], name + "  [LAN]  " + std::to_string(g.players) + "/" + std::to_string(g.max_players));
            else
                text(D.lan_rows[i], name + "  -  Offline");
            if (D.lan_rows[i])
                sui::set_shade_color(D.lan_rows[i], g.address == address ? 0xFFFFFFFF : 0xFFADB6C2);
            D.lan_addr[i] = g.address;
        } else
            D.lan_addr[i].clear();
        show(D.lan_rows[i], have && scanning);
    }
    std::string hint;
    if (A.local_addresses) {
        auto ips = A.local_addresses();
        std::string port = sui::edit_text(D.port_edit);
        for (size_t i = 0; i < ips.size() && i < 3; i++)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
    text(D.host_hint, !D.show_host_addresses ? "Host addresses hidden" : hint.empty() ? "No host addresses" : hint);
    std::string up_line, up_btn;
    bool up = A.update && A.update(up_line, up_btn);
    // the session status wins; otherwise the update line (a new SporeCoop release)
    text(D.status, g_status.empty() && up ? up_line : g_status);
    color(D.status, g_status.empty() && up ? kGreen : g_status_color);
    // the Update button belongs to the update line, not to a session message (BUG-0104)
    show(D.update_btn, up && !up_btn.empty() && (!g_status_fix || g_status.empty()));
    text(D.update_btn, up_btn);
}

static void refresh_lobby()
{
    text(D.session, A.session_line());
    auto rows = A.players();
    const bool host = A.role() == 1;
    const bool vote = A.vote_active && A.vote_active();
    const auto rules = world::rules_for_current();
    const unsigned seats = host && A.seat_limit ? A.seat_limit() : rules.seats;
    show(D.invite_btn, host);
    text(D.rules, A.mode && A.mode() != modes::kMenu
         ? std::string(T("Difficulty Scaling: ", "Сложность: ")) + std::to_string(rules.difficulty_percent) + "%"
         : T("Load a campaign to see its rules", "Загрузите кампанию для просмотра правил"));
    show(D.transfer_mode, host);
    text(D.transfer_mode, std::string(T("World Transfer: ", "Передача мира: ")) +
         (A.paced_transfer && A.paced_transfer() ? "Optimal" : "Fast"));
    std::string preview, update_button;
    text(D.preview_status, A.update && A.update(preview, update_button) ? preview : "");
    for (int i = 0; i < 4; i++) {
        bool have = i < (int)rows.size();
        const bool reserved = !have && static_cast<unsigned>(i) < seats;
        show(D.row_name[i], have || reserved);
        show(D.row_stage[i], have || reserved);
        show(D.row_ping[i], have);
        show(D.row_ready[i], have && vote);
        if (!have) {
            if (reserved) {
                text(D.row_name[i], T("Reserved Seat", "Место в резерве"));
                color(D.row_name[i], kDim);
                text(D.row_stage[i], T("Open", "Свободно"));
            }
            continue;
        }
        const auto& p = rows[i];
        std::string n = p.name;
        if (p.host && n.size() <= 12)
            n += T(" (host)", " (хост)");
        text(D.row_name[i], n);
        color(D.row_name[i], p.me ? kGold : kWhite);
        text(D.row_stage[i], p.stage == "Galaxy menu" ? T("Galaxy", "Галактика") : p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(p.ping_ms) + T("ms", "мс") : "?"));
        text(D.row_ready[i], p.ready ? "+" : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    std::string wl = A.world_line ? A.world_line() : "";
    int share_why = share_blocked();
    if (wl.empty() && g_status.empty() && share_why) // BUG-0597: a host alone can play now
        wl = share_why == 1 && rows.size() <= 1
                 ? T("Start or load a game now: friends can join any time",
                     "Начните или загрузите игру: друзья войдут когда угодно")
             : share_why == 2 ? T("Nobody has joined yet: friends who join get your world by themselves",
                                  "Пока никто не вошёл: друзья получат ваш мир сами")
                              : "";
    text(D.world, wl.empty() ? g_status : wl);
    // the chat history (50 lines kept): long lines wrapped, scrolled with the arrows (BUG-0129)
    std::vector<std::string> chat;
    for (auto& l : A.chat ? A.chat() : std::vector<std::string>{})
        for (auto& w : notify_text::wrap(l, kChatCols))
            chat.push_back(w);
    g_chat_scroll = std::max(0, std::min(g_chat_scroll, (int)chat.size() - 4));
    size_t first = chat.size() > 4 ? chat.size() - 4 - (size_t)g_chat_scroll : 0;
    for (int i = 0; i < 4; i++)
        text(D.chat_lines[i], first + i < chat.size() ? chat[first + i] : "");
    show(D.chat_up, first > 0);
    show(D.chat_down, g_chat_scroll > 0);
    int ck = 0, cown = 0;
    bool hint = false; // the creation row says how to get a choice: it may take the arrows' room
    std::string cname, cl;
    if (A.creation && A.creation(ck, cname, cown)) {
        cl = ck == 1 ? T("My cell: ", "Моя клетка: ") : ck == 2 ? T("My ship: ", "Мой корабль: ") : T("My creature: ", "Моё существо: ");
        bool me_host = A.role() == 1;
        if (!cname.empty())
            cl += cname;
        else if (!g_creation_hint_fix)
            cl += T("the world's own", "из мира хоста");
        else // BUG-0598: say whose it is
            cl += ck == 2 ? T("your empire's ship", "корабль вашей империи")
                  : me_host ? T("your world's species", "вид вашего мира") : T("the host's species", "вид хоста");
        if (ck != 2 && cown > 0)
            cl += std::string(T("  (in My Creations: ", "  (своих: ")) + std::to_string(cown) + ")";
        else if (g_creation_hint_fix && ck != 2 && !me_host) { // no arrows: how to get a choice
            cl += T(" (make one in an editor to pick it)", " (создайте своё в редакторе)");
            hint = true;
        }
    }
    std::string gwhat, gto;
    int gn = A.give ? A.give(gwhat, gto) : 0;
    bool space = gwhat == "spice";
    bool giving = A.give && !gto.empty() && (cl.empty() || space);
    if (space) // the ship can't be changed in flight: the row is for the cargo
        cl = gn ? std::string(T("Spice in cargo: ", "Специи в трюме: ")) + std::to_string(gn)
                : T("No spice in cargo to give", "В трюме нет специи");
    else if (giving) {
        const char* w = gwhat == "city" ? T("the city whose menu you opened", "город, меню которого открыто")
                        : gwhat == "vehicle" ? T("selected vehicles: ", "выбрано машин: ")
                                             : T("selected tribe members: ", "выбрано жителей: ");
        cl = gn ? std::string(w) + (gwhat == "city" ? "" : std::to_string(gn))
                : T("Select your units to give them to a partner", "Выделите свои юниты, чтобы отдать напарнику");
    }
    bool roles = giving && gn == 0 && A.civ_role && A.mode && A.mode() == 0x1654C04; // civ, nothing selected
    if (roles) {
        int r = A.civ_role();
        const char* rn[4] = {T("none - pick one", "нет - выберите"), T("military (stronger vehicles)", "военная (сильнее машины)"),
                             T("economic (trade earns more)", "торговая (выгоднее торговля)"),
                             T("religious (faster conversion)", "религиозная (быстрее обращение)")};
        cl = std::string(T("My role: ", "Моя роль: ")) + rn[r & 3];
    }
    if (g_creation_hint_fix && D.creation) { // the row is wider while it has no arrows and no buttons
        auto& c = g_cache[D.creation];
        float x2 = hint && !giving && !roles ? 720.f : 560.f;
        if (c.box.x2 != x2 && c.left) {
            c.box.x2 = x2;
            fit(D.creation, c);
        }
    }
    text(D.creation, cl);
    show(D.give_btn, giving && gn > 0);
    show(D.give_next, giving && gn > 0 && !space && A.give_choices && A.give_choices() > 1);
    text(D.give_btn, space ? std::string(T("Give 10 To ", "Дать 10: ")) + gto : std::string(T("Give To ", "Отдать ")) + gto);
    std::string jto;
    bool jump = A.jump && A.jump(jto);
    show(D.jump_btn, jump);
    text(D.jump_btn, std::string(T("Jump To ", "К звезде: ")) + jto);
    bool can = (!cl.empty() && !giving && ck != 2 && cown > 0) || roles;
    show(D.creation_prev, can);
    show(D.creation_next, can);
    show(D.ready_btn, vote);
    text(D.ready_btn, A.ready() ? T("Not Ready", "Не готов") : T("I'm Ready", "Я готов"));
    show(D.share_btn, host);
    enable(D.share_btn, !share_why || !g_share_explain_fix);
    text(D.share_btn, A.vote_active && A.vote_active() ? T("Start Now", "Начать сейчас") : T("Send World", "Отправить мир"));
    const auto leave_title = host ? T("Stop Hosting", "Закрыть игру") : T("Leave", "Выйти");
    text(D.leave_btn, leave_title);
    text(D.leave_caption, leave_title);
}

static void refresh_space()
{
    const bool villages = A.mode && A.mode() == modes::kTribe;
    const bool nations = A.mode && A.mode() == modes::kCiv && civ::offline_control_available();
    std::vector<spaceext::Row> rows;
    if (villages) {
        auto action = [](const char* command) {
            open(false);
            notify::show(tribe::debug({command}), "info");
        };
        rows.push_back({"Select your villagers before choosing an action.", "", nullptr});
        if (A.role() == 2) {
            rows.push_back({"Choose a destination on the ground.", "Move", [action] { action("march"); }});
            rows.push_back({"Gather fruit near your village.", "Gather", [action] { action("gather"); }});
        }
        rows.push_back({"Help your partner establish a new village.", "Rebuild", [action] { action("rebuild"); }});
        if (A.role() == 2) {
            rows.push_back({"Attack an enemy villager with your selection.", "Attack", [action] { action("attack"); }});
            rows.push_back({"Select all living members of your village.", "Select", [action] { action("select_village"); }});
            rows.push_back({"Attack an enemy hut or tool building.", "Raid", [action] { action("raid"); }});
            rows.push_back({tribe::village_birth_line(), "Add baby", [action] { action("baby"); }});
            rows.push_back({"Collect tools from a rack in your village.", "Equip", [action] { action("equip"); }});
        }
        if (A.role() == 2)
            rows.push_back({"Heal an injured friendly villager with healing tools.", "Heal", [action] { action("heal"); }});
        else rows.push_back({"Select a living helper at the new village site.", "", nullptr});
        if (A.role() == 2)
            rows.push_back({"Eat food stored at your village.", "Eat", [action] { action("eat"); }});
        else rows.push_back({"Stay at least 60 metres from existing huts.", "", nullptr});
        if (A.role() == 2)
            rows.push_back({"Give food to another tribe to make peace.", "Gift food", [action] { action("gift_food"); }});
        else rows.push_back({"Your partner keeps their original species.", "", nullptr});
        rows.push_back({tribe::village_food_line(), "", nullptr});
        if (A.role() == 2) for (int kind = 1; kind <= 9; ++kind)
            rows.push_back({tribe::village_tool_line(kind), "Build", [kind] {
                open(false);
                notify::show(tribe::debug({"build", std::to_string(kind)}), "info");
            }});
        if (A.role() == 2) {
            rows.push_back({"Perform for an NPC tribe with your chief and musicians.", "Perform", [action] { action("perform"); }});
            rows.push_back({tribe::performance_line(), "", nullptr});
            const char* instruments[] = {"Horns [1]", "Maracas [2]", "Didgeridoos [3]"};
            std::string request = tribe::performance_line();
            if (request.empty()) request = "Answer the audience with selected musicians.";
            for (int tool = 4; tool <= 6; ++tool)
                rows.push_back({request, instruments[tool - 4], [tool] {
                    notify::show(tribe::debug({"instrument", std::to_string(tool)}), "info");
                }});
        }
    } else if (nations) {
        const bool enabled = civ::offline_control_enabled();
        rows.push_back({"Command the partner's cities and vehicles while they are offline.",
                        enabled ? "Enabled" : "Disabled", [enabled] {
                            notify::show(civ::debug({"offline_control", enabled ? "off" : "on"}), "info");
                            g_dirty = true;
                        }});
    } else rows = spaceext::rows_all();
    text(D.space_title, nations ? "Civilization settings" : villages ? "Your two villages" : T("Your galaxy together", "Галактика вместе"));
    int n = (int)rows.size(), pages = std::max(1, (n + kSpaceRows - 1) / kSpaceRows);
    if (g_space_first >= n)
        g_space_first = (pages - 1) * kSpaceRows;
    for (int i = 0; i < kSpaceRows; i++) {
        int k = g_space_first + i;
        bool have = k < n;
        show(D.space_text[i], have);
        D.space_action[i] = have ? rows[k].on_click : nullptr;
        show(D.space_button[i], have && !rows[k].button.empty() && rows[k].on_click);
        if (!have)
            continue;
        text(D.space_text[i], rows[k].text);
        color(D.space_text[i], rows[k].color ? rows[k].color : kWhite);
        text(D.space_button[i], rows[k].button);
    }
    if (rows.empty())
        text(D.space_text[0], T("Nothing here yet", "Пока ничего"));
    show(D.space_text[0], true);
    show(D.space_prev, pages > 1);
    show(D.space_next, pages > 1);
    show(D.space_pageno, pages > 1);
    text(D.space_pageno, std::to_string(g_space_first / kSpaceRows + 1) + "/" + std::to_string(pages));
}

static void refresh()
{
    text(D.report_btn, T("Report Issue", "Сообщить об ошибке"));
    text(D.report_caption, T("Report Issue", "Сообщить об ошибке"));
    g_pending = false;
    if (!g_ok)
        return;
    ensure_menu_button();
    if (g_want_open && !D.root)
        build_dialog();
    if (!D.root)
        return;
    if (g_want_open && g_raise) {
        // on top of every UI layer (in game the HUD layers cover the menu layer and take
        // the mouse): a direct child of the main window, brought to the front
        g_raise = false;
        Win main = sui::main_window();
        if (Win p = sui::parent(D.root))
            if (p != main)
                sui::remove_child(p, D.root);
        if (sui::parent(D.root) != main)
            sui::add_child(main, D.root);
        sui::bring_to_front(main, D.root);
        sui::set_flag(D.root, sui::kFlagAlwaysInFront, true);
    }
    if (g_want_open && g_recenter_fix)
        place_window(D.root);
    show(D.root, g_want_open);
    if (!g_want_open) {
        if (A.lan_scan)
            A.lan_scan(false);
        return;
    }
    bool lobby = A.role() != 0;
    bool space = lobby && A.mode && A.mode() == modes::kSpace;
    bool villages = lobby && A.mode && A.mode() == modes::kTribe && tribe::village_orders();
    bool nations = lobby && civ::offline_control_available();
    if (!space && !villages && !nations)
        g_space_page = false;
    for (Win w : D.page_connect)
        show(w, !lobby);
    for (Win w : D.page_lobby)
        show(w, lobby && !g_space_page);
    for (Win w : D.page_space)
        show(w, lobby && g_space_page);
    bool seat_controls = lobby && A.role() == 1 && !g_space_page && A.mode &&
        (space || A.mode() == modes::kCell || A.mode() == modes::kCreature ||
         A.mode() == modes::kTribe || A.mode() == modes::kCiv);
    show(D.next_seats_edit,seat_controls);
    show(D.queue_seats,seat_controls);
    text(D.queue_seats,space ? T("Reload Seats","Места на загрузку") : T("Next Seats","Места на этап"));
    show(D.space_btn, (space || villages || nations) && !g_space_page);
    text(D.space_btn, nations ? "Nations >" : villages ? "Village >" : T("Space >", "Космос >"));
    if (!lobby || g_space_page) {
        show(D.chat_up, false);
        show(D.chat_down, false);
    }
    text(D.subtitle, lobby && g_space_page ? (nations ? "NATIONS" : villages ? "VILLAGES" : T("SPACE", "КОСМОС"))
                     : lobby ? T("LOBBY", "ЛОББИ") : "");
    if (A.version_line) {
        auto version = A.version_line();
        if (!g_space_page) version = version.substr(0, version.find("  -  "));
        text(D.version, version);
        text(D.connect_version, version);
    }
    show(D.version, lobby);
    const Rect report_area = lobby ? Rect{545, 98, 720, 134} : Rect{545, 107, 720, 142};
    for (Win control : {D.report_btn, D.report_caption})
        if (control && sui::area(control).y1 != report_area.y1)
            sui::set_area(control, report_area);
    if (A.lan_scan)
        A.lan_scan(!lobby);
    if (lobby && g_space_page)
        refresh_space();
    else if (lobby)
        refresh_lobby();
    else
        refresh_connect();
}

static Win g_graphics_fps;
static bool g_graphics_typing;

static bool graphics_input(unsigned message, uintptr_t value, int x, int y)
{
    if (!g_graphics_fps) return false;
    if (message == WM_KEYDOWN && (value == VK_ESCAPE || value == VK_RETURN)) {
        g_graphics_typing = false;
        return false;
    }
    const bool backspace = value == VK_BACK && (message == WM_KEYDOWN || message == WM_KEYUP);
    if (message != WM_LBUTTONDOWN && !(g_graphics_typing && (message == WM_CHAR || backspace)))
        return false;
    if (sui::find(sui::main_window(), 0xC00F0111) != g_graphics_fps) {
        g_graphics_typing = false;
        return false;
    }
    if (message == WM_LBUTTONDOWN) {
        const auto area = sui::screen_rect(g_graphics_fps);
        g_graphics_typing = x >= area.x1 && x < area.x2 && y >= area.y1 && y < area.y2;
        return false;
    }
    if (value == VK_ESCAPE || value == VK_RETURN)
        return false;
    // Spore handles Backspace as menu navigation on keydown, before WM_CHAR.
    if (message == WM_KEYUP || (message == WM_CHAR && value == VK_BACK))
        return true;
    auto text = sui::edit_text(g_graphics_fps);
    if (backspace && !text.empty())
        text.pop_back();
    else if (message == WM_CHAR && value >= '0' && value <= '9' && text.size() < 3)
        text += (char)value;
    sui::set_edit_text(g_graphics_fps, text);
    return true;
}

static void refresh_graphics_options()
{
    constexpr uint32_t kSettings = 0x043DE590;
    constexpr uint32_t kGraphicPanel = 0x0437A968;
    constexpr uint32_t kAccept = 0x043B7A18;
    constexpr uint32_t kDefaults = 0x05CACA50;
    constexpr uint32_t kControls[] = {0xC00F0110, 0xC00F0111, 0xC00F0112, 0xC00F0113};
    static Win last_open = nullptr;
    Win settings = sui::find(sui::main_window(), kSettings);
    Win panel = settings ? sui::find(settings, kGraphicPanel) : nullptr;
    auto visible = [](Win w) {
        if (!w) return false;
        for (; w; w = sui::parent(w))
            if (!sui::visible(w)) return false;
        return true;
    };
    if (!visible(settings)) {
        last_open = nullptr;
        g_graphics_fps = nullptr;
        g_graphics_typing = false;
        return;
    }
    Win fps = sui::find(settings, kControls[1]);
    Win borderless = sui::find(settings, kControls[2]);
    const auto file = slotfs::widen(H->mods_dir()) + L"\\" + lineages_graphics::file_name;
    if (!fps && panel && visible(panel)) {
        Win defaults = sui::find(panel, kDefaults);
        Win accept = sui::find(sui::parent(settings), kAccept);
        if (!defaults || !accept) return;
        const uint32_t font = sui::font(sui::find(panel, 0x0695B438));
        if (!font) return;
        Win label = sui::label(settings, {77, 395, 211, 418}, "Frame Rate Cap", font, kWhite, sui::kAlignLeft);
        fps = sui::text_edit(settings, {209, 392, 261, 420}, "", font, 3);
        borderless = sui::button_like(settings, defaults, {76, 431, 263, 467}, "");
        Win hint = sui::label(settings, {77, 472, 268, 500}, "Borderless uses windowed mode.", font, kDim,
                              sui::kAlignLeft);
        Win windows[] = {label, fps, borderless, hint};
        for (int i = 0; i < 4; ++i)
            if (windows[i]) sui::set_ids(windows[i], kControls[i], 0);
        if (!fps || !borderless) return;
        sui::add_handler(fps, [](Win, const sui::Msg& msg) {
            return msg.type == sui::kMsgTextChanged;
        });
        sui::add_handler(borderless, [](Win button, const sui::Msg& msg) {
            if (!sui::is_click(msg)) return false;
            sui::set_caption(button, sui::caption(button) == "Borderless: On" ? "Borderless: Off" : "Borderless: On");
            return true;
        });
        sui::add_handler(accept, [fps, borderless, file](Win, const sui::Msg& msg) {
            if (!sui::is_click(msg)) return false;
            const auto input = sui::edit_text(fps);
            char* end = nullptr;
            const long value = strtol(input.c_str(), &end, 10);
            if (!input.empty() && (*end || value < 1 || value > 360)) {
                MessageBoxW((HWND)H->game_window(), L"Enter a whole number from 1 to 360. Leave it blank for Spore's default.",
                            L"Frame Rate Cap", MB_OK | MB_ICONWARNING);
                return true;
            }
            const lineages_graphics::Settings value_to_save{(int)value, sui::caption(borderless) == "Borderless: On"};
            const auto previous = lineages_graphics::read(file);
            if (previous.fps == value_to_save.fps && previous.borderless == value_to_save.borderless)
                return false;
            if (!lineages_graphics::write(file, value_to_save)) {
                MessageBoxW((HWND)H->game_window(), L"Could not save the graphics settings.", L"Lineages", MB_OK | MB_ICONWARNING);
                return true;
            }
            H->run_outside_frame([](void* data) {
                auto* settings = static_cast<lineages_graphics::Settings*>(data);
                const auto cap = std::string("frame.target ") + (settings->fps ? std::to_string(settings->fps) : "auto");
                const int fps = H->command_exec(cap.c_str(), nullptr);
                const int window = H->command_exec(settings->borderless ? "window.borderless on" : "window.borderless off", nullptr);
                if (fps || window)
                    MessageBoxW((HWND)H->game_window(),
                        window ? L"Turn off Full Screen Mode to use borderless. Restart Spore if its settings prompt you."
                               : L"Restart Spore to apply the new frame rate cap.",
                        L"Lineages Graphics", MB_OK | MB_ICONINFORMATION);
                delete settings;
            }, new lineages_graphics::Settings(value_to_save));
            return false; // let the native confirmation save its own settings
        });
        last_open = nullptr;
    }
    if (!fps || !borderless) return;
    if (last_open != settings) {
        const auto saved = lineages_graphics::read(file);
        sui::set_edit_text(fps, saved.fps ? std::to_string(saved.fps) : "");
        sui::set_caption(borderless, saved.borderless ? "Borderless: On" : "Borderless: Off");
        last_open = settings;
    }
    for (uint32_t id : kControls)
        if (Win control = sui::find(settings, id))
            sui::set_visible(control, visible(panel));
    g_graphics_fps = visible(panel) ? fps : nullptr;
    if (!g_graphics_fps) g_graphics_typing = false;
    mouse::set_graphics_input(graphics_input);
}

static void refresh_task(void*)
{
    refresh();
    refresh_graphics_options();
}

void init(const SkHost* host, SkMod* mod, Api api)
{
    H = host;
    M = mod;
    A = std::move(api);
    g_ok = sui::init(H, M);
    std::string lang = load_str("ui", "lang", "auto");
    g_ru = lang == "ru" || (lang == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
    if (g_ok)
        sui::register_commands();
}

void open(bool on)
{
    if (g_want_open == on)
        return;
    g_want_open = on;
    if (on) {
        g_raise = true;
        if (D.root)
            sui::set_edit_text(D.name_edit, A.name()); // fresh values when the dialog appears
    }
    g_dirty = true;
}

bool is_open() { return g_want_open; }
int input_lock_now() { return input_lock(); }

void tick()
{
    if (!g_ok)
        return;
    // F9: show/hide the co-op window anywhere in the game
    bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && !g_f9_down)
        open(!g_want_open);
    g_f9_down = f9;
    // The galaxy menu's intro cinematic (~10 s, starting ~16 s after the menu came up unless the
    // start-up video was skipped with Escape) holds the game's input lock: the game drops every click
    // on the menu, so a click on our "Co-op" item did nothing (seen in coop_ui.py on GOG 2024, where
    // the menu is reached faster; docs/re/space.md 7.9). The button press is seen here instead: on our
    // item the mod presses Escape for the player (the game's own skip of the intro, the menu's key
    // handler) and opens the window once the menu takes clicks again. (Ending the intro with EndCinematic in the
    // menu, as before a load, left the menu broken: an access violation in UTFWin::Window::SetFlag
    // from App::Canvas::Tick 10 s later, or a game that stopped answering.)
    if (g_menu_drop_fix && g_menu_btn && !in_menu())
        g_menu_btn = g_menu_parent = nullptr;
    // BUG-0145: the primary click from the window's messages (mouse.h), not the physical left button
    HWND gw = (HWND)H->game_window();
    mouse::install(gw);
    mouse::set_list_input(server_list_input);
    uint32_t nclicks = mouse::clicks();
    bool lb = mouse::g_fix ? nclicks != g_lb_seen : (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_lb_seen = nclicks;
    locked_click_tick(mouse::g_fix && lb);
    if (mouse::g_fix)
        g_lb_down = false;
    // menu_intro_playing() (mode-checked) before any call on the item
    if (lb && !g_lb_down && gw && g_menu_btn && !g_want_open && !g_skip_since &&
        (!g_menu_drop_fix || world::menu_intro_playing()) && sui::visible(g_menu_btn) &&
        world::menu_intro_playing()) {
        POINT c{};
        int cx = 0, cy = 0;
        Rect r = sui::screen_rect(g_menu_btn);
        mouse::last_click(cx, cy);
        bool at = mouse::g_fix ? (c = POINT{cx, cy}, true) : GetCursorPos(&c) && ScreenToClient(gw, &c);
        if (at && c.x >= r.x1 && c.x < r.x2 && c.y >= r.y1 && c.y < r.y2) {
            H->log(M, SK_INFO, "ui: Co-op clicked during the menu intro: Escape skips it, then the window opens");
            LPARAM lp = 1 | (MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC) << 16);
            PostMessageW(gw, WM_KEYDOWN, VK_ESCAPE, lp);
            PostMessageW(gw, WM_KEYUP, VK_ESCAPE, lp | 0xC0000000);
            g_skip_since = H->time_seconds();
        }
    }
    g_lb_down = lb;
    // the game also drops clicks for ~2-3 s after the intro is over (a click 1 s or 2 s after its end
    // did nothing, 3 s after it worked; GOG 2024 lane 9): the window opens when it takes clicks
    if (g_skip_since) {
        double t = H->time_seconds();
        if (world::menu_intro_playing())
            g_intro_over = 0;
        else if (!g_intro_over)
            g_intro_over = t;
        if (g_intro_over && t - g_intro_over >= 3.0) {
            g_skip_since = g_intro_over = 0;
            if (A.mode && A.mode() == modes::kMenu)
                open(true);
        } else if (t - g_skip_since > 20) {
            g_skip_since = g_intro_over = 0; // (the intro ends on its own after ~10 s; a load may have left the menu)
        }
    }
    double now = H->time_seconds();
    report_tick(now);
    if (g_status_until > 0 && now >= g_status_until) { // a message of a finished action (BUG-0104)
        g_status.clear();
        g_status_until = 0;
        g_dirty = true;
    }
    double period = g_want_open ? 0.25 : 1.0;
    if (!g_pending && (g_dirty || now - g_last_refresh >= period)) {
        g_pending = true;
        g_dirty = false;
        g_last_refresh = now;
        H->run_outside_frame(refresh_task, nullptr);
    }
}

void shutdown()
{
    mouse::set_graphics_input(nullptr);
    mouse::set_list_input(nullptr);
    if (D.root)
        sui::set_visible(D.root, false);
    if (g_menu_btn && (!g_menu_drop_fix || in_menu()))
        sui::set_visible(g_menu_btn, false);
}

std::string debug_state()
{
    std::string s = "open=" + std::to_string(g_want_open) + " page=" +
                    (A.role() ? (g_space_page ? "space" : "lobby") : "connect") +
                    " menu_button=" + (g_menu_btn ? "yes" : "no") + " in_menu=" + (in_menu() ? "1" : "0") +
                    " input_lock=" + std::to_string(input_lock()) + " locked_clicks=" + std::to_string(g_locked_clicks) +
                    " locked_native=" + std::to_string(g_locked_native) +
                    " share_enabled=" + (D.share_btn && (sui::flags(D.share_btn) & sui::kFlagEnabled) ? "1" : "0") + "\n";
    char b[256];
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    snprintf(b, sizeof b, "screen %d %d\n", sw, sh);
    s += b;
    if (g_menu_btn && in_menu()) { // never a call on an item of a menu that is gone (BUG-0163)
        Rect r = sui::screen_rect(g_menu_btn);
        snprintf(b, sizeof b, "menu %.0f %.0f %.0f %.0f %d '%s'\n", r.x1, r.y1, r.x2, r.y2,
                 (int)sui::visible(g_menu_btn), sui::caption(g_menu_btn).c_str());
        s += b;
    }
    for (auto& [name, w] : g_named) {
        Rect r = sui::screen_rect(w);
        bool vis = sui::visible(w) && D.root && sui::visible(D.root);
        std::string cap = name == "hostpassword" || name == "joinpassword" || (D.hidden_fields.contains(w) && D.hidden_fields.at(w))
                              ? "<hidden>" : name == "name" || name == "address" || name == "port" ||
                                             name == "joinport" || name == "chatedit"
                              ? sui::edit_text(w) : sui::caption(w);
        snprintf(b, sizeof b, "%s %.0f %.0f %.0f %.0f %d '%s'\n", name.c_str(), r.x1, r.y1, r.x2, r.y2, (int)vis,
                 cap.c_str());
        s += b;
    }
    const size_t players = A.players ? A.players().size() : 0;
    for (size_t i = 0; i < std::min<size_t>(4, players); i++)
        if (D.row_name[i] && sui::visible(D.row_name[i]))
            s += "row " + std::to_string(i) + " '" + sui::caption(D.row_name[i]) + "' '" + sui::caption(D.row_stage[i]) +
                 "' '" + sui::caption(D.row_ping[i]) + "' '" + sui::caption(D.row_ready[i]) + "'\n";
    for (int i = 0; i < 5; i++)
        if (D.chat_lines[i] && !sui::caption(D.chat_lines[i]).empty())
            s += "chat '" + sui::caption(D.chat_lines[i]) + "'\n";
    if (D.session)
        s += "session '" + sui::caption(D.session) + "'\n";
    if (D.world)
        s += "world '" + sui::caption(D.world) + "'\n";
    if (D.creation)
        s += "creation '" + sui::caption(D.creation) + "'\n";
    if (D.lan_hdr)
        s += "lan '" + sui::caption(D.lan_hdr) + "'\n";
    s += "status '" + g_status + "'\n";
    s += "spage first=" + std::to_string(g_space_first) + " rows=" + std::to_string(kSpaceRows) + "\n";
    return s;
}

uint32_t text_font() { return g_text_font; }

bool russian() { return g_ru; }

void set_russian(bool ru)
{
    g_ru = ru;
    g_dirty = true;
}

} // namespace coop_ui
