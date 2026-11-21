#pragma once
// gui_text.h - every user-visible string of the GUI in one place, plus the UTF-8 / UTF-16 helpers.
// This header has no Win32 types, so it can be included from the pure logic (gui_model.h) on every platform; the
// conversions are implemented in gui_text.cpp.
#include <string>
#include "types.h"

namespace ddr5::gui {

// ---- names that are not shown to the user ----
inline constexpr const wchar_t* kWindowClass = L"Ddr5RamTestWindow";
// Global\ so that a copy in another user session (fast user switching, RDP) counts too
inline constexpr const wchar_t* kOnceMutex = L"Global\\Ddr5RamTestSingleInstance";
inline constexpr const wchar_t* kLogFileName = L"ddr5ramtest.log";

// ---- window and boxes ----
inline constexpr const wchar_t* kAppTitle = L"DDR5 RAM Test";
inline constexpr const wchar_t* kAlreadyRunning =
    L"DDR5 RAM Test is already running (in another user session, or as another user).";
inline constexpr const wchar_t* kWindowFailed = L"The window could not be created.";
inline constexpr const wchar_t* kCouldNotStartPrefix = L"The test could not start (";
inline constexpr const wchar_t* kCouldNotStartSuffix = L").";
inline constexpr const wchar_t* kCouldNotStartPlain = L"The test could not start.";

// ---- tabs, groups and labels ----
inline constexpr const wchar_t* kTabTesting = L"Testing";
inline constexpr const wchar_t* kTabAdvanced = L"Advanced";
inline constexpr const wchar_t* kGroupSystem = L"System status";
inline constexpr const wchar_t* kGroupSettings = L"Test settings";
inline constexpr const wchar_t* kGroupStatus = L"Test status";
inline constexpr const wchar_t* kGroupAdvanced = L"Advanced";
inline constexpr const wchar_t* kGroupExperimental = L"Experimental";
inline constexpr const wchar_t* kLabelTotal = L"Total memory:";
inline constexpr const wchar_t* kLabelFree = L"Free memory:";
inline constexpr const wchar_t* kLabelMegabytes = L"Megabytes:";
inline constexpr const wchar_t* kLabelThreads = L"Threads:";
inline constexpr const wchar_t* kLabelDuration = L"Duration:";
inline constexpr const wchar_t* kLabelPages = L"Pages verified:";
inline constexpr const wchar_t* kLabelErrors = L"Error count:";
inline constexpr const wchar_t* kLabelCache = L"CPU cache:";
inline constexpr const wchar_t* kLabelRng = L"RNG:";
inline constexpr const wchar_t* kLabelSet = L"Test set:";
inline constexpr const wchar_t* kCheckBeep = L"Beep on error";
inline constexpr const wchar_t* kCheckStopOnError = L"Stop on error";
inline constexpr const wchar_t* kCheckFpu = L"Stress FPU";
inline constexpr const wchar_t* kAdvancedHelp =
    L"CPU cache: Default follows each test; Bypass reads straight from DRAM (clflush every access); Cached uses "
    L"normal write-back traffic; Non-temporal uses streaming stores.\n\n"
    L"RNG: data generator for hash tests.\n\nTest set: Quick runs a shorter subset.\n\nStress FPU adds an AVX load "
    L"on every thread (more power draw).";

// Entries of the Advanced lists. The order is the order of the enums CacheMode, Rng and SuiteSet.
inline constexpr const wchar_t* kCacheItems[] = {L"Default", L"Bypass (flush)", L"Cached", L"Non-temporal"};
inline constexpr const wchar_t* kRngItems[] = {L"Default", L"SplitMix64", L"Murmur3 mix", L"Xorshift64*"};
inline constexpr const wchar_t* kSetItems[] = {L"Full", L"Quick"};

// ---- the Start button and the status line ----
inline constexpr const wchar_t* kStart = L"Start";
inline constexpr const wchar_t* kStop = L"Stop";
inline constexpr const wchar_t* kForceQuit = L"Force quit";
inline constexpr const wchar_t* kStopping = L"Stopping...";
inline constexpr const wchar_t* kStoppingThenClosing = L"Stopping, then closing...";
inline constexpr const wchar_t* kStuck = L"Stuck? Force quit ends the program";
inline constexpr const wchar_t* kAllocating = L"Allocating memory...";
inline constexpr const wchar_t* kFinished = L"Finished";
inline constexpr const wchar_t* kStopped = L"Stopped";
inline constexpr const wchar_t* kCouldNotRun = L"Could not run";
inline constexpr const wchar_t* kStoppedWithErrors = L"Stopped (errors found)";
inline constexpr const wchar_t* kErrorsSeeLog = L"Errors found - see ddr5ramtest.log";
inline constexpr const wchar_t* kErrorsLogInTemp = L"Errors found - log is in %TEMP%";
inline constexpr const wchar_t* kErrorsNoLog = L"Errors found (no log file could be written)";
inline constexpr const wchar_t* kErrorsLogIncomplete =
    L"Errors found - the log file could not be written completely (disk full?)";
inline constexpr const wchar_t* kZeroDuration = L"0:00:00:00";
inline constexpr const wchar_t* kZeroPercent = L"0 %";
inline constexpr const wchar_t* kZeroPercentDecimal = L"0.0 %";
inline constexpr const wchar_t* kZero = L"0";

// ---- helpers (gui_text.cpp) ----
// Engine text (ASCII or UTF-8 in a std::string) as UTF-16. Never widen a byte at a time.
std::wstring utf8_to_wide(const std::string& s);
// Text of a std::exception (the system's ANSI code page) as UTF-16 for a message box.
std::wstring ansi_to_wide(const std::string& s);
// "d:hh:mm:ss" for a run time in seconds.
std::wstring duration_text(u64 seconds);

} // namespace ddr5::gui
