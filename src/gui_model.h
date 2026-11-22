#pragma once
// gui_model.h - plain data and rules of the GUI: what the form holds, how it becomes engine Settings, and which text
// a finished run shows. No Win32 types and no Win32 calls, so this header compiles (and can be unit tested) on every
// platform. The window code (gui_window.cpp) reads the controls into these structs and shows what comes back.
#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
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

// The form as the window read it: the raw text of the two number fields (nothing is parsed or clamped by the window),
// the Megabytes field's origin, and the selected index of each list.
struct FormValues {
    std::wstring threadsText;
    std::wstring megabytesText;
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

inline constexpr u64 kMinMegabytes = 64; // the least a typed size may be, and the floor of the automatic one
// The strings in gui_text.h name these numbers; this keeps the two from drifting apart.
static_assert(sizing::kAutoPercent == 90 && sizing::kAutoFloor == 512 * sizing::kMB && kMinMegabytes == 64,
              "the texts of the Megabytes hints (gui_text.h) name these numbers");

// ~90% of free RAM (sizing.h), rounded down to a multiple of two pieces per thread, at least 64 MB.
inline u64 auto_megabytes(int threads, u64 availPhys, u64 availCommit) {
    const u64 want = sizing::plan(0, availPhys, availCommit).bytes >> 20;
    const u64 step = static_cast<u64>(std::max(1, threads)) * 2;
    return std::max<u64>(want / step * step, kMinMegabytes);
}

// What the automatic memory size comes from, for the line under the Megabytes field.
enum class AutoSource {
    PercentOfFree,  // 90 % of the free memory
    FreeMinusFloor, // the free memory minus 512 MB (when that is less than 90 %)
    CommitLimit,    // the page file / commit limit is the smaller one
    Minimum         // almost nothing is free: the 64 MB floor
};

inline AutoSource auto_megabytes_source(int threads, const SystemInfo& sys) {
    const sizing::Plan p = sizing::plan(0, sys.availPhys, sys.availCommit);
    const u64 step = static_cast<u64>(std::max(1, threads)) * 2;
    if ((p.bytes >> 20) / step * step < kMinMegabytes)
        return AutoSource::Minimum;
    if (p.limited == sizing::Limit::Commit)
        return AutoSource::CommitLimit;
    const u64 percent = sys.availPhys / 100 * sizing::kAutoPercent;
    return percent <= sizing::minus_floor(sys.availPhys, sizing::kAutoFloor) ? AutoSource::PercentOfFree
                                                                             : AutoSource::FreeMinusFloor;
}

// ---- the number fields: parse, check, say what is wrong ----

enum class NumberKind {
    Empty,      // nothing, or only blanks
    Ok,         // digits only, fits in 64 bits
    NotANumber, // anything else: a letter, a sign, a decimal point, a blank inside ("12 34")
    TooBig      // digits only but more than 64 bits
};

struct ParsedNumber {
    NumberKind kind = NumberKind::Empty;
    u64 value = 0; // Ok: the number; TooBig: the largest u64
};

// The text of a field as a number. Only the digits 0-9 count (no sign, no thousands separator, no other script's
// digits), blanks at either end are ignored. Nothing is clamped: a number that does not fit says so.
inline ParsedNumber parse_number(std::wstring_view text) {
    const auto blank = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    while (!text.empty() && blank(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && blank(text.back()))
        text.remove_suffix(1);
    ParsedNumber r;
    if (text.empty())
        return r;
    std::string digits;
    digits.reserve(text.size());
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            r.kind = NumberKind::NotANumber;
            return r;
        }
        digits.push_back(static_cast<char>(c));
    }
    u64 v = 0;
    const char* const last = digits.data() + digits.size();
    const std::from_chars_result res = std::from_chars(digits.data(), last, v);
    if (res.ec == std::errc::result_out_of_range) {
        r.kind = NumberKind::TooBig;
        r.value = std::numeric_limits<u64>::max();
    } else if (res.ec != std::errc() || res.ptr != last) {
        r.kind = NumberKind::NotANumber;
    } else {
        r.kind = NumberKind::Ok;
        r.value = v;
    }
    return r;
}

enum class FieldState {
    Automatic, // empty (or filled in by the program): the program picks the value
    Valid,     // a typed number inside the range
    NotANumber,
    TooSmall,
    TooLarge
};

struct FieldCheck {
    FieldState state = FieldState::Automatic;
    u64 value = 0; // Valid: the typed number
    u64 min = 0;   // the range a typed number must be in
    u64 max = 0;
    bool ok() const { return state == FieldState::Automatic || state == FieldState::Valid; }
};

// The most megabytes a typed size may be: what a run would be given (sizing::plan caps a request at a share of the free
// memory and of the commit limit), and never more than the machine has. At least kMinMegabytes.
inline u64 max_manual_megabytes(const SystemInfo& sys) {
    u64 cap = sizing::plan(std::numeric_limits<u64>::max(), sys.availPhys, sys.availCommit).bytes >> 20;
    if (sys.availPhys == 0 && sys.availCommit == 0) // plan() capped nothing
        cap = sys.totalMB;
    else if (sys.totalMB)
        cap = std::min(cap, sys.totalMB);
    return std::max(cap, kMinMegabytes);
}

// A typed number against [lo, hi]. Empty is the caller's business (it is Automatic here).
inline FieldCheck check_range(std::wstring_view text, u64 lo, u64 hi) {
    FieldCheck c;
    c.min = lo;
    c.max = hi;
    const ParsedNumber p = parse_number(text);
    switch (p.kind) {
    case NumberKind::Empty:
        c.state = FieldState::Automatic;
        break;
    case NumberKind::NotANumber:
        c.state = FieldState::NotANumber;
        break;
    case NumberKind::TooBig:
        c.state = FieldState::TooLarge;
        break;
    case NumberKind::Ok:
        c.value = p.value;
        c.state = p.value < lo ? FieldState::TooSmall : p.value > hi ? FieldState::TooLarge : FieldState::Valid;
        break;
    }
    return c;
}

// Threads: 1 up to the CPUs this process may use. A larger number is an error, not a silent change.
inline FieldCheck check_threads(std::wstring_view text, int cpus) {
    return check_range(text, 1, static_cast<u64>(std::max(1, cpus)));
}

// Megabytes: kMinMegabytes up to max_manual_megabytes(). `autoFilled`: the program wrote the text (the automatic
// size), so it is not input.
inline FieldCheck check_megabytes(std::wstring_view text, bool autoFilled, const SystemInfo& sys) {
    FieldCheck c = check_range(text, kMinMegabytes, max_manual_megabytes(sys));
    if (autoFilled) {
        c.state = FieldState::Automatic;
        c.value = 0;
    }
    return c;
}

// The whole form checked. runThreads and runMegabytes are what a run would use; a field that is not ok counts as
// Automatic there, but a run must not start then (the window keeps Start disabled).
struct FormCheck {
    FieldCheck megabytes;
    FieldCheck threads;
    int runThreads = 1;
    u64 runMegabytes = kMinMegabytes;
    AutoSource autoSource = AutoSource::Minimum;
    bool ok() const { return megabytes.ok() && threads.ok(); }
};

inline FormCheck validate_form(const FormValues& f, const SystemInfo& sys) {
    FormCheck c;
    c.threads = check_threads(f.threadsText, sys.cpus);
    c.megabytes = check_megabytes(f.megabytesText, f.megabytesAuto, sys);
    c.runThreads = c.threads.state == FieldState::Valid ? static_cast<int>(c.threads.value) : std::max(1, sys.cpus);
    c.runMegabytes = c.megabytes.state == FieldState::Valid
                         ? c.megabytes.value
                         : auto_megabytes(c.runThreads, sys.availPhys, sys.availCommit);
    c.autoSource = auto_megabytes_source(c.runThreads, sys);
    return c;
}

// The line under a field: what the value is, or what is wrong and which range is valid.
inline std::wstring range_hint(const FieldCheck& f, const wchar_t* validSuffix) {
    const std::wstring lo = std::to_wstring(f.min), hi = std::to_wstring(f.max);
    switch (f.state) {
    case FieldState::NotANumber:
        return kHintWholeNumber + lo + kHintTo + hi + kHintEnd;
    case FieldState::TooSmall:
        return kHintTooSmall + lo + kHintEnd;
    case FieldState::TooLarge:
        return kHintTooLarge + hi + kHintEnd;
    case FieldState::Automatic:
    case FieldState::Valid:
        break;
    }
    return lo + kHintTo + hi + validSuffix;
}

inline std::wstring megabytes_hint(const FormCheck& c) {
    if (c.megabytes.state != FieldState::Automatic)
        return range_hint(c.megabytes, kHintManualMb);
    switch (c.autoSource) {
    case AutoSource::PercentOfFree:
        return kHintAutoPercent;
    case AutoSource::FreeMinusFloor:
        return kHintAutoFloor;
    case AutoSource::CommitLimit:
        return kHintAutoCommit;
    case AutoSource::Minimum:
        break;
    }
    return kHintAutoMinimum;
}

inline std::wstring threads_hint(const FormCheck& c) {
    if (c.threads.state == FieldState::Automatic)
        return kHintAutoThreadsPre + std::to_wstring(c.runThreads) + kHintAutoThreadsPost;
    return range_hint(c.threads, kHintEnd);
}

// The engine settings for the form. The caller starts a run only when validate_form(f, sys).ok(); a field that is not
// ok counts as automatic here. The GUI keeps its old behaviour for the options it has no control for yet: large pages
// on, unlocked memory allowed, run until stopped (see FIXES.md #1 and #11).
inline Settings settings_from_form(const FormValues& f, const SystemInfo& sys) {
    const FormCheck c = validate_form(f, sys);
    Settings s;
    s.threads = c.runThreads;
    s.megabytes = c.runMegabytes;
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
