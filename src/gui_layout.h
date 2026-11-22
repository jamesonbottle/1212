#pragma once
// gui_layout.h - the control table of the main window and the computation of every control's rectangle.
// No Win32 types and no Win32 calls, so it compiles (and is unit tested) on every platform; gui_window.cpp creates the
// controls from the table and moves them to the rectangles compute() returns.
//
// Sizes are in dialog units (DLU), the font-relative unit of Windows dialogs: 4 horizontal units are one average
// character width of the window font, 8 vertical units are one text height. So the layout follows the font and the DPI
// (both are in the Metrics) and no pixel number appears here. Dialog units are only converted to pixels at the end.
//
// ORDER IS MEANING. The table is in creation order, and Windows makes creation order the z-order and the Tab-key order
// of sibling controls (the first control created is the topmost and the first to get Tab). So the table runs in
// reading order: the tab strip first, then each page top to bottom, left to right, a group box before its contents.
// There is no z-order fix afterwards; tools/unit_tests.cpp checks that the order is the reading order.
#include <algorithm>
#include <cstddef>
#include <vector>
#include "gui_text.h"

namespace ddr5::gui {

// Control ids (0 = nothing looks the control up). Above the ids Windows gives to dialog buttons (IDOK, IDCANCEL...).
enum ControlId : int {
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
    ID_FPU,
    ID_MBHINT,  // the line under the Megabytes field
    ID_THRHINT, // the line under the Threads field
    ID_UNLOCKED,
    ID_LOOPS,
    ID_MINUTES,
    ID_COVER,
    ID_MAXLOG,
    ID_LIMITHINT // the lines under the four fields of the Limits page
};

namespace layout {

enum class Kind {
    Tab,    // the tab strip (only the strip: the pages are drawn by the window, so no control covers another)
    Group,  // group box around `rowSpan` rows; not a tab stop
    Label,  // fixed caption, may carry a mnemonic (&)
    Value,  // text that changes, right aligned
    Status, // text that changes, left aligned, cut with "..."
    Edit,   // number field
    Spin,   // up-down control; it is placed by its buddy edit, not by the layout
    Check,  // check box over both columns
    Combo,  // drop-down list
    Button, // the Start button, over both columns
    Note,   // wrapped paragraph over both columns
    Caution // wrapped text of up to three lines over both columns (a warning, or a message under fields)
};

struct Control {
    int id;
    Kind kind;
    const wchar_t* text; // caption; nullptr or L"" when there is none
    int page;            // -1: always visible (the tab strip), else the page of the tab strip (0, 1, 2)
    int row;             // row within the page; a Group names the first row inside it
    int col;             // 0 = label column, 1 = field column
    int colSpan;         // 1 or 2 (2: over both columns)
    int rowSpan;         // Group: number of rows inside it; others 1
    int buddy;           // Spin: id of its Edit
};

constexpr Control tab(int id) {
    return {id, Kind::Tab, nullptr, -1, 0, 0, 2, 1, 0};
}
constexpr Control group(int page, int row, int rows, const wchar_t* text) {
    return {0, Kind::Group, text, page, row, 0, 2, rows, 0};
}
constexpr Control label(int page, int row, const wchar_t* text) {
    return {0, Kind::Label, text, page, row, 0, 1, 1, 0};
}
constexpr Control value(int page, int row, int id, const wchar_t* text) {
    return {id, Kind::Value, text, page, row, 1, 1, 1, 0};
}
constexpr Control status(int page, int row, int id) {
    return {id, Kind::Status, L"", page, row, 0, 2, 1, 0};
}
constexpr Control edit(int page, int row, int id) {
    return {id, Kind::Edit, nullptr, page, row, 1, 1, 1, 0};
}
constexpr Control spin(int page, int row, int id, int buddy) {
    return {id, Kind::Spin, nullptr, page, row, 1, 1, 1, buddy};
}
constexpr Control check(int page, int row, int id, const wchar_t* text) {
    return {id, Kind::Check, text, page, row, 0, 2, 1, 0};
}
constexpr Control combo(int page, int row, int id) {
    return {id, Kind::Combo, nullptr, page, row, 1, 1, 1, 0};
}
constexpr Control button(int page, int row, int id, const wchar_t* text) {
    return {id, Kind::Button, text, page, row, 0, 2, 1, 0};
}
constexpr Control note(int page, int row, const wchar_t* text) {
    return {0, Kind::Note, text, page, row, 0, 2, 1, 0};
}
constexpr Control caution(int page, int row, int id, const wchar_t* text) {
    return {id, Kind::Caution, text, page, row, 0, 2, 1, 0};
}

// The table. A label comes right before the field it names (the mnemonic of the label moves to the next tab stop, and
// a screen reader reads the label before the field).
inline constexpr Control kControls[] = {
    tab(ID_TAB),
    // Testing page
    group(0, 0, 2, kGroupSystem),
    label(0, 0, kLabelTotal),
    value(0, 0, ID_TOTAL, L""),
    label(0, 1, kLabelFree),
    value(0, 1, ID_FREE, L""),
    group(0, 2, 6, kGroupSettings),
    label(0, 2, kLabelMegabytes),
    edit(0, 2, ID_MB),
    spin(0, 2, ID_MBUD, ID_MB),
    status(0, 3, ID_MBHINT),
    label(0, 4, kLabelThreads),
    edit(0, 4, ID_THR),
    spin(0, 4, ID_THRUD, ID_THR),
    status(0, 5, ID_THRHINT),
    check(0, 6, ID_BEEP, kCheckBeep),
    check(0, 7, ID_STOPERR, kCheckStopOnError),
    group(0, 8, 4, kGroupStatus),
    label(0, 8, kLabelDuration),
    value(0, 8, ID_DUR, kZeroDuration),
    label(0, 9, kLabelPages),
    value(0, 9, ID_COV, kZeroPercentDecimal),
    label(0, 10, kLabelErrors),
    value(0, 10, ID_ERR, kZero),
    status(0, 11, ID_TEST),
    button(0, 12, ID_START, kStart),
    // Advanced page
    group(1, 0, 3, kGroupAdvanced),
    label(1, 0, kLabelCache),
    combo(1, 0, ID_CACHE),
    label(1, 1, kLabelRng),
    combo(1, 1, ID_RNG),
    label(1, 2, kLabelSet),
    combo(1, 2, ID_SET),
    group(1, 3, 1, kGroupExperimental),
    check(1, 3, ID_FPU, kCheckFpu),
    group(1, 4, 2, kGroupMemory),
    check(1, 4, ID_UNLOCKED, kCheckUnlocked),
    caution(1, 5, 0, kUnlockedWarning),
    note(1, 6, kAdvancedHelp),
    // Limits page
    group(2, 0, 5, kGroupLimits),
    label(2, 0, kLabelLoops),
    edit(2, 0, ID_LOOPS),
    label(2, 1, kLabelMinutes),
    edit(2, 1, ID_MINUTES),
    label(2, 2, kLabelCoverage),
    edit(2, 2, ID_COVER),
    label(2, 3, kLabelMaxLog),
    edit(2, 3, ID_MAXLOG),
    caution(2, 4, ID_LIMITHINT, L""),
};
inline constexpr std::size_t kControlCount = sizeof kControls / sizeof kControls[0];
inline constexpr int kPageCount = 3;

// ---- what the layout needs to know about the window ----

struct Metrics {
    int baseX = 6;     // pixels of 4 horizontal dialog units: the average character width of the window font
    int baseY = 15;    // pixels of 8 vertical dialog units: the text height of the window font
    int tabStrip = 24; // pixels: height of the tab strip, measured from the tab control
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

// ---- the rules, in dialog units ----
inline constexpr int kClientWidth = 160; // width of the client area when the window is first shown
inline constexpr int kMargin = 6;        // window edge to group box, and the top of the tab strip
inline constexpr int kInnerPad = 6;      // group box edge to its contents
inline constexpr int kGroupTop = 11;     // top edge of a group box to its first row (room for the caption)
inline constexpr int kGroupBottom = 6;   // last row to the bottom edge of the group box
inline constexpr int kRowGap = 2;        // between two rows of one group
inline constexpr int kBlockGap = 4;      // between two blocks (group boxes, the button)
inline constexpr int kLabelColumn = 62;  // width of the label column
inline constexpr int kColumnGap = 2;
inline constexpr int kDropDown = 60; // height of the open list of a combo box, added to its closed height

// a*b/c rounded to the nearest whole number (a, b >= 0, c > 0), like Win32 MulDiv
inline int mul_div(int a, int b, int c) {
    return static_cast<int>((static_cast<long long>(a) * b + c / 2) / c);
}
inline int dlu_x(int dlu, const Metrics& m) {
    return mul_div(dlu, m.baseX, 4);
}
inline int dlu_y(int dlu, const Metrics& m) {
    return mul_div(dlu, m.baseY, 8);
}

// Height of a control in dialog units, as it takes room in its row (a combo box counts closed).
inline int cell_dlu(Kind k) {
    switch (k) {
    case Kind::Label:
    case Kind::Value:
    case Kind::Status:
        return 8;
    case Kind::Edit:
    case Kind::Spin:
    case Kind::Combo:
        return 14;
    case Kind::Check:
        return 10;
    case Kind::Button:
        return 22;
    case Kind::Caution:
        return 24;
    case Kind::Note:
        return 80;
    case Kind::Tab:
    case Kind::Group:
        break;
    }
    return 0;
}

struct Result {
    std::vector<Rect> rect; // parallel to the table; a Spin has an empty rectangle
    int contentHeight = 0;  // pixels: client height that fits the taller page, with the bottom margin
};

// The rectangle of every control for a client area `clientWidth` pixels wide. Pure: the same input gives the same
// output, and the height of the window follows from the table, not from a constant.
inline Result compute(const Control* t, std::size_t n, int clientWidth, const Metrics& m) {
    Result res;
    res.rect.assign(n, Rect{});
    const int mx = dlu_x(kMargin, m);
    const int top = dlu_y(kMargin, m);
    for (std::size_t i = 0; i < n; ++i)
        if (t[i].kind == Kind::Tab)
            res.rect[i] = Rect{mx, top, std::max(clientWidth - 2 * mx, 0), m.tabStrip};
    const int pagesTop = top + m.tabStrip + dlu_y(kBlockGap, m);
    const int labelW = dlu_x(kLabelColumn, m);
    const int colGap = dlu_x(kColumnGap, m);
    int bottom = pagesTop;
    for (int page = 0; page < kPageCount; ++page) {
        int lastRow = -1;
        for (std::size_t i = 0; i < n; ++i)
            if (t[i].page == page)
                lastRow = std::max(lastRow, t[i].row);
        int y = pagesTop;
        bool inGroup = false;
        std::size_t groupIdx = 0;
        int groupEnd = -1, groupTop = 0;
        for (int row = 0; row <= lastRow; ++row) {
            for (std::size_t i = 0; i < n; ++i) {
                if (t[i].page == page && t[i].kind == Kind::Group && t[i].row == row) {
                    inGroup = true;
                    groupIdx = i;
                    groupEnd = row + t[i].rowSpan - 1;
                    groupTop = y;
                    y += dlu_y(kGroupTop, m);
                }
            }
            int rowH = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (t[i].page == page && t[i].row == row && t[i].kind != Kind::Group)
                    rowH = std::max(rowH, dlu_y(cell_dlu(t[i].kind), m));
            const int left = inGroup ? mx + dlu_x(kInnerPad, m) : mx;
            const int width = std::max(clientWidth - 2 * left, 0);
            for (std::size_t i = 0; i < n; ++i) {
                if (t[i].page != page || t[i].row != row || t[i].kind == Kind::Group || t[i].kind == Kind::Spin)
                    continue;
                Rect r;
                if (t[i].colSpan >= 2) {
                    r.x = left;
                    r.w = width;
                } else if (t[i].col == 0) {
                    r.x = left;
                    r.w = std::min(labelW, width);
                } else {
                    r.x = left + labelW + colGap;
                    r.w = std::max(width - labelW - colGap, 0);
                }
                const int h = dlu_y(cell_dlu(t[i].kind), m);
                r.y = y + (rowH - h) / 2; // centred in the row: a label sits level with the field next to it
                r.h = t[i].kind == Kind::Combo ? dlu_y(cell_dlu(Kind::Combo) + kDropDown, m) : h;
                res.rect[i] = r;
            }
            y += rowH;
            if (inGroup && row == groupEnd) {
                y += dlu_y(kGroupBottom, m);
                res.rect[groupIdx] = Rect{mx, groupTop, std::max(clientWidth - 2 * mx, 0), y - groupTop};
                inGroup = false;
                y += dlu_y(kBlockGap, m);
            } else {
                y += dlu_y(inGroup ? kRowGap : kBlockGap, m);
            }
        }
        bottom = std::max(bottom, y - dlu_y(kBlockGap, m) + dlu_y(kMargin, m));
    }
    res.contentHeight = bottom;
    return res;
}

} // namespace layout

} // namespace ddr5::gui
