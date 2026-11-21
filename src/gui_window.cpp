// gui_window.cpp - the main window: control creation, layout and message handling. The run itself is in gui_run.cpp.
// The controls come from the table in gui_layout.h (created in table order, which is their z-order and Tab order) and
// are moved to the rectangles that layout::compute() returns for the window's font; no pixel position is written here.
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
#include "gui_layout.h"
#include "gui_model.h"
#include "gui_run.h"
#include "gui_text.h"
#include "platform.h"
#include "stop_gate.h"

namespace ddr5::gui {

using layout::Control;
using layout::kControlCount;
using layout::kControls;
using layout::Kind;
using layout::Metrics;
using layout::Rect;
using layout::Result;

namespace {

const UINT kRunDone = WM_APP + 1; // the worker is done; wParam = exit code of run_suite
const UINT_PTR kTimerId = 1;
const COLORREF kErrorRed = RGB(200, 0, 0); // the one custom colour: the error count while it is above zero

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

// Windows high-contrast mode: the system colours are the whole look, so no custom colour is used then.
bool query_high_contrast() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof hc;
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
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
    Metrics metrics;           // dialog base units of `font` and the height of the tab strip (layout input)
    bool built = false;        // every control exists: WM_SIZE may lay them out
    bool highContrast = false; // Windows high-contrast mode is on
    HWND lastFocus = nullptr;  // the control that had the focus when the window was deactivated
    std::vector<HWND> hwnds;   // parallel to kControls
    std::vector<HWND> pages[layout::kPageCount];
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

    HWND item(int id) const { return GetDlgItem(wnd, id); }

    // Text that changes often (time, pages, errors) goes through here: a control is only touched when its text is
    // different, so it does not flicker.
    void set_text(HWND h, const std::wstring& t) {
        if (!h)
            return;
        auto& cur = shown[h];
        if (cur == t)
            return;
        cur = t;
        SetWindowTextW(h, t.c_str());
    }

    // The message font of the system (usually Segoe UI 9 pt) at this DPI, or a larger one derived from it for the
    // Start button. When the system gives nothing, the stock GUI font is used.
    HFONT make_font(Font& owner, bool big) {
        LOGFONTW lf{};
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof ncm;
        if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, static_cast<UINT>(dpi)))
            lf = ncm.lfMessageFont;
        else if (GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof lf, &lf) == 0)
            return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (big)
            lf.lfHeight = MulDiv(lf.lfHeight, 11, 9); // 9 pt -> 11 pt (the height is negative: character height)
        owner.reset(CreateFontIndirectW(&lf));
        if (owner)
            return owner.get();
        return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    // Dialog base units of the window font: the average character width and the text height, measured the way
    // Windows documents it (MapDialogRect does the same for dialogs).
    void measure_font() {
        const LONG sys = GetDialogBaseUnits(); // fallback: the system font's units
        int baseX = LOWORD(sys), baseY = HIWORD(sys);
        if (HDC dc = GetDC(wnd)) {
            HGDIOBJ old = SelectObject(dc, font);
            const wchar_t alphabet[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
            TEXTMETRICW tm{};
            SIZE sz{};
            if (GetTextMetricsW(dc, &tm) && GetTextExtentPoint32W(dc, alphabet, 52, &sz)) {
                const int x = (sz.cx / 26 + 1) / 2, y = tm.tmHeight;
                if (x >= 1 && y >= 1) {
                    baseX = x;
                    baseY = y;
                }
            }
            if (old)
                SelectObject(dc, old);
            ReleaseDC(wnd, dc);
        }
        metrics.baseX = std::max(baseX, 1);
        metrics.baseY = std::max(baseY, 1);
    }

    // One control of the table. Group boxes and the first control of each block carry WS_GROUP, fields carry
    // WS_TABSTOP; none carries WS_CLIPSIBLINGS (a group box lies over its contents and would hide them). Labels with a
    // fixed caption keep the & of their mnemonic; text that changes has SS_NOPREFIX, so an & in it is just a character.
    HWND create_one(const Control& c) {
        const wchar_t* cls = L"STATIC";
        DWORD style = WS_CHILD | WS_VISIBLE, ex = 0;
        int w = 0, h = 0; // the real rectangle comes from the layout; the tab control needs a size to measure itself
        switch (c.kind) {
        case Kind::Tab:
            cls = WC_TABCONTROLW;
            style |= WS_GROUP | WS_TABSTOP;
            w = scale_px(200, dpi);
            h = scale_px(100, dpi);
            break;
        case Kind::Group:
            cls = L"BUTTON";
            style |= BS_GROUPBOX | WS_GROUP;
            break;
        case Kind::Label:
            style |= SS_LEFT;
            break;
        case Kind::Value:
            style |= SS_RIGHT | SS_NOPREFIX;
            break;
        case Kind::Status:
            style |= SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX;
            break;
        case Kind::Edit:
            cls = L"EDIT";
            style |= ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL | WS_TABSTOP;
            ex = WS_EX_CLIENTEDGE;
            break;
        case Kind::Spin:
            cls = UPDOWN_CLASSW;
            style |= UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS; // not a tab stop: its edit is
            break;
        case Kind::Check:
            cls = L"BUTTON";
            style |= BS_AUTOCHECKBOX | WS_TABSTOP;
            break;
        case Kind::Combo:
            cls = L"COMBOBOX";
            style |= CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
            break;
        case Kind::Button:
            cls = L"BUTTON";
            style |= BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP; // Enter presses it (DM_GETDEFID below)
            break;
        case Kind::Note:
            style |= SS_LEFT | SS_NOPREFIX;
            break;
        }
        HWND ctl = CreateWindowExW(ex, cls, c.text ? c.text : L"", style, 0, 0, w, h, wnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(c.id)), inst, nullptr);
        if (ctl)
            SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(c.kind == Kind::Button ? fontBig : font), TRUE);
        return ctl;
    }

    void show_page(int sel) {
        for (int p = 0; p < layout::kPageCount; p++)
            for (HWND h : pages[p])
                ShowWindow(h, p == sel ? SW_SHOW : SW_HIDE);
    }

    // Moves every control to the rectangle the layout gives for the current client size, all in one pass
    // (DeferWindowPos) so nothing flickers. Also the answer to WM_SIZE: a resizable window is a small change away.
    void relayout() {
        if (!built)
            return;
        RECT rc{};
        if (!GetClientRect(wnd, &rc))
            return;
        const Result r = layout::compute(kControls, kControlCount, rc.right - rc.left, metrics);
        const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
        HDWP dwp = BeginDeferWindowPos(static_cast<int>(kControlCount));
        bool ok = dwp != nullptr;
        for (size_t i = 0; i < kControlCount && ok; ++i) {
            if (kControls[i].kind == Kind::Spin)
                continue; // follows its edit
            const Rect& q = r.rect[i];
            dwp = DeferWindowPos(dwp, hwnds[i], nullptr, q.x, q.y, q.w, q.h, flags);
            ok = dwp != nullptr;
        }
        if (ok)
            ok = EndDeferWindowPos(dwp) != FALSE;
        if (!ok) { // the batch failed: move the controls one by one
            for (size_t i = 0; i < kControlCount; ++i) {
                if (kControls[i].kind == Kind::Spin)
                    continue;
                const Rect& q = r.rect[i];
                SetWindowPos(hwnds[i], nullptr, q.x, q.y, q.w, q.h, flags);
            }
        }
        // An up-down control sits at the right edge of its buddy; attach it again now that the buddy has moved.
        for (size_t i = 0; i < kControlCount; ++i)
            if (kControls[i].kind == Kind::Spin)
                SendMessageW(hwnds[i], UDM_SETBUDDY, reinterpret_cast<WPARAM>(item(kControls[i].buddy)), 0);
    }

    // Sizes the window so that its client area is exactly what the layout needs, and centres it on the screen.
    bool fit_window() {
        const int clientW = layout::dlu_x(layout::kClientWidth, metrics);
        const Result r = layout::compute(kControls, kControlCount, clientW, metrics);
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(wnd, GWL_STYLE));
        const DWORD ex = static_cast<DWORD>(GetWindowLongPtrW(wnd, GWL_EXSTYLE));
        RECT rc{0, 0, clientW, r.contentHeight};
        AdjustWindowRectEx(&rc, style, FALSE, ex);
        const int width = rc.right - rc.left, height = rc.bottom - rc.top;
        const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        return SetWindowPos(wnd, nullptr, (sw - width) / 2, (sh - height) / 3, width, height,
                            SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
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
        highContrast = query_high_contrast();
        font = make_font(ownedFont, false);
        fontBig = make_font(ownedBigFont, true);
        measure_font();

        // Create every control in table order (this is the z-order and the Tab order), then move them all at once.
        hwnds.assign(kControlCount, nullptr);
        for (size_t i = 0; i < kControlCount; ++i) {
            hwnds[i] = create_one(kControls[i]);
            if (!hwnds[i])
                return false;
            if (kControls[i].page >= 0)
                pages[kControls[i].page].push_back(hwnds[i]);
            if (kControls[i].kind == Kind::Tab)
                tab = hwnds[i];
        }
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<LPWSTR>(kTabTesting);
        if (TabCtrl_InsertItem(tab, 0, &ti) < 0)
            return false;
        ti.pszText = const_cast<LPWSTR>(kTabAdvanced);
        if (TabCtrl_InsertItem(tab, 1, &ti) < 0)
            return false;
        // The tab control is only the strip of tabs. Its height is what the display area starts at, measured at the
        // size it was created with.
        RECT strip{0, 0, scale_px(200, dpi), scale_px(100, dpi)};
        TabCtrl_AdjustRect(tab, FALSE, &strip);
        metrics.tabStrip = strip.top > 0 ? strip.top : layout::dlu_y(14, metrics);

        hTotal = item(ID_TOTAL);
        hFree = item(ID_FREE);
        hMB = item(ID_MB);
        hMBud = item(ID_MBUD);
        hThr = item(ID_THR);
        hThrUd = item(ID_THRUD);
        hBeep = item(ID_BEEP);
        hStopErr = item(ID_STOPERR);
        hDur = item(ID_DUR);
        hCov = item(ID_COV);
        hErr = item(ID_ERR);
        hTest = item(ID_TEST);
        hStart = item(ID_START);
        hCache = item(ID_CACHE);
        hRng = item(ID_RNG);
        hSet = item(ID_SET);
        hFpu = item(ID_FPU);

        SendMessageW(hBeep, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(hStopErr, BM_SETCHECK, BST_CHECKED, 0);
        for (const wchar_t* t : kCacheItems)
            SendMessageW(hCache, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        for (const wchar_t* t : kRngItems)
            SendMessageW(hRng, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        for (const wchar_t* t : kSetItems)
            SendMessageW(hSet, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        SendMessageW(hCache, CB_SETCURSEL, 0, 0);
        SendMessageW(hRng, CB_SETCURSEL, 0, 0);
        SendMessageW(hSet, CB_SETCURSEL, 0, 0);

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
        if (!fit_window())
            return false;
        built = true;
        relayout();
        show_page(0);
        refresh();
        apply_auto_mb();
        setting = false;
        return true;
    }

    // Gives the keyboard focus back to the control that had it (a window that is not a dialog does not do that by
    // itself), or to the tab strip, the first Tab stop, the first time.
    void restore_focus() {
        HWND target = lastFocus;
        if (!target || !IsWindowVisible(target) || !IsWindowEnabled(target))
            target = tab;
        SetFocus(target);
    }

    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE:
            if (!build_ui())
                return -1; // CreateWindowExW then fails and the program says so
            if (!SetTimer(h, kTimerId, 500, nullptr))
                return -1;
            return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED)
                relayout();
            return 0;
        case WM_ACTIVATE: {
            const LRESULT r = DefWindowProcW(h, msg, wp, lp);
            if (LOWORD(wp) == WA_INACTIVE) {
                HWND f = GetFocus();
                if (f && IsChild(h, f))
                    lastFocus = f;
            } else if (built && HIWORD(wp) == 0) { // activated, not minimised
                restore_focus();
            }
            return r;
        }
        case DM_GETDEFID: // IsDialogMessage asks which button Enter presses: Start/Stop on the Testing page
            if (built && TabCtrl_GetCurSel(tab) == 0 && IsWindowEnabled(hStart))
                return static_cast<LRESULT>(MAKELONG(ID_START, DC_HASDEFID));
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
            if (LOWORD(wp) == IDCANCEL) { // Esc (IsDialogMessage): the same as closing the window
                SendMessageW(h, WM_CLOSE, 0, 0);
                return 0;
            }
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
            // System colours for everything; the error count turns red while it is above zero (not in high contrast)
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            const bool red = lastErr > 0 && !highContrast && reinterpret_cast<HWND>(lp) == hErr;
            SetTextColor(dc, red ? kErrorRed : GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_SYSCOLORCHANGE:
        case WM_SETTINGCHANGE:
            highContrast = query_high_contrast();
            if (hErr)
                InvalidateRect(hErr, nullptr, TRUE);
            return DefWindowProcW(h, msg, wp, lp);
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
    // Only the classes this window uses: tab, up-down, and the standard ones (button, edit, static, combo box).
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&icc)) {
        MessageBoxW(nullptr, kWindowFailed, kAppTitle, MB_OK | MB_ICONERROR);
        return nullptr;
    }

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

    // The size here is only a start: WM_CREATE fits the window to the layout (fit_window) before it is shown.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const DWORD ex = WS_EX_CONTROLPARENT;
    HWND hw = CreateWindowExW(ex, kWindowClass, kAppTitle, style, 0, 0, 300, 300, nullptr, nullptr, inst, &w);
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
