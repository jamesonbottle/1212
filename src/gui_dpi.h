#pragma once
// gui_dpi.h - DPI helpers of the GUI. Every size in the window goes through scale_px() with the DPI of the window
// (per-monitor v2: it changes when the window is dragged to another monitor, see WM_DPICHANGED in gui_window.cpp).
// The arithmetic is plain C++ (no Win32 types), so tools/unit_tests.cpp tests it on every platform; the one call that
// asks Windows, window_dpi(), is defined in gui_dpi.cpp. Its parameter is spelled HWND__*, which is what HWND is, so
// this header does not need <windows.h>.

struct HWND__;

namespace ddr5::gui {

inline constexpr int kBaseDpi = 96; // 100 % scaling

// The DPI of the monitor the window is on right now (never below 96). Not a constant: it changes when the window moves.
int window_dpi(HWND__* wnd);

// A DPI value reported by Windows, made usable: 0 (the call failed) or anything below 96 becomes 96.
constexpr int valid_dpi(int reported) {
    return reported < kBaseDpi ? kBaseDpi : reported;
}

// `px` pixels designed for 96 DPI, scaled to `dpi`, rounded to the nearest pixel (halves away from zero, like MulDiv).
constexpr int scale_px(int px, int dpi) {
    const long long n = static_cast<long long>(px) * dpi;
    return static_cast<int>(n >= 0 ? (n + kBaseDpi / 2) / kBaseDpi : -((-n + kBaseDpi / 2) / kBaseDpi));
}

} // namespace ddr5::gui
