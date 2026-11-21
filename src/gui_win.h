#pragma once
// gui_win.h - the one place that includes the Windows headers for the GUI (Unicode, no min/max macros, lean).
// Only the Win32 files of the GUI include this: gui_main.cpp, gui_window.cpp, gui_text.cpp, gui_dpi.cpp.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
