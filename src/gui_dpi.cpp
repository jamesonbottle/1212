// gui_dpi.cpp - see gui_dpi.h.
#include "gui_dpi.h"
#include "gui_win.h"

namespace ddr5::gui {

int system_dpi() {
    int dpi = 96;
    if (HDC dc = GetDC(nullptr)) {
        dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
    }
    return dpi < 96 ? 96 : dpi;
}

int scale_px(int px, int dpi) {
    return MulDiv(px, dpi, 96);
}

} // namespace ddr5::gui
