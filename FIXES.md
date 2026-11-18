# Fix list

Findings from the review of ddr5ramtest (commit fc389f0). Ordered by priority. Each point has its own
"done when" line so it can be checked off on its own.

---

## 1. Unlocked memory only produces a one-line warning  [medium]

**Problem.** If `mlock` fails, `plat::alloc` still returns the region (`src/platform_posix.cpp:110-125`) and the
run goes on. The only notice is one header line (`src/suite.cpp:358-362`). `print_final_summary`
(`src/suite.cpp:449`) never repeats it, so the summary can say "total coverage: 100.0% ... no errors" for a run
whose pages may have been swapped. Reproduced without `CAP_IPC_LOCK` and with a 64 KiB `RLIMIT_MEMLOCK`: run
completed, exit code 0. Nothing in the code calls `setrlimit`, so this is the default for a non-root Linux user.

**Fix.**
- [x] Before allocating, try to raise `RLIMIT_MEMLOCK` to the size needed (`setrlimit`).
- [x] If memory still cannot be locked, refuse to start (exit 2) unless the user passes an explicit
      `--allow-unlocked`.
- [ ] Repeat "memory NOT locked" in the final summary and in the log header when it was allowed.

**Done when.** A run without the memlock right either fails fast with a clear message, or finishes with a
summary line saying the memory was not locked.

---

## 2. `--coverage` has no range check and can overflow  [low]

**Problem.** `--coverage` accepts values up to 2^55 (`src/main_cli.cpp:112`). The stop test multiplies the value
by the page count (`src/run.h:249-250`). `--coverage 281474976710656` on a 256 MB region wraps to 0, and the run
stops at 0.0% coverage with "coverage target reached" and exit code 0 (reproduced).

**Fix.**
- [x] Reject values above 100 in the CLI (the usage text says `PCT`).
- [x] Make the comparison in `run.h` overflow-safe (use `__int128`/`mulhi64`, or divide instead of multiply).
- [x] Add a unit test for a huge value.

**Done when.** `--coverage 281474976710656` exits 2 with a message, and the stop logic has a test that cannot wrap.

---

## 3. An interrupted or under-covered run exits 0 with "no errors"  [low, deliberate today]

**Problem.** Ctrl-C after one second gives exit code 0 and "finished ... no errors" (`src/suite.cpp:204`,
`src/suite.cpp:449-452`). `tools/selftest.sh` asserts this on purpose. Automation cannot tell "passed" from
"stopped early".

**Fix.**
- [ ] Decide the policy: a distinct exit code for an interrupted run, and/or an "INCOMPLETE" label in the summary.
- [ ] Update `tools/selftest.sh` section 4 and the exit-code text in `usage()` to match.

**Done when.** The exit code or the summary line tells a finished run from a stopped one.

---

## 4. Missing README, formatter config, linter config and CI  [medium for trust]

**Problem.** The repo root has no README, no `.clang-format` (although `tools/format.sh` says its config is
`.clang-format`), no `.clang-tidy`, and no CI config (no `.github/` or other CI file is tracked). `build.bat` and a
comment at `src/main_gui.cpp:145` refer to a GitHub Actions job and a Windows runner, so a CI setup may exist
outside this repo; if it does, commit it. Either way, nothing in the repo runs the unit tests and
`tools/selftest.sh`.

**Fix.**
- [ ] README: what the tool tests and does not test, how to build, how to run, exit codes, what "coverage" means
      (virtual 4 KiB pages), the need to lock memory, and why the read/write storm tests race by design.
- [ ] Commit a `.clang-format` that matches the current style, so `tools/format.sh --check` works.
- [ ] Add a `.clang-tidy` (start with `bugprone-*`, `performance-*`, `clang-analyzer-*`).
- [ ] CI that builds, runs `unit_tests`, and runs `tools/selftest.sh` (also with `SAN=address,undefined` and
      `SAN=thread`).

**Done when.** A new clone builds from the README alone, and CI is green on a pull request.

---

## 5. Turn on stricter warnings, at least for the hot-path headers  [low]

**Problem.** The build uses only `-Wall -Wextra` (`CMakeLists.txt:65`, `:72`). With `-Wconversion
-Wsign-conversion` there are about 500 warnings. Almost all are small non-negative `int` values converted to
`u64`, plus `u64` to `double` in printing code. None changes a value on real input, but they hide any future
real one.

**Fix.**
- [ ] Add `-Wconversion -Wsign-conversion -Wshadow` to `ddr5_flags` and fix the warnings, starting with
      `src/lineio.h`, `src/gen.h`, `src/coverage.h`, `src/order.h`, `src/run.h`.
- [ ] Use `u64` (or `size_t`) loop counters where the value indexes arrays or multiplies by `kLineBytes`.
- [ ] Keep `/W4` on MSVC and add the matching conversion warnings there.

**Done when.** The strict build is warning-free, or each remaining warning has a one-line justification.

---

## 6. Check read-after-flush ordering on real hardware  [unverified, needs a decision]

**Problem.** `IO::store` and `IO::verify` (`src/lineio.h:13-72`) wrap `CLFLUSH`/`CLFLUSHOPT` with only a compiler
barrier (`CBAR()`), no `MFENCE`. Write-to-verify phases are fine because the algorithms use `sfence` plus a
barrier sync between them. Inside one march element with several ops on the same line (for example `r0 w1 r1`,
`src/algo_march.cpp:208-221`) the read after the flush is not fenced. It is not confirmed that this read always
comes from DRAM and not from cache.

**Fix.**
- [ ] Measure it: time the read after the flush on real hardware, with and without an `MFENCE`.
- [ ] If the read can hit the cache, add `_mm_mfence()` between the flush and the next access to the same line in
      the cache-bypass path, or document the limit.

**Done when.** There is a measurement or a written statement of what the same-line reads guarantee.

---

## 7. Prove it finds real bad memory  [validation, not code]

**Problem.** The fault-injection tests plant simulated faults in software. They do not show that the program
detects a failing DIMM.

**Do.**
- [ ] Run it on memory known to be bad (for example a kit pushed to unstable timings that fails MemTest86 or
      TestMem5) and confirm it reports errors.
- [ ] Compare how fast it finds them against a second tester.
- [ ] Record the results in the README.

**Done when.** There is at least one documented case where this tool caught real faults.

---

## 8. March parser accepts malformed ops silently  [low]

**Problem.** `parse_march` (`src/algo_march.cpp:65-67`) drops ops that are not exactly two characters, and takes
any character as the data digit (`op[1] - '0'`). Only the built-in suite feeds it today, but a typo in
`src/suite_data.cpp` would change a test with no message.

**Fix.**
- [ ] Log every dropped or malformed op the way bad elements are logged.
- [ ] Accept only `0` and `1` as the digit.
- [ ] Unit test: malformed op strings give a message.

**Done when.** `bad_values()` reports a malformed march op.

---

## 9. Error log cap is fixed at 200 lines  [low]

**Problem.** `Cfg::maxLog = 200` (`src/params.h:77`) and there is no option to change it. After 200 logged errors
only counters grow, which is deliberate, but the user cannot raise it for a full capture.

**Fix.**
- [ ] Add `--max-log N` (0 = unlimited) to the CLI and `usage()`.
- [ ] Say in the console and log when the cap was reached.

**Done when.** The option works and the cap message appears once per run.

---

## 10. `DDR5TEST_INJECT` is defined per library, not per executable  [low, fragile]

**Problem.** `CMakeLists.txt:133` sets `DDR5TEST_INJECT` as PRIVATE on `ddr5engine_inject`, while
`src/lineio.h:65` changes an inline function body based on it. Today `main_cli.cpp` does not include `lineio.h`,
so there is no mismatch. A future include of it from an executable's own source would give the same inline
function two different definitions (ODR violation).

**Fix.**
- [ ] Make the definition PUBLIC on the inject library so every target that links it gets it, or move the hook
      out of the inline function.
- [ ] Add a CMake comment or check explaining the rule.

**Done when.** No translation unit of `ddr5test_cli_inject` can see `lineio.h` without the define.

---

## 11. GUI problems

After an internal error the status line says **"Finished"** | medium |
The GUI never shows the memory size really tested or whether it was locked | medium |
Manual input is silently clamped, and parsed with `_wtoi`/`_wtoi64` | low |
Narrow-to-wide string widening is wrong for non-ASCII text | low |
DPI: system-aware only, blurry on mixed-DPI monitors | low |
Shutdown/logoff and the second Ctrl-C are not handled defensively | low |
Small cleanups (fonts, manifest OS list) | trivial |


## Not reviewed (still open)

- [ ] `src/inject.cpp` and `src/suite_data.cpp` were not read.
- [ ] `tools/selftest.sh` was not run in full (the unit tests and a set of short real runs were).
- [ ] The GUI and `platform_win.cpp` were read but never compiled or run (no Windows toolchain in the review
      environment).
- [ ] Nothing was tested on real DDR5 hardware with many cores.









