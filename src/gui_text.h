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
inline constexpr const wchar_t* kUnlockedRefused =
    L"The memory could not be locked into RAM, so the system may swap parts of it out and a clean result would prove "
    L"little. Lower Megabytes, or tick \"Allow unlocked memory\" on the Advanced tab to run anyway.";

// ---- tabs, groups and labels ----
// An & in a caption marks its Alt+letter key (a label moves focus to the field after it). Each letter is used once
// in the window: S (Start/Stop), M, R, B, E on the Testing page; C, G, T, F, U on the Advanced page; P, N, V, W on the
// Limits page; Q (Force quit).
inline constexpr const wchar_t* kTabTesting = L"Testing";
inline constexpr const wchar_t* kTabAdvanced = L"Advanced";
inline constexpr const wchar_t* kTabLimits = L"Limits";
inline constexpr const wchar_t* kTabLimitsError = L"Limits (error)"; // a field on that page needs fixing
inline constexpr const wchar_t* kGroupSystem = L"System status";
inline constexpr const wchar_t* kGroupSettings = L"Test settings";
inline constexpr const wchar_t* kGroupStatus = L"Test status";
inline constexpr const wchar_t* kGroupAdvanced = L"Advanced";
inline constexpr const wchar_t* kGroupExperimental = L"Experimental";
inline constexpr const wchar_t* kGroupMemory = L"Memory";
inline constexpr const wchar_t* kGroupLimits = L"Stop by itself";
inline constexpr const wchar_t* kLabelTotal = L"Total memory:";
inline constexpr const wchar_t* kLabelFree = L"Free memory:";
inline constexpr const wchar_t* kLabelMegabytes = L"&Megabytes:";
inline constexpr const wchar_t* kLabelThreads = L"Th&reads:";
inline constexpr const wchar_t* kLabelDuration = L"Duration:";
inline constexpr const wchar_t* kLabelPages = L"Pages verified:";
inline constexpr const wchar_t* kLabelErrors = L"Error count:";
inline constexpr const wchar_t* kLabelCache = L"CPU &cache:";
inline constexpr const wchar_t* kLabelRng = L"RN&G:";
inline constexpr const wchar_t* kLabelSet = L"&Test set:";
inline constexpr const wchar_t* kCheckBeep = L"&Beep on error";
inline constexpr const wchar_t* kCheckStopOnError = L"Stop on &error";
inline constexpr const wchar_t* kCheckFpu = L"Stress &FPU";
inline constexpr const wchar_t* kCheckUnlocked = L"Allow &unlocked memory";
inline constexpr const wchar_t* kUnlockedWarning =
    L"Memory that can be swapped out makes a clean result prove little. Leave this off unless a run will not start.";
inline constexpr const wchar_t* kLabelLoops = L"Loo&ps:";
inline constexpr const wchar_t* kLabelMinutes = L"Mi&nutes:";
inline constexpr const wchar_t* kLabelCoverage = L"Co&verage %:";
inline constexpr const wchar_t* kLabelMaxLog = L"Errors &written:";
inline constexpr const wchar_t* kAdvancedHelp =
    L"CPU cache: Default follows each test; Bypass reads straight from DRAM (clflush every access); Cached uses "
    L"normal write-back traffic; Non-temporal uses streaming stores.\n\n"
    L"RNG: data generator for hash tests.\n\nTest set: Quick runs a shorter subset.\n\nStress FPU adds an AVX load "
    L"on every thread (more power draw).";

// Entries of the Advanced lists. The order is the order of the enums CacheMode, Rng and SuiteSet.
inline constexpr const wchar_t* kCacheItems[] = {L"Default", L"Bypass (flush)", L"Cached", L"Non-temporal"};
inline constexpr const wchar_t* kRngItems[] = {L"Default", L"SplitMix64", L"Murmur3 mix", L"Xorshift64*"};
inline constexpr const wchar_t* kSetItems[] = {L"Full", L"Quick"};

// ---- messages under the number fields (gui_model.h puts the numbers in) ----
// Each is a line under its field, so it names no field and stays short: the line is one row of the window.
inline constexpr const wchar_t* kHintWholeNumber = L"Whole number, ";
inline constexpr const wchar_t* kHintTo = L" to ";
inline constexpr const wchar_t* kHintTooSmall = L"Too small: the least is ";
inline constexpr const wchar_t* kHintTooLarge = L"Too large: the most is ";
inline constexpr const wchar_t* kHintEnd = L".";
inline constexpr const wchar_t* kHintManualMb = L" MB.";
inline constexpr const wchar_t* kHintAutoPercent = L"Automatic: 90 % of free memory.";
inline constexpr const wchar_t* kHintAutoFloor = L"Automatic: free memory minus 512 MB.";
inline constexpr const wchar_t* kHintAutoCommit = L"Automatic: limited by the page file.";
inline constexpr const wchar_t* kHintAutoMinimum = L"Automatic: the minimum, 64 MB.";
inline constexpr const wchar_t* kNameLoops = L"Loops: ";
inline constexpr const wchar_t* kNameMinutes = L"Minutes: ";
inline constexpr const wchar_t* kNameCoverage = L"Coverage: ";
inline constexpr const wchar_t* kNameMaxLog = L"Errors written: ";
inline constexpr const wchar_t* kHintLimitsOk =
    L"Empty or 0 means no limit. For Errors written, empty means 200 and 0 means all.";
inline constexpr const wchar_t* kHintAutoThreadsPre = L"Automatic: ";
inline constexpr const wchar_t* kHintAutoThreadsPost = L" (one per usable CPU).";

// ---- the Start button and the status line ----
inline constexpr const wchar_t* kStart = L"&Start";
inline constexpr const wchar_t* kStop = L"&Stop";
inline constexpr const wchar_t* kForceQuit = L"Force &quit";
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
