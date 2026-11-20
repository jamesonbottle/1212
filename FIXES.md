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
- [x] Decide the policy: a distinct exit code for an interrupted run, and/or an "INCOMPLETE" label in the summary.
      **Decided: both.** Exit 4 and `stopped after Ns: INCOMPLETE, no errors found so far` for a stop nobody asked for
      in advance (Ctrl-C, GUI Stop). Errors found still give 1. A `--coverage` target or `--minutes` limit that ends
      the run is deliberate: exit 0, `finished in`, with the reason. Coverage below 100% after every test ran is
      not "incomplete" (some tests verify no region pages by design); the summary shows the coverage.
- [x] Update `tools/selftest.sh` section 4 and the exit-code text in `usage()` to match.

**Done when.** The exit code or the summary line tells a finished run from a stopped one.

---

## 4. Missing README, formatter config, linter config and CI  [medium for trust]

**Problem.** The repo root has no README, no `.clang-format` (although `tools/format.sh` says its config is
`.clang-format`), no `.clang-tidy`, and no CI config (no `.github/` or other CI file is tracked). `build.bat` and a
comment at `src/main_gui.cpp:145` refer to a GitHub Actions job and a Windows runner, so a CI setup may exist
outside this repo; if it does, commit it. Either way, nothing in the repo runs the unit tests and
`tools/selftest.sh`.

**Fix.**
- [x] README: what the tool tests and does not test, how to build, how to run, exit codes, what "coverage" means
      (virtual 4 KiB pages), the need to lock memory, and why the read/write storm tests race by design.
- [x] Commit a `.clang-format` that matches the current style, so `tools/format.sh --check` works.
- [x] Add a `.clang-tidy` (start with `bugprone-*`, `performance-*`, `clang-analyzer-*`).
- [x] CI that builds, runs `unit_tests`, and runs `tools/selftest.sh` (also with `SAN=address,undefined` and
      `SAN=thread`). `.github/workflows/ci.yml`; not yet run on GitHub, and its Windows job was never run
      (no Windows toolchain where it was written). clang-tidy is report-only: it lists findings, it does not fail.

**Done when.** A new clone builds from the README alone, and CI is green on a pull request.

---

## 5. Turn on stricter warnings, at least for the hot-path headers  [low]

**Problem.** The build uses only `-Wall -Wextra` (`CMakeLists.txt:65`, `:72`). With `-Wconversion
-Wsign-conversion` there are about 500 warnings. Almost all are small non-negative `int` values converted to
`u64`, plus `u64` to `double` in printing code. None changes a value on real input, but they hide any future
real one.

**Fix.**
- [x] Add `-Wconversion -Wsign-conversion -Wshadow` to `ddr5_flags` and fix the warnings, starting with
      `src/lineio.h`, `src/gen.h`, `src/coverage.h`, `src/order.h`, `src/run.h`. (`DDR5_WARN_FLAGS` in
      `CMakeLists.txt`, also used for the `ddr5_cpucheck` libraries. Clang 18 reported 65 distinct warnings, in 16
      files: 457 sign-conversion and 114 `u64` to `double` lines before de-duplication of the header repeats.
      No `-Wshadow` and no narrowing hit.)
- [x] Use `u64` (or `size_t`) loop counters where the value indexes arrays or multiplies by `kLineBytes`.
      `kLinesPerPage` is now `u64`; `u64` to `double` conversions in printing code are explicit
      (`gib()` in `src/suite.cpp`).
- [x] Keep `/W4` on MSVC and add the matching conversion warnings there. `/w44365` adds C4365 (signed/unsigned);
      C4244/C4267 are already on at `/W4`. Not compiled with MSVC (no Windows toolchain), so the Windows CI job
      is the first check.

**Done when.** The strict build is warning-free, or each remaining warning has a one-line justification.
**Result.** Clang 18 strict build: 0 warnings. `unit_tests` and `tools/selftest.sh` pass (the selftest also fails
on any warning in its own build). One fix was a real, if harmless, sign issue: `sa_flags = SA_RESETHAND` in
`src/platform_posix.cpp` stored an unsigned sign-bit constant in an `int`; now an explicit cast with a comment.
The `SAN=address,undefined` and `SAN=thread` selftests were not run (no sanitizer runtime in the review
environment); their compile step is warning-free.

---

## 6. Check read-after-flush ordering on real hardware  [unverified, needs a decision]

**Problem.** `IO::store` and `IO::verify` (`src/lineio.h:13-72`) wrap `CLFLUSH`/`CLFLUSHOPT` with only a compiler
barrier (`CBAR()`), no `MFENCE`. Write-to-verify phases are fine because the algorithms use `sfence` plus a
barrier sync between them. Inside one march element with several ops on the same line (for example `r0 w1 r1`,
`src/algo_march.cpp:208-221`) the read after the flush is not fenced. It is not confirmed that this read always
comes from DRAM and not from cache.

**Fix.**
- [x] Measure it: time the read after the flush on real hardware, with and without an `MFENCE`.
      `tools/flush_order_bench.cpp` (`cmake --build <dir> --target flush_order_bench`, not in the default build)
      times a dependent chain of `store; flush; [fence]; load` on a 256 MB region against a cache-hit and a DRAM
      reference. Run on an Intel Xeon @ 2.30 GHz (a cloud VM, 2 vCPUs, DRAM generation unknown; two runs, ns per
      step, best of 5):

      | sequence                          | CLFLUSH | CLFLUSHOPT |
      |-----------------------------------|---------|------------|
      | HIT: store; load, 4 KiB set       | 14      | 14         |
      | DRAM: plain load, big set         | 183-200 | 143-185    |
      | store; flush; load (as `IO::store`) | 246-248 | **34-39** |
      | store; flush; sfence; load        | 245-248 | 248-304    |
      | store; flush; mfence; load        | 561-633 | 637-657    |
      | store; load, no flush, big set    | 34      | 37         |
      | store NT; load (with or without sfence) | 567-653 | 660-766 |

      **Result.** With CLFLUSHOPT (what `--flush auto` picks on any CPU that has it) the unfenced read after
      the flush costs the same as a cache hit: it is answered from the cache or the store buffer, not from DRAM.
      With CLFLUSH, and with an `sfence`/`mfence` after CLFLUSHOPT, the read costs a DRAM access. Non-temporal
      stores are not affected.
- [x] If the read can hit the cache, add `_mm_mfence()` between the flush and the next access to the same line in
      the cache-bypass path, or document the limit. `src/algo_march.cpp` now issues `_mm_mfence()` before every op
      group that reads lines an earlier op of the same element touched, when flush is on (once per burst group, so
      per line only at `burst=1`). The other algorithms separate write and verify of a line with `sfence` and a
      barrier sync, and the read/write storm tests read another thread's chunk, so they need no fence;
      `src/lineio.h` now says so in a comment.

**Done when.** There is a measurement or a written statement of what the same-line reads guarantee.
**Guarantee now.** With flush on, a march read that follows an op on the same lines reads DRAM. Not covered: other
CPUs and real DDR5 hardware (the measurement is from one VM; rerun the benchmark on the target machine), and
whether `sfence` alone would be enough (it measured as DRAM here, but only `mfence` is architecturally guaranteed
to order a load after CLFLUSHOPT).

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
- [x] Log every dropped or malformed op the way bad elements are logged. (`bad march op 'w2' in element
      'u(r0,w2)' (use r0, r1, w0 or w1)`; a token shorter than 3 characters is now reported as a bad element too.)
- [x] Accept only `0` and `1` as the digit.
- [x] Unit test: malformed op strings give a message. (`w2`, `x1`, `w10`, `r`, and a bare `u` in
      `tools/unit_tests.cpp`.)

**Done when.** `bad_values()` reports a malformed march op.
**Result.** Done; `unit_tests` pass and the strict build has 0 warnings. The built-in suite is still clean.

---

## 9. Error log cap is fixed at 200 lines  [low]

**Problem.** `Cfg::maxLog = 200` (`src/params.h:77`) and there is no option to change it. After 200 logged errors
only counters grow, which is deliberate, but the user cannot raise it for a full capture.

**Fix.**
- [x] Add `--max-log N` (0 = unlimited) to the CLI and `usage()`. (`Settings::maxLog`, default 200, copied into
      `Cfg::maxLog`; also in the README option table.)
- [x] Say in the console and log when the cap was reached. (`Run::report` writes `NOTE: error log limit reached
      (N errors logged); further errors are only counted...` right after the last logged block, so once per run.)

**Done when.** The option works and the cap message appears once per run.
**Result.** Unit tests (cap 200, custom cap, 0 = unlimited, cap above the error count) and two new fault-injection
selftest checks (`--max-log 10` and `0` on the 42 planted lines). The GUI keeps the default of 200 (no option there).

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









