# Plan: rewrite the Windows GUI from scratch (plain Win32)

Status: **plan only, nothing implemented.** Same format as `FIXES.md`: points with a "done when" line.

Why: `src/main_gui.cpp` (about 700 lines) is one file that mixes layout, run control, logging and state in one global
`App`. It has the open problems of `FIXES.md` section 11 (shows "Finished" after an internal error, never shows the
tested size or the lock state, clamps input silently and parses it with `_wtoi`, widens text wrongly, is only
system-DPI aware, handles logoff and the second Ctrl-C weakly). It has also never been compiled or run by the person who
reviewed it.

Constraints:
- **Plain Win32 only.** `<windows.h>` and Common Controls 6. No Qt, wxWidgets, ImGui, WinUI or .NET, no new dependency.
- Windows 10 and 11 on x64. The tests need AVX2 anyway, so there is no reason to support older systems.
- The engine (`ddr5engine`, `Settings`, `run_suite`, `Run`) stays as it is. The GUI is a front-end only.
- MSVC (`cl.exe`) stays the Windows compiler.
- No Windows toolchain was available where this plan was written: every point below is first checked by the Windows CI
  job, and the GUI needs a manual run on a real machine before it counts as done.

## Rules for the whole rewrite

- [ ] **Unicode everywhere.** UTF-16 `W` APIs only; `UNICODE`/`_UNICODE` defined; no `A` calls except `MessageBoxA`-style
      cases that are removed. Engine text (UTF-8/ASCII `std::string`) is converted with `MultiByteToWideChar(CP_UTF8,...)`,
      never by `std::wstring(s.begin(), s.end())`.
- [ ] **Check every API result** that can fail (`CreateWindowExW`, `RegisterClassExW`, `CreateFontIndirectW`,
      `SetTimer`, file open) and show a clear message instead of a half-built window.
- [ ] **No raw globals for logic.** One `App` object owned by the window (stored with `SetWindowLongPtr(GWLP_USERDATA)`),
      pure logic in small testable units outside the Win32 code.
- [ ] **RAII for handles** (`HFONT`, `HANDLE`, `HMODULE`, `FILE*`) so no path leaks them.
- [ ] **Message-thread rule:** only the UI thread touches controls; the worker thread only posts a message.
- [ ] **No blocking on the UI thread.** No `join()` or wait inside a handler that the user can trigger while a run is
      active (the StopGate already exists for this).
- [ ] **Warnings stay on.** `/W4 /w44365 /WX` in CI as today, no `#pragma warning(disable)`.
- [ ] **One place for strings** (a table in one file) so wording is consistent and later translation is possible.

## 1. Split the code into files  [prerequisite]

**Problem.** Layout, run control, log file and state live in one file and one global.

**Fix.**
- [x] `gui_main.cpp`: `wWinMain`, single-instance check, AVX2 check, message loop only.
- [x] `gui_window.cpp/.h`: window class, `WndProc`, control creation, layout.
- [x] `gui_run.cpp/.h`: starts and stops the engine, owns `Run`, the worker thread and the log writer.
- [x] `gui_model.h`: plain data (form values, result text) with no Win32 types, so it can be unit tested. `UiState`
      comes with the state machine of point 7.
- [x] `gui_text.cpp/.h`: UTF-8 to UTF-16 helpers and all user-visible strings.
- [x] `gui_dpi.cpp/.h`: DPI helpers (point 4).
- [x] CMake: the GUI target lists these files; `app.rc` and `app.manifest` stay. `gui_run.cpp` is built as the
      `ddr5gui_logic` library on every platform, so the Linux (Clang) CI job compiles it too.
- [x] `gui_win.h` (not in the first list): the one header that includes `<windows.h>` (Unicode, `NOMINMAX`, lean).

**Done when.** The old `main_gui.cpp` is gone, the GUI builds, and no file mixes Win32 calls with run logic.

**Status: done.** Built and tested only by GitHub Actions (Clang on Linux, MSVC `/W4 /w44365 /WX` on Windows): format,
Linux build and unit tests, self-test (none, ASan+UBSan, TSan) and the Windows build all pass. The GUI has still not
been run on a real machine (see point 14). Behaviour is unchanged except: no `A` calls (`MessageBoxA` removed),
engine text goes through `MultiByteToWideChar`, failures to register the class, create the window or set the timer
are reported, fonts are deleted, and the planned memory and thread values are written back to the fields together
(one `setting` guard).

## 2. Entry point, manifest and single instance

**Fix.**
- [x] Use `wWinMain` (UTF-16 command line).
- [x] Manifest: `<dpiAwareness>PerMonitorV2</dpiAwareness>` plus `<dpiAware>true/pm</dpiAware>` fallback,
      `<longPathAware>`, UTF-8 `activeCodePage`, Common Controls 6, `supportedOS` for Windows 10/11 only (drop the old OS
      ids), `asInvoker`.
- [x] Keep the check for AVX2 first, before anything else runs.
- [x] Keep the single-instance mutex (`Global\...`), and bring the first window forward with a registered window message
      or `FindWindow` retry as today; handle `ERROR_ACCESS_DENIED` the same way.
- [x] `InitCommonControlsEx` with only the classes used.
- [x] Return a real exit code (0 normal, 1 window creation failed, 2 not supported CPU).

**Done when.** The exe starts only on Windows 10 or later, and a second start activates the first.

**Status: done.** Built and tested only by GitHub Actions (all jobs pass, including the Windows build with `/W4 /w44365
/WX`). The exe has still not been started on a real machine (point 14), so "starts only on Windows 10 or later" and
"a second start activates the first" are not yet confirmed there. What changed, and what was already in place from
point 1:
- `wWinMain`, the AVX2 check before anything else, the `Global\` mutex with the `ERROR_ACCESS_DENIED` case and the
  `FindWindow` retry were already there and are kept. The mutex handle is now closed by a guard instead of being leaked.
- Manifest: `PerMonitorV2` plus the `true/pm` fallback, `longPathAware`, UTF-8 `activeCodePage` added. `supportedOS` was
  already only the Windows 10/11 id; Common Controls 6 and `asInvoker` unchanged.
- `InitCommonControlsEx` already listed only the classes the window uses (tab, up-down, standard); its result is now
  checked and a failure is reported like a window failure.
- Exit codes are named constants (0, 1, 2); an error from `GetMessageW` returns 1 instead of an uninitialised value.
- Known interim effect (gone with point 4): the window was per-monitor DPI aware but laid out at the primary monitor's
  DPI, so dragging it to a monitor with another scaling showed it at the wrong size.


## 3. Window, layout and controls

**Fix.**
- [ ] Create controls from a **layout table** (id, type, text, row, column, span) instead of hand-written pixel numbers; one
      function computes rectangles from the client size, the DPI and the font metrics (not fixed pixels).
- [ ] Use **`SetWindowPos` with `DeferWindowPos`** for moving many controls at once (no flicker), and handle `WM_SIZE`
      even if the window is fixed-size today, so a resizable window is a small change later.
- [ ] Use the **message font** from `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS)` (usually Segoe UI 9 pt), not a
      hard-coded face name; one big font derived from it for the Start button.
- [ ] Z-order and tab order are set by **creation order plus `WS_TABSTOP`/`WS_GROUP`** and checked, not patched later
      (the old code needed `fix_z_order`).
- [ ] Keyboard: `IsDialogMessageW` for Tab/Shift-Tab/arrows, mnemonics (`&Start`), Enter starts, Esc does the same as the
      close rule, default button set with `BS_DEFPUSHBUTTON`.
- [ ] Group boxes only where they group; labels are `STATIC` with `SS_NOPREFIX` where text may contain `&`.
- [ ] Text that is updated often (time, pages, errors) goes through a **"set only if changed"** helper to avoid flicker.
- [ ] Do not paint by hand. Colours come from `GetSysColor`/`WM_CTLCOLOR*`; the red error count is the only custom colour.
      Respect Windows high-contrast mode (no custom colours in that case).
- [ ] Dark mode is **out of scope** for the first version (Win32 controls do not support it cleanly).

**Done when.** The window looks right at 100 %, 150 % and 200 % scaling, every control is reachable by keyboard, and
tab order follows the visual order.

## 4. DPI (per-monitor v2)

**Fix.**
- [x] Manifest value from point 2; at run time call `GetDpiForWindow`, not `GetDeviceCaps` once at start.
- [x] Handle **`WM_DPICHANGED`**: resize to the suggested rectangle, recreate fonts, re-run layout.
- [x] Every size comes from one `Scale(px)` helper that uses the *current* window DPI.
- [x] Use `AdjustWindowRectExForDpi` for the window frame.
- [x] Dialogs and message boxes are owned by the window so they open on the same monitor.

**Done when.** Dragging the window between a 100 % and a 150 % monitor keeps it sharp and correctly laid out.

**Status: done.** Built and tested only by GitHub Actions (format, Linux build and unit tests, self-test with none,
ASan+UBSan and TSan, and the Windows build with `/W4 /w44365 /WX`, which also links the GUI) and all of it passes. The
GUI has still not been run on a real machine, and the "done when" line (dragging between a 100 % and a 150 % monitor)
needs two monitors: it stays on the manual checklist of point 14. What changed:
- `gui_dpi.h/.cpp`: `system_dpi()` (`GetDeviceCaps`, read once) is gone. `window_dpi(HWND)` asks `GetDpiForWindow` each
  time; `scale_px` and `valid_dpi` are plain C++ in the header and unit tested (`test_gui_dpi`).
- `build_ui` reads the DPI from the window itself, so the first layout is right for the monitor the window starts on.
- `WM_DPICHANGED` (`on_dpi_changed`): new DPI, new fonts for every control (`apply_fonts`; the old fonts are deleted only
  after no control uses them), font units and tab strip measured again, the window resized for the layout at the new
  DPI with its top-left at the corner Windows suggests, then the layout is re-run. The size comes from the layout, not
  from the suggested rectangle, so the client area stays exactly what the layout needs when the font rounds differently.
- `fit_window`: `AdjustWindowRectExForDpi` for the frame; the first placement is centred in the work area of the
  monitor the window is on, not the primary screen.
- Message boxes made after the window exists already used the window as owner; the ones with no owner are shown before
  the window exists (startup failures, second instance) and cannot have one.
- The interim effect noted in point 2 (wrong size on a monitor with another scaling) is gone.
- Not done here, belongs to point 13: `WM_SETTINGCHANGE` for a changed text size does not re-create the fonts yet.

## 5. Input fields and validation

**Problem.** Today input is clamped silently and parsed with `_wtoi`/`_wtoi64`.

**Fix.**
- [x] Parse with `wcstoull`/`std::from_chars` into a checked result type: *empty*, *ok(value)*, *not a number*,
      *out of range*. No silent clamping.
- [x] On bad input: show a short message next to the field (an inline label, or a balloon tip via `EM_SHOWBALLOONTIP`),
      keep the user's text, disable Start until valid. Say what the valid range is.
- [x] Megabytes: empty means automatic (as today) and the field then shows the value actually planned, with the source of
      the limit ("90 % of free RAM"). Manual range 64 MB up to what `sizing::plan` allows.
- [x] Threads: 1 up to the number of logical CPUs the process may use (`plat::ncpu()`); a larger value is an error, not a
      silent change.
- [x] Use `ES_NUMBER` only as a help, not as validation (it still allows pasted text).
- [x] Keep the up/down controls (`UPDOWN_CLASSW`) with 32-bit ranges and acceleration.
- [x] The validation rules are a plain function in `gui_model.h` with unit tests in `tools/unit_tests.cpp`.

**Done when.** Typing `abc`, `0`, `-5`, a huge number, and a pasted `12 34` each give a clear message and never start a run.

**Status: done.** Built and tested only by GitHub Actions (format, clang-tidy report, Linux build and unit tests,
self-test with none, ASan+UBSan and TSan, and the Windows build with `/W4 /w44365 /WX`, which also links the GUI) and
all of it passed on commit 794c391. The GUI has still not been run on a real machine, so the "done when" line is proven
for the rules (unit tests, `test_gui_input`) but not yet by typing into the window; that stays on the manual checklist of
point 14. What changed:
- `gui_model.h`: `parse_number` takes digits only (no sign, no separator, no other script's digits; blanks at the ends are
  ignored) and returns Empty, Ok, NotANumber or TooBig. `check_threads` and `check_megabytes` give Automatic, Valid,
  NotANumber, TooSmall or TooLarge with the range. `validate_form` checks the whole form, and `settings_from_form` builds
  the engine `Settings` from it. The old `threads_for_auto`, `threads_from_form` and `megabytes_from_form` clamps are gone.
- Window: the text of a field is read whole (the old 31-character buffer cut a long paste silently, `_wtoi64` read
  `12 34` as 12). Two status lines under the fields (inline labels, not balloon tips) say what is wrong and the valid range,
  or for an automatic size where it comes from (90 % of free memory, free memory minus 512 MB, the page file, the 64 MB
  floor). The user's text is never changed. Start is disabled while a field is invalid and is checked again in
  `start_run`. Nothing is checked while a run is going (the fields are locked and the free memory is the run's own doing).
- Ranges: Threads is 1 to the usable CPUs (before: 0 or text that was no number meant every CPU, and anything above 256 was cut to 256). Megabytes is 64 to what `sizing::plan`
  allows for the free memory and commit limit right now, and never more than the machine's RAM (before: up to the total
  RAM, then cut by the engine). The up-down ranges follow these, set once when the window is built: changing them later
  could make the control rewrite what the user typed.
- Kept as they were: empty Threads means every usable CPU; empty or cleared Megabytes is refilled with the automatic size;
  `ES_NUMBER` stays as a typing help only.
- Layout: two new rows in the Testing page, so the window is taller.
- Free memory changes while the window is open, so a typed size that was valid can become too large (the line says so and
  Start is disabled) until the user lowers it.

## 6. Settings sent to the engine

**Fix.**
- [ ] Build `Settings` from validated form values in one function (`gui_model.h`), no Win32 calls inside, unit tested.
- [ ] Add the missing options the engine has and the GUI does not: **"Allow running with unlocked memory"** (default off,
      with a warning text; today the GUI forces it on), **maximum errors logged** (`maxLog`), **loops/time limit** and
      **coverage target** (the engine already supports them).
- [ ] Keep "Advanced" settings on a second tab; the defaults stay what they are today. The limits (loops, minutes,
      coverage target, errors written) go on a third tab, "Limits".
- The GUI **does not save its settings**: nothing is written to disk to remember the form between runs, so every start
  shows the defaults. (Decision: no settings file, no registry key. The log of point 10 is a separate matter.)

**Done when.** Every engine option that makes sense for a GUI is either shown or deliberately listed as CLI-only.
CLI-only: `-f` (test name filter), `--seed`, `--flush`, `--test-seconds`, `--log FILE` (the GUI has its own log, point
10), `--no-large-pages` (the GUI always asks for them; point 7 shows whether it got them), `--list`, `--version`.

## 7. Running the test: result, status and state machine

**Problem.** After an internal error the status says "Finished"; the GUI never shows the tested size or the lock state.

**Fix.**
- [ ] Make a small **state machine** in `gui_model.h`: `Idle -> Starting -> Running -> Stopping -> Done(result)`; every
      button, field and text is derived from the state in one function, not set in many places.
- [ ] The result comes from the **exit code of `run_suite`** and is shown exactly: 0 finished, 1 errors found,
      2 could not start, 3 internal error, 4 stopped. "Finished" is only shown for 0. A stop by the user is "Stopped",
      never "Finished".
- [ ] Add to the engine a small **result struct** (exit code, tested bytes, memory locked yes/no, large pages yes/no, last
      message) returned next to the exit code. The CLI can use it too. (Prerequisite change in `suite.cpp`/`suite.h`.)
- [ ] Show in the "Test status" box: **tested memory** (MB actually allocated, which can be less than requested),
      **locked / not locked**, **large pages**, current test name, time, pages verified, error count.
- [ ] Error text from the engine is shown in a **read-only multi-line edit control** (selectable, copyable), not only in a
      message box. A message box is used only for "could not start" so the user cannot miss it.
- [ ] Copy-to-clipboard button for the full result text.
- [ ] A thrown exception inside the worker is caught, becomes "Internal error" with the message, and never ends the
      process silently.
- [ ] Start button is a toggle (Start / Stop / Force quit) driven only by the state machine and the existing `StopGate`.

**Done when.** Each exit code 0 to 4 plus a thrown exception shows its own correct text, and the tested size and lock
state are always visible.

## 8. Worker thread and messages

**Fix.**
- [ ] One worker thread started with `std::thread` inside a `try`, exactly as the engine's `try_start` helper allows
      (a failed thread start is reported, not fatal). Always joined before `Run` is destroyed.
- [ ] The worker finishes by `PostMessageW(WM_APP_RUN_DONE)` with a pointer to a heap result struct (not a bare `int`), and
      the UI thread takes ownership. If the post fails, the result is not leaked.
- [ ] Progress is **polled** at 500 ms by `WM_TIMER` from `Run::status()` (already thread safe). Use a timer that is
      stopped while minimised if that saves CPU; the test itself must not be slowed by the GUI.
- [ ] The GUI thread does not hold the log mutex for long (copy the last line, release).
- [ ] The worker never touches a control.

**Done when.** Starting 100 runs in a row (start, stop) with a debug build shows no leak and no deadlock.

## 9. Stop, close, logoff, Ctrl-C

**Fix.**
- [ ] Keep `StopGate` (it already has unit tests); the GUI only reports events to it and carries out what it says.
- [ ] **`WM_CLOSE`** while running: first request a stop and show "Stopping, then closing...", then close when the run is
      done; after the delay offer **Force quit**, as today.
- [ ] **`WM_QUERYENDSESSION` / `WM_ENDSESSION`** (logoff, shutdown): request a stop, flush and close the log, then let
      Windows continue; do not block the shutdown for longer than a few seconds. Use `ShutdownBlockReasonCreate` only if a
      run is active and release it afterwards.
- [ ] **Second request while stopping** (second click, second close) never starts a second stop; it only moves the gate to
      the force-quit offer.
- [ ] `TerminateProcess` is the last resort and is only reached from the explicit Force quit.
- [ ] Console Ctrl-C does not apply to a `WIN32` GUI program, so no handler is needed (remove the idea from the old notes).

**Done when.** Closing the window, pressing Stop twice, logging off and killing from Task Manager during a run all leave
a complete log and no orphan process.

## 10. Log file

**Fix.**
- [ ] Log path: **`%LOCALAPPDATA%\ddr5ramtest\ddr5ramtest.log`**, not next to the exe (the exe may be in a read-only
      folder). Create the folder; fall back to `%TEMP%`, and say which one is used in the window.
- [ ] Open as UTF-8, append, and write the header line with the version, the settings and the date.
- [ ] Every write is checked; the first failure sets a **log failed** flag that the result text shows (as today).
- [ ] Flush per error block (so a crash loses nothing), but not per progress line.
- [ ] **Open log** and **Open log folder** buttons (`ShellExecuteW`, with the result checked).
- [ ] The log writer is a class with its own mutex and a unit test (open failure, write failure, fallback path).

**Done when.** A full disk, a read-only folder and a locked file each give a visible message and the run still works.

## 11. Memory, privileges and large pages

**Fix.**
- [ ] Show whether the process has **"Lock pages in memory"** (`SeLockMemoryPrivilege`) and explain how to get it, as a
      link-style label that opens a short help text. The engine already requests the privilege; the GUI only reports it.
- [ ] Do not ask for elevation. The manifest stays `asInvoker`; the text says what to do for large pages.
- [ ] Warn **before** starting when the planned size is more than the free RAM (uses `sizing::plan`), with the number.
- [ ] Display free/total memory with a timer (500 ms, "set only if changed").

**Done when.** A user without the privilege sees "large pages: no" and the reason before and after the run.

## 12. Accessibility and usability

**Fix.**
- [ ] Every control has an accessible name (label association by order, or `WM_GETOBJECT`-free approaches such as proper
      static label before edit); check with **Narrator** and **Accessibility Insights**.
- [ ] Text contrast follows system colours; the red error count is never the only signal (also a text "errors found").
- [ ] Tooltips (`TOOLTIPS_CLASS`) for every field with the unit and the allowed range.
- [ ] No fixed minimum font size; layout survives larger text size from Windows settings.
- [ ] Beep on error stays optional and uses `MessageBeep`; a flashing taskbar button (`FlashWindowEx`) when an error is
      found while the window is not active.
- [ ] Tray/notification is **not** added in the first version.

**Done when.** The whole window can be used with the keyboard only and with Narrator, and the texts read in a sensible
order.

## 13. Robustness against the system

**Fix.**
- [ ] Do not load DLLs by name from the current directory: `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` (already the
      case for `dwmapi`; keep it) and set `SetDefaultDllDirectories` early.
- [ ] Use `DwmSetWindowAttribute` through the import library where available, not a hand-made function pointer, unless a
      Windows 10 build needs the dynamic path (check the minimum build).
- [ ] The window title bar colour (maroon caption) is **optional polish**: if it fails it is ignored. Decide whether to keep
      it; custom caption colours can look wrong in high-contrast mode, so skip them there.
- [ ] Handle `WM_SETTINGCHANGE` (theme, text size) by re-creating fonts and re-running layout.
- [ ] Handle `WM_POWERBROADCAST`/sleep: warn in the result that the run was interrupted by sleep if the clock jumped.

**Done when.** None of these calls can fail the start of the program.

## 14. Tests

- [ ] **Pure logic unit tests** in `tools/unit_tests.cpp` (it already runs on Linux too): input validation, form to
      `Settings`, the state machine and what each state shows, UTF-8/UTF-16 conversion (non-ASCII text, an invalid
      sequence, an empty string), log writer failures. These files contain no Win32 headers, so
      they compile everywhere.
- [ ] **Windows CI job** builds the GUI and runs the unit tests with `/WX`.
- [ ] **Smoke test on the Windows runner:** start the GUI with a hidden command line switch (`--selftest`) that creates the
      window, runs a 5 second quick suite on 64 MB, checks the displayed result text through `GetWindowTextW`, and exits
      with a code. This also proves the tab control and the z-order problem of the old code is gone.
- [ ] **Manual checklist** (in the repo as `GUI_TEST.md`): 100/150/200 % scaling, two monitors with different scaling,
      keyboard only, Narrator, high contrast, Stop, close during a run, logoff during a run, read-only exe folder, full
      disk, no privilege, a run with injected errors.
- [ ] Sanitizers do not run for the Windows GUI; use **Application Verifier** and a debug-CRT leak check for the manual run.

**Done when.** CI runs the smoke test on every push, and the manual checklist has been done once on a real machine.

## 15. Build, packaging and docs

**Fix.**
- [ ] CMake target `ddr5ramtest` (WIN32, MSVC) lists the new files; link libraries are named in CMake only, not also with
      `#pragma comment(lib)`.
- [ ] `app.rc`: version resource as today, plus an **icon** (a real `.ico` instead of `IDI_APPLICATION`).
- [ ] README: a GUI section (what each field means, where the log is, how to get large pages, what exit/result texts mean)
      and a screenshot taken from CI.
- [ ] `FIXES.md` section 11 is ticked off point by point with the commit that did it.
- [ ] Release notes line: the log file location changed.

**Done when.** A fresh clone builds on Windows with `build.bat`, and the README matches the program.

## Suggested order

1. Result struct in the engine (point 7, prerequisite) and the pure-logic files (points 5, 6, 7 state machine, 14 unit tests).
   These need no Windows machine and can be done and tested on Linux first.
2. Split the files and the new entry point/manifest (points 1, 2).
3. Window, layout, DPI (points 3, 4).
4. Run control, stop/close/logoff, log (points 7 to 10).
5. Privileges, accessibility, system robustness (points 11 to 13).
6. Smoke test, manual checklist, docs (points 14, 15).

## Risks

- The GUI cannot be compiled where this plan was written; every step depends on the Windows CI job and needs several
  rounds. Keep the commits small.
- A full rewrite can lose behaviour that the old code got right by accident (the z-order fix, the delayed force-quit offer,
  the AVX2 check before anything else). Keep a list of those from the old file and check each one at the end.
- Per-monitor DPI and `WM_DPICHANGED` are the part most likely to need a real two-monitor machine.

## Not part of this plan

- Another UI toolkit, dark mode, translations, a tray icon, charts of the results.
- A separate worker process (see `WORKER_PROCESSES.md` on the `worker-processes-plan` branch). This plan keeps the engine
  in-process; the event/result struct of point 7 is the same prerequisite that plan needs.
