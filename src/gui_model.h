#pragma once
// gui_model.h - plain data and rules of the GUI: what the form holds, how it becomes engine Settings, and which text
// a finished run shows. No Win32 types and no Win32 calls, so this header compiles (and can be unit tested) on every
// platform. The window code (gui_window.cpp) reads the controls into these structs and shows what comes back.
#include <algorithm>
#include "gui_text.h"
#include "sizing.h"
#include "suite.h"
#include "types.h"

namespace ddr5::gui {

// Where the log of a run went: next to the exe, the temp folder, or nowhere.
enum class LogPlace { Exe, Temp, None };

// What the machine offers right now.
struct SystemInfo {
    int cpus = 1;
    u64 totalMB = 0;
    u64 availPhys = 0;   // bytes
    u64 availCommit = 0; // bytes
};

// The form as the window read it: numbers are what the edit fields hold (0 = empty or not a number), lists are the
// selected index.
struct FormValues {
    long long threadsText = 0;
    long long megabytesText = 0;
    bool megabytesAuto = true; // the Megabytes field is empty or filled in by the program
    bool stopOnError = true;
    int cacheIndex = 0; // list order = enum order (gui_text.h)
    int rngIndex = 0;
    int suiteIndex = 0;
    bool stressFpu = false;
};

// What a finished run tells the status line.
struct DoneInfo {
    u64 errors = 0;
    bool logFailed = false; // a write to the log file failed (disk full...)
    bool stopRequested = false;
    LogPlace logPlace = LogPlace::Exe;
};

// ~90% of free RAM (sizing.h), rounded down to a multiple of two pieces per thread, at least 64 MB.
inline u64 auto_megabytes(int threads, u64 availPhys, u64 availCommit) {
    const u64 want = sizing::plan(0, availPhys, availCommit).bytes >> 20;
    const u64 step = static_cast<u64>(std::max(1, threads)) * 2;
    return std::max<u64>(want / step * step, 64);
}

// The thread count the automatic memory size is computed for: the typed one, or every usable CPU.
inline int threads_for_auto(long long typed, int cpus) {
    if (typed < 1)
        return std::max(1, cpus);
    return static_cast<int>(std::min<long long>(typed, 256));
}

inline int threads_from_form(const FormValues& f, int cpus) {
    u64 v = static_cast<u64>(f.threadsText);
    if (!v)
        v = static_cast<u64>(std::max(1, cpus));
    return static_cast<int>(std::min<u64>(std::max<u64>(v, 1), 256));
}

inline u64 megabytes_from_form(const FormValues& f, int threads, const SystemInfo& sys) {
    if (f.megabytesAuto || f.megabytesText <= 0)
        return auto_megabytes(threads, sys.availPhys, sys.availCommit);
    const u64 v = static_cast<u64>(f.megabytesText);
    return std::min(std::max<u64>(v, 64), std::max<u64>(sys.totalMB, 64));
}

// The engine settings for the form. The GUI keeps its old behaviour for the options it has no control for yet:
// large pages on, unlocked memory allowed, run until stopped (see FIXES.md #1 and #11).
inline Settings settings_from_form(const FormValues& f, const SystemInfo& sys) {
    Settings s;
    s.threads = threads_from_form(f, sys.cpus);
    s.megabytes = megabytes_from_form(f, s.threads, sys);
    s.stopOnError = f.stopOnError;
    s.cacheMode = static_cast<CacheMode>(std::max(0, f.cacheIndex));
    s.rngMode = static_cast<Rng>(std::max(0, f.rngIndex));
    s.suite = static_cast<SuiteSet>(std::max(0, f.suiteIndex));
    s.stressFpu = f.stressFpu;
    s.largePages = true;
    s.allowUnlocked = true;
    s.loops = 0;
    return s;
}

// What the status line says while a stop is under way.
inline const wchar_t* stopping_text(bool forceOffered, bool closing) {
    if (forceOffered)
        return kStuck;
    return closing ? kStoppingThenClosing : kStopping;
}

// The status text after a run ended, or nullptr when the line stays as it is.
inline const wchar_t* done_status_text(const DoneInfo& d) {
    if (d.errors > 0) {
        if (d.logFailed)
            return kErrorsLogIncomplete;
        if (d.stopRequested)
            return kStoppedWithErrors;
        switch (d.logPlace) {
        case LogPlace::Exe:
            return kErrorsSeeLog;
        case LogPlace::Temp:
            return kErrorsLogInTemp;
        case LogPlace::None:
            break;
        }
        return kErrorsNoLog;
    }
    return d.stopRequested ? kStopped : nullptr;
}

// run_suite() returned 2 or 3 (could not start / internal error); kExitIncomplete is a Stop and is not one of them.
inline bool exit_code_is_failure(int rc) {
    return rc == 2 || rc == 3;
}

} // namespace ddr5::gui
