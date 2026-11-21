// gui_dpi.cpp - see gui_dpi.h.
#include "gui_dpi.h"
#include "gui_win.h"

namespace ddr5::gui {

// GetDpiForWindow returns 0 for a bad handle; valid_dpi() then gives 96.
int window_dpi(HWND__* wnd) {
    return valid_dpi(static_cast<int>(GetDpiForWindow(wnd)));
}

} // namespace ddr5::gui
