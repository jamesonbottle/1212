// DDR5 RAM Test - Windows GUI entry point: CPU check, single instance, message loop. Everything else is in
// gui_window.cpp (window), gui_run.cpp (run control), gui_model.h (rules), gui_text.cpp and gui_dpi.cpp (helpers).
#include <memory>
#include "cpu_check.h"
#include "gui_text.h"
#include "gui_window.h"

namespace {

// Exit codes of the program.
constexpr int kExitOk = 0;
constexpr int kExitWindowFailed = 1;   // the window could not be created (or the message loop failed)
constexpr int kExitUnsupportedCpu = 2; // this CPU or OS cannot run the AVX2 code

// Closes a Win32 handle when it goes out of scope.
struct HandleCloser {
    void operator()(HANDLE h) const noexcept { CloseHandle(h); }
};
using HandleGuard = std::unique_ptr<void, HandleCloser>;

// A copy is already running: bring its window forward (restoring it when minimised).
void activate_running_copy() {
    HWND w = nullptr;
    for (int i = 0; i < 20 && !w; i++) {
        w = FindWindowW(ddr5::gui::kWindowClass, nullptr);
        if (!w)
            Sleep(100);
    }
    if (!w) {
        MessageBoxW(nullptr, ddr5::gui::kAlreadyRunning, ddr5::gui::kAppTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (IsIconic(w))
        ShowWindow(w, SW_RESTORE);
    SetForegroundWindow(w);
}

} // namespace

// Exit codes: see kExit* above.
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    // First, before anything else: the rest of the program is compiled for AVX2 and would crash on a CPU without it
    if (const ddr5::Avx2Status avx = ddr5::avx2_status(); avx != ddr5::Avx2Status::Ok) {
        const std::wstring text = ddr5::gui::utf8_to_wide(ddr5::avx2_message(avx));
        MessageBoxW(nullptr, text.c_str(), ddr5::gui::kAppTitle, MB_OK | MB_ICONERROR);
        return kExitUnsupportedCpu;
    }
    // One copy at a time. The mutex is held until the program ends (the guard is declared before the window, so it is
    // closed after it). ERROR_ALREADY_EXISTS: a copy of this user session runs; ERROR_ACCESS_DENIED: the mutex exists
    // but belongs to another user, so a copy runs there. Any other failure to create it is not fatal: the program then
    // runs without the one-copy rule.
    SetLastError(0);
    const HandleGuard once(CreateMutexW(nullptr, FALSE, ddr5::gui::kOnceMutex));
    const DWORD onceErr = GetLastError();
    if (onceErr == ERROR_ALREADY_EXISTS || (!once && onceErr == ERROR_ACCESS_DENIED)) {
        activate_running_copy();
        return kExitOk;
    }

    const std::unique_ptr<ddr5::gui::MainWindow> window = ddr5::gui::MainWindow::create(inst, show);
    if (!window)
        return kExitWindowFailed;

    MSG msg{};
    for (;;) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0)
            break; // WM_QUIT
        if (got < 0)
            return kExitWindowFailed;
        if (!window->translate_dialog(&msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam); // the code the window gave PostQuitMessage (0)
}
