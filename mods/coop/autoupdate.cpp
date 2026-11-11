#include "autoupdate.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <mutex>

#include "../../updater/winupdate.h"
#include "fixes.h"

namespace autoupdate {

static const SkHost* H;
static SkMod* M;
static std::string g_version, g_url;
static bool g_enabled;

enum State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed, CheckFailed };
static std::mutex g_mu;
static State g_state = Idle;
static upd::Release g_rel;
static std::string g_err;
static std::atomic<uint32_t> g_got{0}, g_total{0};
static bool g_told_available, g_told_ready, g_told_failed;
static std::atomic<bool> g_busy{false};

static void (*g_feed)(const std::string&) = nullptr;

static std::wstring game_root()
{
    std::wstring m = upd::widen(H->mods_dir()); // ...\SporebinEP1\mods
    for (int i = 0; i < 2; i++) {
        size_t k = m.find_last_of(L"\\/");
        if (k == std::wstring::npos)
            return {};
        m.resize(k);
    }
    return m;
}

// BUG-0060: a hot reload (mod.reload coop) freed the DLL while this thread was blocked in WinHTTP
// (up to 30 s); it then returned into unmapped memory or into the next image at the same base.
// Every thread holds a reference to its own module and leaves through FreeLibraryAndExitThread,
// so the old image stays mapped until the thread is done. `g_unloaded` keeps an old image's thread
// quiet after on_unload (it only finishes its own copy of the state).
static std::atomic<bool> g_unloaded{false};
static std::atomic<int> g_threads{0};
bool g_pin_fix = true; // tests: `coop.corefix update_pin 0` = threads without a module reference
COOP_FIX_UNSAFE("core.update_pin", "BUG-0060", g_pin_fix, "update: its threads hold a reference to their module (hot reload)");

static bool start_thread(LPTHREAD_START_ROUTINE fn)
{
    HMODULE self = nullptr;
    if (g_pin_fix && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(void*)&start_thread, &self))
        return false;
    g_threads++;
    HANDLE h = CreateThread(nullptr, 0, fn, self, 0, nullptr);
    if (!h) {
        g_threads--;
        if (self)
            FreeLibrary(self);
        return false;
    }
    CloseHandle(h);
    return true;
}

[[noreturn]] static void end_thread(void* self)
{
    g_busy = false;
    g_threads--;
    if (!self)
        ExitThread(0); // the old way (update_pin 0): the image may be gone under this thread
    FreeLibraryAndExitThread((HMODULE)self, 0);
}

static void set(State s, const std::string& err = {})
{
    std::lock_guard<std::mutex> l(g_mu);
    g_state = s;
    g_err = err;
}

// BUG-0241: ExitThread / FreeLibraryAndExitThread do not unwind the C++ stack: a thread function
// that called end_thread() with its locals in scope leaked them (the whole decoded update pack, several
// MB, at every Update / Try again). The work runs in a function of its own whose locals are destroyed
// when it returns; the thread functions hold no objects and only call it and end_thread().
// Off (coop.fix core.update_unwind off): end_thread() inside the work function, the 0.7.3.0 leak.
static bool g_unwind_fix = true;
COOP_FIX("core.update_unwind", "BUG-0241", g_unwind_fix, "update: the threads free the downloaded pack before they end");

static void check_work(bool exit_inside, void* self)
{
    upd::Release r;
    std::string err;
    set(Checking);
    if (!upd::latest_release(g_url, r, err)) {
        if (!g_unloaded)
            H->log(M, SK_INFO, "update check: %s", err.c_str());
        set(CheckFailed, err);
    } else {
        std::lock_guard<std::mutex> l(g_mu);
        g_rel = r;
        g_state = upd::compare_versions(r.version, g_version) > 0 && !r.pack_url.empty() ? Available : UpToDate;
        if (!g_unloaded)
            H->log(M, SK_INFO, "update check: latest %s, running %s%s", r.version.c_str(), g_version.c_str(),
                   r.from_web ? " (from the release page: the GitHub API refused, BUG-0392)" : "");
    }
    if (exit_inside)
        end_thread(self); // the 0.7.3.0 leak (r, err never destroyed)
}

static DWORD WINAPI check_thread(void* self)
{
    check_work(!g_unwind_fix, self);
    end_thread(self);
}

static void apply_work(bool exit_inside, void* self)
{
    upd::Release r;
    {
        std::lock_guard<std::mutex> l(g_mu);
        r = g_rel;
    }
    set(Downloading);
    upd::Pack p;
    std::string err;
    bool ok = upd::download_pack(r, p, err, [](uint64_t got, uint64_t total) {
        g_got = (uint32_t)got;
        g_total = (uint32_t)total;
    });
    if (ok && upd::compare_versions(p.version, g_version) <= 0) {
        ok = false;
        err = "the release pack is not newer";
    }
    int later = 0;
    if (ok)
        ok = upd::apply_pack(game_root(), p, err, &later); // files the game holds open: next start
    if (!g_unloaded)
        H->log(M, ok ? SK_INFO : SK_WARN, "update to %s: %s", r.version.c_str(), ok ? "installed" : err.c_str());
    set(ok ? Ready : Failed, err);
    if (exit_inside)
        end_thread(self); // the 0.7.3.0 leak (the pack p never freed)
}

static DWORD WINAPI apply_thread(void* self)
{
    apply_work(!g_unwind_fix, self);
    end_thread(self);
}

void init(const SkHost* host, SkMod* mod, const char* version)
{
    H = host;
    M = mod;
    g_version = version;
    char b[512];
    H->config_str(M, "update", "url", upd::kLineagesPreviewUrl, b, sizeof b);
    g_url = b;
    g_enabled = !g_url.empty() && H->config_int(M, "update", "check", g_url != upd::kLineagesPreviewUrl) != 0;
    upd::cleanup_old(game_root()); // the files an update replaced last time
    if (g_enabled && !g_busy.exchange(true))
        if (!start_thread(check_thread))
            g_busy = false;
}

void set_feed(void (*f)(const std::string&)) { g_feed = f; }

int shutdown()
{
    g_unloaded = true;
    g_feed = nullptr;
    return g_threads;
}

void tick()
{
    State s;
    std::string v, err;
    {
        std::lock_guard<std::mutex> l(g_mu);
        s = g_state;
        v = g_rel.version;
        err = g_err;
    }
    if (!g_feed)
        return;
    if (s == Available && !g_told_available) {
        g_told_available = true;
        g_feed("SporeCoop " + v + " is available: open Co-op and press Update");
    } else if (s == Ready && !g_told_ready) {
        g_told_ready = true;
        g_feed("SporeCoop " + v + " is installed: restart Spore to use it");
    } else if (s == Failed && !g_told_failed) {
        g_told_failed = true;
        g_feed("the update failed: " + err);
    }
}

// A short name for a version on one line of the co-op window: "0.7.8-lineages48-dev" -> "Lineages 48"
// (the full version made the update line wrap: "SporeCoop 0.7.8-lineages48-dev is available (you have
// 0.7.8-lineages47-dev)"); other versions keep their number: "SporeCoop 0.7.9".
static std::string short_name(const std::string& v)
{
    const auto at = v.find("lineages");
    if (at != std::string::npos) {
        size_t end = at + 8;
        while (end < v.size() && isdigit((unsigned char)v[end])) ++end;
        if (end > at + 8) return "Lineages " + v.substr(at + 8, end - at - 8);
    }
    return "SporeCoop " + v;
}

bool ui(std::string& line, std::string& button)
{
    std::lock_guard<std::mutex> l(g_mu);
    switch (g_state) {
    case Available:
        line = short_name(g_rel.version) + " is available";
        button = "Update";
        return true;
    case Downloading: {
        uint32_t got = g_got, total = g_total;
        line = "Downloading " + short_name(g_rel.version) + "... " +
               (total ? std::to_string(got * 100ull / total) + "%" : std::to_string(got / 1024) + " KB");
        return true;
    }
    case Ready:
        line = short_name(g_rel.version) + " installed: restart Spore";
        return true;
    case Failed: {
        std::string why = g_err.size() > 40 ? g_err.substr(0, 37) + "..." : g_err; // the log has it whole
        line = "Update failed: " + why;
        button = "Try Again";
        return true;
    }
    case Checking:
        line = "Checking for updates...";
        return true;
    case CheckFailed:
        line = "Could not check for updates. Try again.";
        button = "Check for Updates";
        return true;
    case Idle:
    case UpToDate:
        if (g_url.empty()) return false;
        if (g_state == UpToDate)
            line = "Lineages is up to date";
        button = "Check for Updates";
        return true;
    default:
        return false;
    }
}

void start_update()
{
    bool check = false;
    {
        std::lock_guard<std::mutex> l(g_mu);
        check = g_state == Idle || g_state == UpToDate || g_state == CheckFailed;
        if (check ? g_url.empty() : g_state != Available && g_state != Failed) {
            return;
        }
        g_told_failed = false;
    }
    if (!g_busy.exchange(true))
        if (!start_thread(check ? check_thread : apply_thread))
            g_busy = false;
}

std::string command(const std::string& arg)
{
    if (arg == "check") {
        if (!g_busy.exchange(true))
            if (!start_thread(check_thread))
            g_busy = false;
        return "checking " + g_url;
    }
    if (arg == "apply") {
        start_update();
        return "updating";
    }
    if (arg.rfind("fake ", 0) == 0) { // tests (the co-op window's look): pretend this version is out
        std::lock_guard<std::mutex> l(g_mu);
        g_rel.version = arg.substr(5);
        g_state = Available;
        return "pretending " + g_rel.version + " is available (nothing is downloaded until Update)";
    }
    static const char* names[] = {"idle", "checking", "up to date", "available", "downloading", "ready", "failed", "check failed"};
    std::lock_guard<std::mutex> l(g_mu);
    return std::string("update: ") + names[g_state] + " running=" + g_version + " latest=" + g_rel.version +
           (g_rel.from_web ? " via=page" : "") +
           " check=" + (g_enabled ? "on" : "off") + " url=" + g_url + (g_err.empty() ? "" : " error=" + g_err);
}

} // namespace autoupdate
