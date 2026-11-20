// flush_order_bench.cpp - does a read right after a flush of the same line come from DRAM? (FIXES.md #6)
//
// IO::store (flush = 1) is `store; CLFLUSH[OPT]` and IO::verify is `load; compare; CLFLUSH[OPT]`, with only a
// compiler barrier between the flush and the next access. This program times the load that follows a store +
// flush of the same line, with and without a fence in between, against two reference points:
//   HIT   a load the CPU can only answer from cache (small working set, no flush)
//   MISS  a load that must go to DRAM (large random working set, flush, MFENCE, then load)
// Every iteration is one dependent chain step (the next line index is computed from the value that was loaded
// back), so the time per step is the latency of the whole store -> flush -> load sequence, not a throughput.
//
// A result near HIT means the load was served from cache or from the store buffer (the flush did not take
// effect before the load); a result near or above MISS means it went to DRAM.
//
// Build: cmake --build <dir> --target flush_order_bench   Run: <dir>/flush_order_bench [MB] [steps]
#include <time.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "flush.h"
#include "lineio.h"

using namespace ddr5;

static double now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

enum class Fence { None, Sfence, Mfence };

template <Fence F>
static inline void fence() {
    if constexpr (F == Fence::Sfence)
        _mm_sfence();
    else if constexpr (F == Fence::Mfence)
        _mm_mfence();
}

// Dependent chain over `n` lines. NT = non-temporal store, FL = flush after the store, F = fence between the
// store (and flush) and the load.
template <int NT, int FL, Fence F>
static double run_chain(u8* mem, u64 n, u64 steps, u64& sink) {
    using st = IO<32, NT, FL>;
    using ld = IO<32, 0, 0>;
    u64 idx = 1;
    double t0 = now_ns();
    for (u64 i = 0; i < steps; i++) {
        u8* a = mem + (idx % n) * kLineBytes;
        Line l;
        for (u64 k = 0; k < 8; k++)
            l.w[k] = splitmix(idx + k);
        st::store(a, l);
        fence<F>();
        Line t;
        ld::load(a, t);
        idx = splitmix(t.w[0] ^ t.w[7]); // the next address depends on the loaded data
    }
    double t1 = now_ns();
    sink ^= idx;
    return (t1 - t0) / (double)steps;
}

// Plain dependent load over a region far larger than the cache: the latency of one DRAM read.
static double run_load_only(u8* mem, u64 n, u64 steps, u64& sink) {
    u64 idx = 1;
    double t0 = now_ns();
    for (u64 i = 0; i < steps; i++) {
        Line t;
        IO<32, 0, 0>::load(mem + (idx % n) * kLineBytes, t);
        idx = splitmix(t.w[0] ^ idx);
    }
    double t1 = now_ns();
    sink ^= idx;
    return (t1 - t0) / (double)steps;
}

// A clean load that must miss: load a line, flush it, MFENCE, load it again (the second load is timed with the
// first as a fixed cost, so this reports the cost of the pair).
static double run_load_miss(u8* mem, u64 n, u64 steps, u64& sink) {
    u64 idx = 1;
    double t0 = now_ns();
    for (u64 i = 0; i < steps; i++) {
        u8* a = mem + (idx % n) * kLineBytes;
        Line t;
        IO<32, 0, 0>::load(a, t);
        flush_line(a);
        _mm_mfence();
        Line t2;
        IO<32, 0, 0>::load(a, t2);
        idx = splitmix(t.w[0] ^ t2.w[7] ^ idx);
    }
    double t1 = now_ns();
    sink ^= idx;
    return (t1 - t0) / (double)steps;
}

template <class F>
static double best_of(int reps, F fn) {
    double best = 1e30;
    for (int r = 0; r < reps; r++)
        best = std::min(best, fn());
    return best;
}

int main(int argc, char** argv) {
    const u64 mb = argc > 1 ? (u64)strtoull(argv[1], nullptr, 10) : 256;
    const u64 steps = argc > 2 ? (u64)strtoull(argv[2], nullptr, 10) : 1000000;
    const u64 bigLines = (mb << 20) / kLineBytes;
    const u64 smallLines = 64; // 4 KiB, stays in L1
    u8* mem = (u8*)aligned_alloc(4096, bigLines * kLineBytes);
    if (!mem) {
        std::fprintf(stderr, "cannot allocate %llu MB\n", (unsigned long long)mb);
        return 2;
    }
    std::memset(mem, 0x5A, bigLines * kLineBytes);
    u64 sink = 0;
    std::printf("region %llu MB, %llu steps per run, best of 5\n", (unsigned long long)mb, (unsigned long long)steps);
    std::printf("%-10s %-34s %10s\n", "flush", "sequence (ns per step)", "ns");
    for (FlushMode m : {FlushMode::ClFlush, FlushMode::ClFlushOpt}) {
        if (m == FlushMode::ClFlushOpt && !cpu_has_clflushopt())
            continue;
        set_flush_mode(m);
        const char* nm = flush_mode_name();
        auto row = [&](const char* what, double ns) { std::printf("%-10s %-34s %10.1f\n", nm, what, ns); };
        row("HIT   store; load (4 KiB set)", best_of(5, [&] { return run_chain<0, 0, Fence::None>(mem, smallLines, steps, sink); }));
        row("DRAM  load only (big set)", best_of(5, [&] { return run_load_only(mem, bigLines, steps, sink); }));
        row("MISS  load; flush; mfence; load", best_of(5, [&] { return run_load_miss(mem, bigLines, steps, sink); }));
        row("store; flush; mfence; load", best_of(5, [&] { return run_chain<0, 1, Fence::Mfence>(mem, bigLines, steps, sink); }));
        row("store; flush; sfence; load", best_of(5, [&] { return run_chain<0, 1, Fence::Sfence>(mem, bigLines, steps, sink); }));
        row("store; flush; load  (as IO::store)", best_of(5, [&] { return run_chain<0, 1, Fence::None>(mem, bigLines, steps, sink); }));
        row("store; load  (no flush, big set)", best_of(5, [&] { return run_chain<0, 0, Fence::None>(mem, bigLines, steps, sink); }));
        row("store NT; load  (no fence)", best_of(5, [&] { return run_chain<1, 0, Fence::None>(mem, bigLines, steps, sink); }));
        row("store NT; sfence; load", best_of(5, [&] { return run_chain<1, 0, Fence::Sfence>(mem, bigLines, steps, sink); }));
    }
    std::printf("(sink %llx)\n", (unsigned long long)sink);
    std::free(mem);
    return 0;
}
