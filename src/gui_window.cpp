// gui_window.cpp - the main window: control creation, layout and message handling. The run itself is in gui_run.cpp.
#include "gui_window.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "coverage.h"
#include "gui_dpi.h"
#include "gui_model.h"
#include "gui_run.h"
#include "gui_text.h"
#include "platform.h"
#include "stop_gate.h"

namespace ddr5::gui {

namespace {

enum Ids {
    ID_TAB = 100,
    ID_TOTAL,
    ID_FREE,
    ID_MB,
    ID_MBUD,
    ID_THR,
    ID_THRUD,
    ID_BEEP,
    ID_STOPERR,
    ID_DUR,
    ID_COV,
    ID_ERR,
    ID_TEST,
    ID_START,
    ID_CACHE,
    ID_RNG,
    ID_SET,
    ID_FPU
};
const UINT kRunDone = WM_APP + 1; // the worker is done; wParam = exit code of run_suite
const UINT_PTR kTimerId = 1;

// A font the window created and has to delete.
struct FontDeleter {
    using pointer = HFONT;
    void operator()(HFONT f) const { DeleteObject(f); }
};
using Font = std::unique_ptr<std::remove_pointer_t<HFONT>, FontDeleter>;

std::wstring exe_dir_file(const wchar_t* name) {
    wchar_t p[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, p, static_cast<DWORD>(sizeof p / sizeof p[0]));
    std::wstring s(p, n);
    const size_t k = s.find_last_of(L"\\/");
    s = k == std::wstring::npos ? L"" : s.substr(0, k + 1);
    return s + name;
}

// The log goes next to the exe; the temp folder is the fallback (the exe may be in a read-only folder).
LogPaths log_paths() {
    LogPaths p;
    p.primary = exe_dir_file(kLogFileName);
    wchar_t tp[MAX_PATH + 1];
    const DWORD n = GetTempPathW(MAX_PATH, tp);
    if (n && n <= MAX_PATH)
        p.fallback = std::wstring(tp, n) + kLogFileName;
    return p;
}

SystemInfo query_system() {
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    GlobalMemoryStatusEx(&m);
    SystemInfo s;
    s.cpus = std::max(1, plat::ncpu());
    s.totalMB = m.ullTotalPhys >> 20;
    s.availPhys = m.ullAvailPhys;
    s.availCommit = m.ullAvailPageFile;
    return s;
}

long long read_number(HWND edit) {
    wchar_t b[32] = {};
    GetWindowTextW(edit, b, 32);
    return _wtoi64(b);
}
bool is_checked(HWND box) {
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
int selected_index(HWND combo) {
    return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
}

// The caption colour is polish: when any call fails it is ignored.
void apply_caption_style(HWND h) {
    HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dwm)
        return; // System32 only: no DLL search-order hijack
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    Fn f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
    if (f) {
        const COLORREF cap = RGB(128, 0, 0), txt = RGB(255, 255, 255), brd = RGB(128, 0, 0);
        f(h, 35, &cap, sizeof cap);
        f(h, 36, &txt, sizeof txt);
        f(h, 34, &brd, sizeof brd);
    }
    FreeLibrary(dwm);
}

} // namespace

// All the state of the window. Only the UI thread touches it (the worker only posts kRunDone).
struct MainWindow::Impl {
    HINSTANCE inst = nullptr;
    HWND wnd = nullptr, tab = nullptr;
    Font ownedFont, ownedBigFont;
    HFONT font = nullptr, fontBig = nullptr; // in use: the owned ones, or a stock font when CreateFont failed
    int dpi = 96;
    std::vector<HWND> pages[2];
    HWND hTotal = nullptr, hFree = nullptr, hMB = nullptr, hMBud = nullptr, hThr = nullptr, hThrUd = nullptr,
         hBeep = nullptr, hStopErr = nullptr, hDur = nullptr, hCov = nullptr, hErr = nullptr, hTest = nullptr,
         hStart = nullptr, hCache = nullptr, hRng = nullptr, hSet = nullptr, hFpu = nullptr;
    std::vector<HWND> lockWhileRunning;
    GuiRun run;
    double endT = 0;
    u64 lastErr = 0;
    bool stopRequested = false;
    StopGate gate; // Stop / close rules (stop_gate.h)
    bool mbAuto = true, setting = true;
    std::unordered_map<HWND, std::wstring> shown; // last text per control, to avoid flicker
    u64 totalMB = 0;
    bool buildFailed = false; // a control could not be created

    int px(int v) const { return scale_px(v, dpi); }

    HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, int pg,
            DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, px(x), px(y), px(w),
                                 px(h), wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        if (!c) {
            buildFailed = true;
            return nullptr;
        }
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (pg >= 0)
            pages[pg].push_back(c);
        return c;
    }
    HWND group(const wchar_t* text, int x, int y, int w, int h, int pg) {
        return mk(L"BUTTON", text, BS_GROUPBOX, x, y, w, h, 0, pg);
    }
    HWND label(const wchar_t* text, int x, int y, int w, int pg, DWORD style = SS_LEFT, int id = 0) {
        return mk(L"STATIC", text, style, x, y, w, 16, id, pg);
    }

    void set_text(HWND h, const std::wstring& t) {
        if (!h)
            return;
        auto& cur = shown[h];
        if (cur == t)
            return;
        cur = t;
        SetWindowTextW(h, t.c_str());
    }

    HFONT make_font(Font& owner, int points) {
        owner.reset(CreateFontW(font_height_px(points, dpi), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI"));
        if (owner)
            return owner.get();
        return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    void show_page(int sel) {
        for (int p = 0; p < 2; p++)
            for (HWND h : pages[p])
                ShowWindow(h, p == sel ? SW_SHOW : SW_HIDE);
    }

    // CreateWindow puts a new child window BELOW the siblings that already exist. The tab control is created first, so
    // it ended up above every page control and its body covered them: the tabs showed, the page stayed empty (seen on
    // a Windows runner: SysTabControl32 was first in the top-to-bottom order). The same order put each group box above
    // the controls inside it, and those carry WS_CLIPSIBLINGS. Fix: tab at the bottom, then raise the page controls in
    // the order they were created, so every control is above the ones made before it (group boxes below their
    // contents).
    void fix_z_order() {
        const UINT keep = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
        SetWindowPos(tab, HWND_BOTTOM, 0, 0, 0, 0, keep);
        for (auto& pg : pages)
            for (HWND h : pg)
                SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, keep);
    }

    void set_running_ui(bool running) {
        for (HWND h : lockWhileRunning)
            EnableWindow(h, !running);
        set_text(hStart, running ? kStop : kStart);
        EnableWindow(hStart, TRUE);
    }

    FormValues read_form() const {
        FormValues f;
        f.threadsText = read_number(hThr);
        f.megabytesText = read_number(hMB);
        f.megabytesAuto = mbAuto;
        f.stopOnError = is_checked(hStopErr);
        f.cacheIndex = selected_index(hCache);
        f.rngIndex = selected_index(hRng);
        f.suiteIndex = selected_index(hSet);
        f.stressFpu = is_checked(hFpu);
        return f;
    }

    // Fills the Megabytes field with the automatic size for the thread count in the Threads field.
    void apply_auto_mb() {
        const SystemInfo sys = query_system();
        const int threads = threads_for_auto(read_number(hThr), sys.cpus);
        setting = true;
        SetWindowTextW(hMB, std::to_wstring(auto_megabytes(threads, sys.availPhys, sys.availCommit)).c_str());
        setting = false;
    }

    void refresh() {
        const SystemInfo sys = query_system();
        totalMB = sys.totalMB;
        set_text(hTotal, std::to_wstring(sys.totalMB) + L" MB");
        set_text(hFree, std::to_wstring(sys.availPhys >> 20) + L" MB");
        if (!run.has_run())
            return;
        const bool busy = run.busy();
        if (busy && gate.tick(plat::now())) {
            set_text(hStart, kForceQuit);
            EnableWindow(hStart, TRUE);
        }
        const Run::Status st = run.status();
        double t = st.tStart > 0 ? (busy ? plat::now() : endT) - st.tStart : 0;
        if (t < 0)
            t = 0;
        set_text(hDur, duration_text(static_cast<u64>(t)));
        // one decimal, never 100.0 early
        set_text(hCov, utf8_to_wide(percent_text(st.pagesCovered, st.pagesTotal)) + L" %");
        const u64 err = st.errors;
        set_text(hErr, std::to_wstring(err));
        if (err != lastErr) {
            if (err > lastErr && is_checked(hBeep))
                MessageBeep(MB_ICONERROR);
            lastErr = err;
            InvalidateRect(hErr, nullptr, TRUE);
        }
        if (busy && gate.stopping())
            set_text(hTest, stopping_text(gate.force_offered(), gate.closing()));
        else if (busy)
            set_text(hTest, st.tStart > 0 ? utf8_to_wide(st.label) : std::wstring(kAllocating));
        else if (!stopRequested && err == 0 && st.tStart > 0)
            set_text(hTest, kFinished);
    }

    // run.start() could not create the Run or its worker thread (FIXES.md F2).
    void abort_start(const std::string& why) {
        set_text(hTest, kCouldNotRun);
        const std::wstring msg = std::wstring(kCouldNotStartPrefix) + ansi_to_wide(why) + kCouldNotStartSuffix;
        MessageBoxW(wnd, msg.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    }

    void start_run() {
        if (run.busy())
            return;
        const Settings s = settings_from_form(read_form(), query_system());
        // show what will really run; the fields are not user input now, so EN_CHANGE is ignored
        setting = true;
        SetWindowTextW(hMB, std::to_wstring(s.megabytes).c_str());
        SetWindowTextW(hThr, std::to_wstring(s.threads).c_str());
        setting = false;

        const HWND target = wnd;
        std::string why;
        const bool started = run.start(
            s, log_paths(), [target](int rc) { PostMessageW(target, kRunDone, static_cast<WPARAM>(rc), 0); }, why);
        if (!started) {
            abort_start(why);
            return;
        }
        lastErr = 0;
        stopRequested = false;
        endT = 0;
        set_text(hDur, kZeroDuration);
        set_text(hCov, kZeroPercent);
        set_text(hErr, kZero);
        set_text(hTest, kAllocating);
        InvalidateRect(hErr, nullptr, TRUE);
        gate.on_run_start();
        set_running_ui(true);
    }

    void request_stop() {
        if (!run.busy() || !run.has_run())
            return;
        stopRequested = true;
        run.request_stop();
        set_text(hStart, kStopping);
        EnableWindow(hStart, FALSE);
        set_text(hTest, kStopping);
    }

    // The way out of a stop that does not finish (one long OS call, a wedged run)
    [[noreturn]] void force_exit() {
        run.try_close_log();
        TerminateProcess(GetCurrentProcess(), 1);
        std::abort();
    }

    // Carry out what the StopGate decided.
    void apply_gate(StopGate::Act a) {
        if (a == StopGate::Act::ForceExit)
            force_exit();
        if (a == StopGate::Act::RequestStop)
            request_stop();
        else if (a == StopGate::Act::CloseNow)
            DestroyWindow(wnd);
    }

    void on_run_done(int rc, bool closing) {
        const std::string last = run.finish(); // joins the worker, closes the log
        endT = plat::now();
        set_running_ui(false);
        refresh();
        DoneInfo d;
        d.errors = run.error_count();
        d.logFailed = run.log_failed();
        d.stopRequested = stopRequested;
        d.logPlace = run.log_place();
        if (const wchar_t* text = done_status_text(d))
            set_text(hTest, text);
        if (exit_code_is_failure(rc)) { // not kExitIncomplete: Stop gives that, and "Stopped" is already shown
            set_text(hTest, kCouldNotRun);
            if (closing)
                return; // the user is closing the window: no box in the way
            std::wstring msg = utf8_to_wide(last);
            if (msg.empty())
                msg = kCouldNotStartPlain;
            MessageBoxW(wnd, msg.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
        }
    }

    bool build_ui() {
        font = make_font(ownedFont, 9);
        fontBig = make_font(ownedBigFont, 11);

        tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, px(6),
                              px(6), px(238), px(388), wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TAB)), inst,
                              nullptr);
        if (!tab)
            return false;
        SendMessageW(tab, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<LPWSTR>(kTabTesting);
        TabCtrl_InsertItem(tab, 0, &ti);
        ti.pszText = const_cast<LPWSTR>(kTabAdvanced);
        TabCtrl_InsertItem(tab, 1, &ti);

        const int X = 14, W = 222;
        // Testing page
        int y = 38;
        group(kGroupSystem, X, y, W, 58, 0);
        label(kLabelTotal, X + 10, y + 20, 100, 0);
        hTotal = label(L"", X + 110, y + 20, 100, 0, SS_RIGHT, ID_TOTAL);
        label(kLabelFree, X + 10, y + 38, 100, 0);
        hFree = label(L"", X + 110, y + 38, 100, 0, SS_RIGHT, ID_FREE);

        y += 64;
        group(kGroupSettings, X, y, W, 122, 0);
        label(kLabelMegabytes, X + 10, y + 26, 90, 0);
        hMB = mk(L"EDIT", L"", ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL | WS_TABSTOP, X + 108, y + 22, 102, 22, ID_MB, 0,
                 WS_EX_CLIENTEDGE);
        hMBud = mk(UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0,
                   ID_MBUD, 0);
        label(kLabelThreads, X + 10, y + 54, 90, 0);
        hThr = mk(L"EDIT", L"", ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL | WS_TABSTOP, X + 108, y + 50, 102, 22, ID_THR, 0,
                  WS_EX_CLIENTEDGE);
        hThrUd = mk(UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0,
                    ID_THRUD, 0);
        hBeep = mk(L"BUTTON", kCheckBeep, BS_AUTOCHECKBOX | WS_TABSTOP, X + 10, y + 82, 190, 18, ID_BEEP, 0);
        hStopErr =
            mk(L"BUTTON", kCheckStopOnError, BS_AUTOCHECKBOX | WS_TABSTOP, X + 10, y + 100, 190, 18, ID_STOPERR, 0);
        SendMessageW(hBeep, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(hStopErr, BM_SETCHECK, BST_CHECKED, 0);

        y += 128;
        group(kGroupStatus, X, y, W, 102, 0);
        label(kLabelDuration, X + 10, y + 20, 90, 0);
        hDur = label(kZeroDuration, X + 110, y + 20, 100, 0, SS_RIGHT, ID_DUR);
        label(kLabelPages, X + 10, y + 38, 100, 0);
        hCov = label(kZeroPercentDecimal, X + 110, y + 38, 100, 0, SS_RIGHT, ID_COV);
        label(kLabelErrors, X + 10, y + 56, 90, 0);
        hErr = label(kZero, X + 110, y + 56, 100, 0, SS_RIGHT, ID_ERR);
        hTest = label(L"", X + 10, y + 78, W - 20, 0, SS_LEFT | SS_ENDELLIPSIS, ID_TEST);

        y += 108;
        hStart = mk(L"BUTTON", kStart, BS_PUSHBUTTON | WS_TABSTOP, X, y, W, 44, ID_START, 0);
        SendMessageW(hStart, WM_SETFONT, reinterpret_cast<WPARAM>(fontBig), TRUE);

        // Advanced page
        y = 38;
        group(kGroupAdvanced, X, y, W, 112, 1);
        label(kLabelCache, X + 10, y + 28, 80, 1);
        hCache =
            mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, X + 92, y + 24, 118, 160, ID_CACHE, 1);
        label(kLabelRng, X + 10, y + 58, 80, 1);
        hRng = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, X + 92, y + 54, 118, 160, ID_RNG, 1);
        label(kLabelSet, X + 10, y + 88, 80, 1);
        hSet = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, X + 92, y + 84, 118, 160, ID_SET, 1);
        for (const wchar_t* t : kCacheItems)
            SendMessageW(hCache, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        for (const wchar_t* t : kRngItems)
            SendMessageW(hRng, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        for (const wchar_t* t : kSetItems)
            SendMessageW(hSet, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        SendMessageW(hCache, CB_SETCURSEL, 0, 0);
        SendMessageW(hRng, CB_SETCURSEL, 0, 0);
        SendMessageW(hSet, CB_SETCURSEL, 0, 0);

        y += 118;
        group(kGroupExperimental, X, y, W, 48, 1);
        hFpu = mk(L"BUTTON", kCheckFpu, BS_AUTOCHECKBOX | WS_TABSTOP, X + 10, y + 20, 190, 18, ID_FPU, 1);

        y += 56;
        mk(L"STATIC", kAdvancedHelp, SS_LEFT, X, y, W, 150, 0, 1);

        if (buildFailed)
            return false;

        // spin buttons: ranges, defaults, acceleration
        const SystemInfo sys = query_system();
        totalMB = sys.totalMB;
        const u64 freeMB = sys.availPhys >> 20;
        const u64 defMB = std::max<u64>(64, freeMB / 100 * 90);
        SendMessageW(hMBud, UDM_SETBUDDY, reinterpret_cast<WPARAM>(hMB), 0);
        SendMessageW(hMBud, UDM_SETRANGE32, 64, static_cast<LPARAM>(std::max<u64>(totalMB, 64)));
        SendMessageW(hMBud, UDM_SETPOS32, 0, static_cast<LPARAM>(defMB));
        UDACCEL acc[3] = {{0, 16}, {1, 128}, {3, 1024}};
        SendMessageW(hMBud, UDM_SETACCEL, 3, reinterpret_cast<LPARAM>(acc));
        SendMessageW(hThrUd, UDM_SETBUDDY, reinterpret_cast<WPARAM>(hThr), 0);
        SendMessageW(hThrUd, UDM_SETRANGE32, 1, 256);
        SendMessageW(hThrUd, UDM_SETPOS32, 0, sys.cpus);

        lockWhileRunning = {hMB, hMBud, hThr, hThrUd, hStopErr, hCache, hRng, hSet, hFpu};
        fix_z_order();
        show_page(0);
        refresh();
        apply_auto_mb();
        setting = false;
        return true;
    }

    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE:
            if (!build_ui())
                return -1; // CreateWindowExW then fails and the program says so
            if (!SetTimer(h, kTimerId, 500, nullptr))
                return -1;
            return 0;
        case WM_TIMER:
            if (wp == kTimerId)
                refresh();
            return 0;
        case WM_NOTIFY: {
            const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
            if (n->idFrom == ID_TAB && n->code == TCN_SELCHANGE)
                show_page(TabCtrl_GetCurSel(tab));
            return 0;
        }
        case WM_COMMAND:
            if (HIWORD(wp) == EN_CHANGE && !setting) {
                if (LOWORD(wp) == ID_MB) {
                    wchar_t b[32] = {};
                    GetWindowTextW(hMB, b, 32);
                    mbAuto = b[0] == 0;
                    if (mbAuto)
                        apply_auto_mb();
                } // typed a value = manual; cleared = automatic
                else if (LOWORD(wp) == ID_THR && mbAuto)
                    apply_auto_mb(); // thread count changed: refill memory automatically
            }
            if (LOWORD(wp) == ID_START && HIWORD(wp) == BN_CLICKED) {
                if (run.busy())
                    apply_gate(gate.on_stop_click(plat::now()));
                else
                    start_run();
            }
            return 0;
        case kRunDone: {
            const StopGate::Act a = gate.on_run_done();
            on_run_done(static_cast<int>(wp), a == StopGate::Act::CloseNow);
            apply_gate(a);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            if (reinterpret_cast<HWND>(lp) == hErr)
                SetTextColor(dc, lastErr > 0 ? RGB(200, 0, 0) : RGB(0, 128, 128));
            else
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_CLOSE:
            apply_gate(gate.on_close(plat::now()));
            if (gate.running())
                set_text(hTest, stopping_text(gate.force_offered(), gate.closing()));
            return 0;
        case WM_DESTROY:
            KillTimer(h, kTimerId);
            run.close_log();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

namespace {

LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) { // the object comes in through CreateWindowExW's last argument
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        MainWindow::Impl* created = static_cast<MainWindow::Impl*>(cs->lpCreateParams);
        created->wnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        return DefWindowProcW(h, msg, wp, lp);
    }
    MainWindow::Impl* self = reinterpret_cast<MainWindow::Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self)
        return DefWindowProcW(h, msg, wp, lp);
    const LRESULT result = self->handle(h, msg, wp, lp);
    if (msg == WM_NCDESTROY)
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    return result;
}

} // namespace

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() = default;

std::unique_ptr<MainWindow> MainWindow::create(HINSTANCE inst, int show) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    std::unique_ptr<MainWindow> mw(new MainWindow());
    mw->impl_ = std::make_unique<Impl>();
    Impl& w = *mw->impl_;
    w.inst = inst;
    w.dpi = system_dpi();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, kWindowFailed, kAppTitle, MB_OK | MB_ICONERROR);
        return nullptr;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const DWORD ex = WS_EX_CONTROLPARENT;
    RECT r{0, 0, w.px(250), w.px(400)};
    AdjustWindowRectEx(&r, style, FALSE, ex);
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    const int width = r.right - r.left, height = r.bottom - r.top;
    HWND hw = CreateWindowExW(ex, kWindowClass, kAppTitle, style, (sw - width) / 2, (sh - height) / 3, width, height,
                              nullptr, nullptr, inst, &w);
    if (!hw) {
        MessageBoxW(nullptr, kWindowFailed, kAppTitle, MB_OK | MB_ICONERROR);
        return nullptr;
    }
    apply_caption_style(hw);
    ShowWindow(hw, show);
    UpdateWindow(hw);
    return mw;
}

bool MainWindow::translate_dialog(MSG* msg) {
    return IsDialogMessageW(impl_->wnd, msg) != FALSE;
}

} // namespace ddr5::gui
