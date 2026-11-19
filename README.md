# ddr5ramtest

A DDR5 memory tester that runs inside the operating system. It fills a large region of RAM with data, reads it back
and reports every line that does not match, with the failing DQ lanes and bits. It ships as a console program
(`ddr5test_cli`, Linux and Windows) and a Windows GUI (`ddr5ramtest.exe`) on the same engine.

**Status.** The engine is checked by unit tests and by fault injection (faults planted in software, see
[Tests](#tests)). It has **not yet been validated against a known-bad DIMM**; see `FIXES.md` item 7. Until a case is
recorded here, treat a clean result as "no errors found by this tool", not as proof that the memory is good.

## What it tests

- A built-in suite of 28 tests (`ddr5test_cli --list`; `Q` marks the ones in the `--quick` set). They cover:
  - data patterns: solid, checkerboards, walking bits, stressapptest-style patterns, per-DQ-lane styles (SSO,
    crosstalk, pulse, skew, PRBS), hash and address-in-address data;
  - access orders: sequential, reverse, random, bit-reversed, Gray, butterfly and power-of-2 strides, so every
    bank, bank group and row bit is toggled;
  - march tests (MATS+, March C-, PMOVI and others), bus-turnaround and write/read adjacency timing, non-temporal
    and SSE2 accesses, idle gaps between write and verify, and disturb traffic on a neighbouring window;
  - pointer chasing, bulk copy engines, evolving data, page shuffle, read/write storms and cache coherency.
- Accesses are 64-byte lines (one DDR5 burst). By default most tests flush the line (`CLFLUSHOPT`, or `CLFLUSH`) after
  each access so the next read comes from DRAM, not from the CPU cache. `--cache` and `--flush` change this.
- Every run picks a random seed and prints it. Pass it back with `--seed` (and the same `-mb` and `-t`) to repeat a run.

## What it does not test

- **It runs under an OS with the caches on.** Memory that the OS, the firmware or other programs hold is not tested;
  by default the tool allocates 90 % of the free memory. A boot-time tester such as MemTest86 can reach more of it.
- **Addresses are virtual.** The tool does not know which physical row, bank or DIMM a page lives on.
- **It is not a retention-time test.** The idle-gap tests (seconds) check that data survives with refresh running.
- **It sees only what the CPU sees.** Errors that ECC corrects before the data reaches the CPU are not reported.
- **Read/write storm tests are not a clean data-race model** (see [Why the storm tests race](#why-the-storm-tests-race)).

## Requirements

- x86-64 CPU with AVX2 (checked at start; the program exits with code 2 and a message if it is missing).
- Linux: Clang with C++20 (CI uses `clang++` from Ubuntu 24.04) and CMake 3.20+. Windows: Visual Studio 2022 (MSVC) and CMake.
  Clang and MSVC are the only supported compilers; GCC is not built or tested.

## Build

Linux:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --target ddr5test_cli unit_tests -j
# the executables are in build/ (change it with -DDDR5_OUTPUT_DIR=...)
```

Windows (from an "x64 Native Tools Command Prompt for VS"):

```bat
build.bat
```

This builds `build\ddr5ramtest.exe` (GUI), `build\ddr5test_cli.exe` and `build\unit_tests.exe`. Set
`DDR5_WARNINGS_AS_ERRORS=ON` first to build with `/WX`.

CMake options: `DDR5_SANITIZE` (`thread` or `address,undefined`, Clang), `DDR5_CHECK_SYNC=ON` (check that all
workers make the same number of barrier calls), `DDR5_VERSION` (override the version text).

## Run

```sh
./build/ddr5test_cli                      # all tests, 90 % of free memory, all logical CPUs, one loop
./build/ddr5test_cli --quick -mb 4096     # the quick set on a 4 GiB region
./build/ddr5test_cli --coverage 100       # run until every page has been verified
./build/ddr5test_cli --minutes 30 -l 0    # keep looping for 30 minutes
./build/ddr5test_cli -f "Page shuffle"    # only tests whose name contains the filter
```

Run `ddr5test_cli --help` for every option. The most used:

| Option | Meaning |
| --- | --- |
| `-mb MB` | Region size; `0` or omitted = 90 % of free memory. A requested size is capped to leave memory for the OS. |
| `-t N` | Worker threads; default is every logical CPU the process may use. |
| `-l N` | Loops over the suite (`0` = until another limit stops it). |
| `-f TEXT` | Run only tests whose name contains `TEXT`. `--list` shows the names. |
| `--quick` | The quick subset (no long pauses). |
| `--coverage PCT` | Stop when `PCT` percent (0 to 100) of the pages have been verified. |
| `--minutes N`, `--test-seconds N` | Time limit for the run, time budget per test. |
| `--seed N` | Repeat a run with a given seed. |
| `--stop-on-error` / `--keep-going` | Stop at the first error, or carry on (default for the CLI). |
| `--allow-unlocked` | Allow a run whose memory could not be locked (see below). |
| `--no-large-pages` | Do not ask for large pages. |
| `--log FILE` | Log file (default `ddr5test.log` in the current directory). |

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Finished, no errors. This includes a run that ended at a `--coverage` target or `--minutes` limit you asked for. |
| 1 | Memory errors found. This wins over every other outcome. |
| 2 | Could not start: bad option, log file that cannot be opened, not enough memory, or memory that cannot be locked. |
| 3 | Internal error. |
| 4 | Stopped early (Ctrl-C, or Stop in the GUI) before the run finished, no errors found so far. The summary says `INCOMPLETE`. |

A first Ctrl-C ends the current step and prints the summary; a second one ends the process at once.

### What "coverage" means

Coverage is the share of the tested region's **virtual 4 KiB pages** that were completely verified at least once. A
page counts only after the tool compared all of its 64 lines. Coverage below 100 % after every test ran is not an
error: some tests verify only part of the region on purpose. The summary prints the figure and the number of passes
(`total verified passes`).

### Lock the memory

The tester locks the whole region into RAM (`mlock` on Linux, `VirtualLock` on Windows). Memory that the OS can
swap out may be written to disk and read back, which hides or causes errors and makes a clean result meaningless.

- On Linux the tool tries to raise `RLIMIT_MEMLOCK` to the region size. A non-root user can raise only the soft limit,
  up to the hard limit. If the hard limit is too small, raise it (for example in `/etc/security/limits.conf`, or run
  with `CAP_IPC_LOCK`/`CAP_SYS_RESOURCE`) or lower `-mb`.
- If the memory still cannot be locked, the run is refused with exit code 2 and a message. `--allow-unlocked` lets it
  go on; the run header then says `memory NOT locked`.

Before you trust a result, check that the header says `memory locked`.

### Output

The console shows the header (version, seed, thread count, size, whether the memory is locked), one line per test and
a summary. `ddr5test.log` (or `--log FILE`) records every bad line (`ERROR ...`, up to 200 lines per run; after that
only the counters grow) with the address, the expected and the actual data, the failing DQ lanes and bits, and whether
a re-read gave the same wrong data.

## Why the storm tests race

The four "Read/write storm" tests run a writer thread and a reader thread on the same memory at the same time. The
writer rewrites a chunk with the **same data it already holds**, and the reader verifies that chunk against the value
it should have. A correct memory returns the expected value whether the read comes before or after a rewrite, so any
mismatch is a real error. The accesses are plain, unsynchronised loads and stores to the same address, which is a data
race in C++ terms: the goal is concurrent traffic on the same rows, not a race-free program. ThreadSanitizer reports
it, so `tools/selftest.sh` skips these tests when run with `SAN=thread`. It is deliberate, not a suppression.

## Tests

```sh
cmake --build build/cmake --target unit_tests && build/unit_tests   # pure logic, no memory stress
bash tools/selftest.sh                                              # Linux: full self-test
SAN=address,undefined bash tools/selftest.sh                        # the same under ASan + UBSan
SAN=thread bash tools/selftest.sh                                   # the same under TSan
ctest --output-on-failure                                           # from the build directory
```

`tools/selftest.sh` builds the tester and runs the unit tests, a clean run of every test group, option checking,
seed reproduction, log-file and CPU-restriction behaviour, memory-lock refusal, **fault injection** (the
`ddr5test_cli_inject` build plants known faults and the self-test checks that each one is found), a coverage audit and
a stop (SIGINT) check in every test. It needs `clang++` (set `CXX` to use another one) and an AVX2 CPU, and takes a while.

The memory-lock checks are skipped when the process ignores `RLIMIT_MEMLOCK` (root or `CAP_IPC_LOCK`).

## Formatting and static analysis

```sh
bash tools/format.sh           # format the sources with clang-format (config: .clang-format)
bash tools/format.sh --check   # fail if any file is not formatted
clang-tidy -p build/cmake src/*.cpp   # config: .clang-tidy (bugprone, performance, clang-analyzer)
```

CI (`.github/workflows/ci.yml`) runs the build, the unit tests, the format check and the self-test on every push and
pull request.

## Extra algorithms

`src/suite_data.cpp` defines the built-in suite in code; there is no config file. Two algorithms are not used by any
built-in test but are available to add to it: `hammer` (n-sided row hammer / activation-rate stress, keys `rowstride`,
`aggressors`, `acts`, `rounds`) and `probe` (row-hit versus row-conflict latency by address distance; informational,
not pass or fail; keys `samples`, `stride`). Edit the suite, rebuild, and run `unit_tests` (`test_extra_algos` covers
both).

## Layout

| Path | Contents |
| --- | --- |
| `src/` | Engine (`algo_*.cpp`, `run.h`, `suite*.cpp`, `platform_*.cpp`) and the two front-ends (`main_cli.cpp`, `main_gui.cpp`). |
| `tools/unit_tests.cpp` | Deterministic unit tests. |
| `tools/selftest.sh`, `tools/inject_checks.sh` | Linux self-test and the portable fault-injection checks. |
| `tools/format.sh` | clang-format wrapper. |
| `build.bat` | Windows build. |
| `FIXES.md` | Open review findings. |
