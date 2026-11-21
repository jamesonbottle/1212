// DDR5 RAM Test - Windows GUI entry point: CPU check, single instance, message loop. Everything else is in
// gui_window.cpp (window), gui_run.cpp (run control), gui_model.h (rules), gui_text.cpp and gui_dpi.cpp (helpers).
#include <memory>
#include "cpu_check.h"
#include "gui_text.h"
#include "gui_window.h"

namespace {

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

// Exit codes: 0 normal, 1 the window could not be created, 2 this CPU or OS cannot run the AVX2 code.
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    // First, before anything else: the rest of the program is compiled for AVX2 and would crash on a CPU without it
    if (const ddr5::Avx2Status avx = ddr5::avx2_status(); avx != ddr5::Avx2Status::Ok) {
        const std::wstring text = ddr5::gui::utf8_to_wide(ddr5::avx2_message(avx));
        MessageBoxW(nullptr, text.c_str(), ddr5::gui::kAppTitle, MB_OK | MB_ICONERROR);
        return 2;
    }
    // One copy at a time.
    SetLastError(0);
    HANDLE once = CreateMutexW(nullptr, FALSE, ddr5::gui::kOnceMutex);
    const DWORD onceErr = GetLastError();
    if (onceErr == ERROR_ALREADY_EXISTS || (!once && onceErr == ERROR_ACCESS_DENIED)) {
        activate_running_copy();
        return 0;
    }

    const std::unique_ptr<ddr5::gui::MainWindow> window = ddr5::gui::MainWindow::create(inst, show);
    if (!window)
        return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!window->translate_dialog(&msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
