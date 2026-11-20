# Plan: run the tests in a worker process

Status: **plan only, nothing implemented.** Written after the review in `FIXES.md`; same format (points with a
"done when" line).

Today the CLI and the GUI each run the whole engine inside their own process, with one `std::thread` per worker
(`Run::parallel`, `src/run.h`; the GUI runs the engine on one extra thread, `src/main_gui.cpp`). A hard crash or hang
caused by bad memory (or by the tester itself) therefore takes the front-end with it, and no summary, exit code or
"which test was running" message is produced.

The change: a small **supervisor** process (CLI or GUI front-end) starts one **worker** process that allocates and
locks the region, runs the suite and reports back over a pipe. If the worker dies or stops answering, the supervisor
says what happened and exits with a clear code.

---

## Decisions to make first

| # | Question | Recommendation |
|---|----------|----------------|
| D1 | One worker process, or several (each tests a slice, like HCI MemTest or `stress-ng`)? | **One.** The coherency, read/write storm and hammer tests need all threads in one address space on one region. Several processes would need shared memory and would change what those tests mean. |
| D2 | Is the worker the default, or opt-in? | Default for the CLI and the GUI, with `--in-process` to keep today's behaviour (debugging, unit tests, machines where a second process is a problem). |
| D3 | How does the worker start? | Re-run the same executable with a hidden `--worker` mode (settings passed as arguments, the CLI parser already exists). The GUI exe gets the same hidden mode, so no second exe has to ship next to it. |
| D4 | Does a worker crash count as "memory errors found" (exit 1) or as its own result? | **Its own code, 5**, with a message that a crash can be a sign of unstable memory but is not proof. Exit 1 stays "errors were verified and logged". |
| D5 | Who writes the log file? | The **supervisor**. The worker only sends error blocks over the pipe; data already in the pipe survives the worker's death. |

---

## Architecture

```
front-end (CLI / GUI)  ->  supervisor library  --pipe-->  worker process
 settings, display,         spawn, watchdog,               engine (ddr5engine, unchanged):
 exit code, log file        crash report, Ctrl-C           allocate, mlock, threads, tests
```

- The engine library (`ddr5engine`) keeps its tests and its thread model. Only how results leave the engine changes.
- Worker to supervisor: header text, progress (the same data as `Run::Status`), test start/end, one message per error
  block, final result. Supervisor to worker: stop, abort.
- The supervisor keeps the last known state (test name, loop, seed, coverage) so a crash report can name them.

---

## 1. Separate the engine from its output  [prerequisite, no processes yet]

**Problem.** The engine prints through `Logger` and the front-ends read `Run::status()` and the `run_suite` exit code.
There is no single description of "what a run reports", so a second process would have to copy that coupling.

**Fix.**
- [ ] Define the events a run produces: header lines, progress, test start/end, error block, final result.
- [ ] Define a final result struct: exit code, stop reason, tested bytes, memory locked or not, coverage, error totals.
      (The GUI needs the same data; this also covers the medium items of `FIXES.md` section 11.)
- [ ] CLI and GUI consume the events through one interface; the engine still runs in-process behind it.

**Done when.** CLI and GUI work as today, driven only by the event interface, and all tests pass.

---

## 2. Message protocol

**Fix.**
- [ ] A versioned, length-prefixed message format with an encoder and decoder in one place (no `printf` parsing).
- [ ] Reject a wrong protocol version, an oversized or truncated message, and garbage on the pipe, each with a clear error.
- [ ] Unit tests: round trip of every message, truncated and corrupted input.

**Done when.** The decoder cannot be made to read past a buffer, and bad input gives an error, never a crash.

---

## 3. Worker mode and POSIX supervisor

**Fix.**
- [ ] Hidden `--worker` mode: runs `run_suite` with the event interface writing to the pipe.
- [ ] Supervisor: create pipes, `fork`/`exec` the same binary, read events, `waitpid`, forward the first Ctrl-C as a
      "stop", kill the worker on the second.
- [ ] `RLIMIT_MEMLOCK`, `mlock` and large pages are per process: the worker does them (the supervisor needs no locked memory).
- [ ] Worker exits when the supervisor dies (`prctl(PR_SET_PDEATHSIG)` plus a check on the pipe), so a worker holding 90 % of
      the RAM is never left behind.
- [ ] Worker gets a higher `oom_score_adj`: if the system runs out of memory, the worker is killed, not the supervisor.

**Done when.** `ddr5test_cli` gives the same output, log and exit codes as today for every existing selftest case.

---

## 4. Crash and hang detection

**Fix.**
- [ ] Worker sends a progress heartbeat at least every second (the engine already ticks about every 250 ms).
- [ ] Supervisor kills a worker that is silent for N seconds (configurable, generous by default: a test can legitimately
      stall in `D(...)` delay elements) and reports a hang.
- [ ] On death by signal / exception code, print: signal or exit code, test name, loop, elapsed time, seed, coverage so far,
      and how to repeat the run (`--seed`).
- [ ] New exit code **5** (worker crashed or hung); `usage()` and README exit-code tables updated.
- [ ] Error blocks already received are written to the log before the crash message.

**Done when.** `kill -9` on the worker and a worker stuck in an endless loop each give exit 5 with a message naming the
test, and the log file contains every error block sent before the failure.

---

## 5. Windows supervisor

**Fix.**
- [ ] `CreateProcess` for the worker, anonymous pipes, and a Job Object with `KILL_ON_JOB_CLOSE` (the Windows counterpart
      of the parent-death rule).
- [ ] Stop request through the pipe; Ctrl-C and console-close handling in the supervisor.
- [ ] "Lock pages in memory" and large pages are checked in the worker's own token.
- [ ] Exception code (for example `0xC0000005`) shown in the crash report.

**Done when.** The Windows CI job builds it and runs the protocol and crash tests below. No Windows toolchain is
available where this plan was written, so the first check is CI.

---

## 6. GUI on the supervisor

**Fix.**
- [ ] The GUI starts a run through the supervisor and reads progress from messages instead of calling `Run::status()` from
      another thread.
- [ ] Show a crashed or hung worker as such, not "Finished" (see `FIXES.md` section 11).
- [ ] Show the tested size and whether the memory was locked, from the final result.
- [ ] Stop button sends the stop message; closing the window kills the worker.

**Done when.** A worker killed from outside shows a clear error in the GUI, and the GUI keeps running.

---

## 7. Tests

- [ ] Unit tests: protocol, supervisor state machine with a fake worker (crash, hang, silence, garbage, early exit).
- [ ] Fault-injection hooks next to the existing ones in `src/inject.cpp` (crash after N compares, hang in a test), in the
      inject build only.
- [ ] `tools/selftest.sh`: worker killed (exit 5), worker hung (exit 5), supervisor killed (no worker left running),
      Ctrl-C still exit 4 with INCOMPLETE, memlock refusal still exit 2, error log intact after a crash.
- [ ] `--in-process` runs the same selftests it runs today, so the old path stays covered.

---

## Build notes

- New library for the supervisor and protocol (platform files `supervisor_posix.cpp` / `supervisor_win.cpp`); the engine
  library stays as it is.
- The `DDR5TEST_INJECT` rule from `FIXES.md` section 10 applies: the worker linked into an inject program must use the
  inject engine, and a program never links both engines (CMake already stops that).

---

## Costs and risks

- More code and a second process to debug; `--in-process` keeps a simple path.
- The pipe adds a little latency to progress and error messages. The test threads never touch it, they only write to a
  buffer that one thread sends.
- Not every failure is caught: a machine that freezes or reboots on bad memory takes the supervisor too. The supervisor
  only helps when the operating system survives.
- The Windows side cannot be tested until CI runs it.

## Not part of this plan

- Splitting the region across several worker processes (see D1).
- Remote or network control of the worker.
- Changing the tests themselves or their thread model.
