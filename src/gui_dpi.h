#pragma once
// gui_dpi.h - DPI helpers of the GUI. Every size in the window goes through scale_px() with the window's DPI.
// (Per-monitor DPI and WM_DPICHANGED come with point 4 of GUI_REWRITE.md; this is the system DPI as before.)

namespace ddr5::gui {

// DPI of the primary display (the system DPI), never below 96.
int system_dpi();

// `px` pixels designed for 96 DPI, scaled to `dpi`.
int scale_px(int px, int dpi);

// Height for CreateFontW of a font of `points` points at `dpi` (negative: character height).
int font_height_px(int points, int dpi);

} // namespace ddr5::gui
