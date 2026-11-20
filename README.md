# ddr5ramtest

A DDR5 memory tester that runs inside the operating system. It fills a large region of RAM with test data, reads it
back and reports every mismatching 64-byte line together with the failing DQ lanes and bits. One engine drives a
console program (`ddr5test_cli`, Linux and Windows) and a Windows GUI (`ddr5ramtest.exe`).

## What it does

- Runs a built-in suite of 28 tests (`ddr5test_cli --list`; `Q` marks the `--quick` set):
  - data patterns: solid, checkerboards, walking bits, stressapptest-style patterns, per-DQ-lane styles (SSO,
    crosstalk, pulse, skew, PRBS), hash and address-in-address data;
  - access orders: sequential, reverse, random, bit-reversed, Gray, butterfly and power-of-2 strides, toggling every
    bank, bank group and row bit;
  - march tests (MATS+, March C-, PMOVI and others), bus-turnaround and write/read adjacency timing, non-temporal and
    SSE2 accesses, idle gaps between write and verify, and disturb traffic on a neighbouring window;
  - pointer chasing, bulk copy engines, evolving data, page shuffle, read/write storms and cache coherency.
- Flushes each line (`CLFLUSHOPT`, or `CLFLUSH`) after access so reads come from DRAM; `--cache` and `--flush`
  change this.
- Uses all logical CPUs and, by default, 90 % of free memory, locked into RAM.
- Prints a random seed for every run; `--seed` with the same `-mb` and `-t` repeats it.
- Logs each bad line with its address, expected and actual data, failing DQ lanes and bits, and whether a re-read
  returned the same wrong data.

## Requirements

- x86-64 CPU with AVX2.
- Clang (C++20) and CMake 3.20+ on both systems. Windows: `clang-cl` and `lld-link` from LLVM, plus the Windows SDK and
  C++ runtime headers/libraries that Visual Studio 2022 (or its Build Tools) installs. The MSVC compiler (`cl.exe`) is not used.

## Build

Linux:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --target ddr5test_cli unit_tests -j
```

Executables go to `build/` (change with `-DDDR5_OUTPUT_DIR=...`).

Windows (from an "x64 Native Tools Command Prompt for VS", which sets `INCLUDE` and `LIB`; LLVM must be on `PATH`):

```bat
build.bat
```

This builds `build\ddr5ramtest.exe` (GUI), `build\ddr5test_cli.exe` and `build\unit_tests.exe`. Set
`DDR5_WARNINGS_AS_ERRORS=ON` first to build with `/WX`.

CMake options: `DDR5_SANITIZE` (`thread` or `address,undefined`), `DDR5_CHECK_SYNC=ON` (check that all workers make
the same number of barrier calls), `DDR5_VERSION` (override the version text).

## Run

```sh
./build/ddr5test_cli                      # all tests, one loop
./build/ddr5test_cli --quick -mb 4096     # the quick set on a 4 GiB region
./build/ddr5test_cli --coverage 100       # run until every page has been verified
./build/ddr5test_cli --minutes 30 -l 0    # loop for 30 minutes
./build/ddr5test_cli -f "Page shuffle"    # only tests whose name contains the filter
```

| Option | Meaning |
| --- | --- |
| `-mb MB` | Region size; `0` or omitted = 90 % of free memory. |
| `-t N` | Worker threads; default is every logical CPU. |
| `-l N` | Loops over the suite (`0` = until another limit stops it). |
| `-f TEXT` | Run only tests whose name contains `TEXT`. |
| `--quick` | The quick subset. |
| `--coverage PCT` | Stop when `PCT` percent of the pages have been verified. |
| `--minutes N`, `--test-seconds N` | Time limit for the run, time budget per test. |
| `--seed N` | Repeat a run with a given seed. |
| `--stop-on-error` / `--keep-going` | Stop at the first error, or carry on (CLI default). |
| `--allow-unlocked` | Continue when the memory cannot be locked. |
| `--no-large-pages` | Do not ask for large pages. |
| `--log FILE` | Log file (default `ddr5test.log`). |
| `--max-log N` | Error blocks written to the console and log (default 200, `0` = unlimited). Later errors are only counted; a note says so once. |

`ddr5test_cli --help` lists every option.

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Finished, no errors (including a `--coverage` or `--minutes` limit you set). |
| 1 | Memory errors found. |
| 2 | Could not start: bad option, unopenable log file, not enough memory, or memory that cannot be locked. |
| 3 | Internal error. |
| 4 | Stopped early (Ctrl-C, or Stop in the GUI) with no errors so far; the summary says `INCOMPLETE`. |

The first Ctrl-C ends the current step and prints the summary; a second one exits at once.

### Coverage

Coverage is the share of the region's virtual 4 KiB pages whose 64 lines were all verified at least once. The summary
prints it with the number of verified passes (`total verified passes`).

### Memory locking

The region is locked into RAM (`mlock` on Linux, `VirtualLock` on Windows) so that every read and write reaches
memory. On Linux the tool raises `RLIMIT_MEMLOCK` to the region size, up to the hard limit; raise the hard limit
(for example in `/etc/security/limits.conf`, or run with `CAP_IPC_LOCK`/`CAP_SYS_RESOURCE`) or lower `-mb` if needed.
If locking fails the run exits with code 2; `--allow-unlocked` continues anyway and the header shows
`memory NOT locked`. A normal run's header shows `memory locked`.

### Output

The console shows a header (version, seed, thread count, size, lock state), one line per test and a summary. The log
file records up to 200 `ERROR` lines per run; after that only the counters grow.

## Read/write storm tests

The four "Read/write storm" tests run a writer and a reader on the same memory at once. The writer rewrites a chunk
with the data it already holds and the reader verifies that chunk, so any mismatch is a real error whether the read
lands before or after a rewrite. The loads and stores are plain and unsynchronised on purpose, to produce concurrent
traffic on the same rows. ThreadSanitizer flags them, so `tools/selftest.sh` skips these tests under `SAN=thread`.

## Tests

```sh
cmake --build build/cmake --target unit_tests && build/unit_tests   # unit tests
bash tools/selftest.sh                                              # Linux: full self-test
SAN=address,undefined bash tools/selftest.sh                        # the same under ASan + UBSan
SAN=thread bash tools/selftest.sh                                   # the same under TSan
ctest --output-on-failure                                           # from the build directory
```

`tools/selftest.sh` builds the tester and runs the unit tests, a clean run of every test group, option checks, seed
reproduction, log-file and CPU-restriction behaviour, memory-lock refusal, **fault injection** (the
`ddr5test_cli_inject` build plants known faults and the self-test checks that each one is found), a coverage audit and
a SIGINT stop check in every test. Set `CXX` to choose the compiler (default `clang++`).

## Formatting and static analysis

```sh
bash tools/format.sh           # format with clang-format (config: .clang-format)
bash tools/format.sh --check   # fail if any file is not formatted
clang-tidy -p build/cmake src/*.cpp   # config: .clang-tidy
```

CI (`.github/workflows/ci.yml`) runs the format check, clang-tidy, the Linux and Windows (clang-cl) builds, both with Clang, with
unit tests, and the self-test on every push and pull request.

## Extra algorithms

`src/suite_data.cpp` defines the built-in suite. Two further algorithms can be added to it: `hammer` (n-sided row
hammer, keys `rowstride`, `aggressors`, `acts`, `rounds`) and `probe` (row-hit versus row-conflict latency by address
distance, informational; keys `samples`, `stride`). Edit the suite, rebuild and run `unit_tests` (`test_extra_algos`
covers both).

## Layout

| Path | Contents |
| --- | --- |
| `src/` | Engine (`algo_*.cpp`, `run.h`, `suite*.cpp`, `platform_*.cpp`) and front-ends (`main_cli.cpp`, `main_gui.cpp`). |
| `tools/unit_tests.cpp` | Unit tests. |
| `tools/selftest.sh`, `tools/inject_checks.sh` | Linux self-test and fault-injection checks. |
| `tools/format.sh` | clang-format wrapper. |
| `build.bat` | Windows build. |
| `FIXES.md` | Open review findings. |
