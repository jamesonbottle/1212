// platform_posix.cpp - Linux/POSIX implementation of platform.h.
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE // CPU_SET / pthread_setaffinity_np
#endif
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include "platform.h"

namespace ddr5 {

namespace plat {
static std::atomic<bool>* g_stop = nullptr;

static void ctrl(int) { // async-signal-safe: one atomic store
    if (g_stop)
        g_stop->store(true);
}
// First Ctrl-C: ask the run to stop (it ends the current test and prints the summary).
void install_ctrlc(std::atomic<bool>* s) {
    g_stop = s;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = ctrl;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = (int)SA_RESETHAND; // SA_RESETHAND is the sign bit: an unsigned constant stored in an int field
    sigaction(SIGINT, &sa, nullptr);
}
// The CPUs this process may run on (taskset, a cpuset or a container limit shrink it), read once
static const std::vector<int>& allowed_cpus() {
    static const std::vector<int> cpus = [] {
        std::vector<int> v;
        cpu_set_t s;
        CPU_ZERO(&s);
        if (sched_getaffinity(0, sizeof s, &s) == 0)
            for (int i = 0; i < CPU_SETSIZE; i++)
                if (CPU_ISSET(i, &s))
                    v.push_back(i);
        if (v.empty())
            for (int i = 0, n = (int)std::thread::hardware_concurrency(); i < n && i < CPU_SETSIZE; i++)
                v.push_back(i);
        return v;
    }();
    return cpus;
}
int ncpu() {
    return (int)allowed_cpus().size();
}
bool pin(int cpu) {
    const std::vector<int>& a = allowed_cpus();
    if (a.empty() || cpu < 0)
        return false;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(a[(size_t)cpu % a.size()], &s);
    return pthread_setaffinity_np(pthread_self(), sizeof(s), &s) == 0;
}
// "MemAvailable" in /proc/meminfo (kB): what a new allocation can get without swapping
static u64 mem_available() {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f)
        return 0;
    char line[256];
    u64 bytes = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long long kb;
        if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1) {
            bytes = (u64)kb * 1024;
            break;
        }
    }
    fclose(f);
    return bytes;
}
// pages * page size from sysconf; 0 (unknown) if either call fails
static u64 sysconf_bytes(int pagesName) {
    long pages = sysconf(pagesName), size = sysconf(_SC_PAGESIZE);
    return pages < 0 || size <= 0 ? 0 : (u64)pages * (u64)size;
}
u64 avail_phys() {
    u64 m = mem_available();
    return m ? m : sysconf_bytes(_SC_AVPHYS_PAGES); // old kernels have no MemAvailable
}
u64 total_phys() {
    return sysconf_bytes(_SC_PHYS_PAGES);
}
u64 avail_commit() {
    return 0;
}
void lower_priority() {}
KeepAwake keep_awake(bool) {
    return KeepAwake::NotSupported;
}
// Best effort: make RLIMIT_MEMLOCK at least `bytes`, so mlock of the whole region can succeed.
// A non-root user can raise the soft limit only up to the hard limit; root / CAP_SYS_RESOURCE can raise
// both. Returns true when the limit is already enough or was raised to `bytes`; on false, `detail` says
// what the limit is (for the "mlock failed" note).
static bool raise_memlock_limit(u64 bytes, std::string& detail) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_MEMLOCK, &rl) != 0)
        return true; // cannot tell: let mlock decide
    if (rl.rlim_cur == RLIM_INFINITY || (u64)rl.rlim_cur >= bytes)
        return true;
    const rlim_t want = (rlim_t)bytes;
    struct rlimit n = rl;
    n.rlim_cur = want;
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want)
        n.rlim_max = want; // needs CAP_SYS_RESOURCE; fails with EPERM otherwise
    if (setrlimit(RLIMIT_MEMLOCK, &n) == 0)
        return true;
    // Could not raise the hard limit: take what the hard limit allows
    n = rl;
    n.rlim_cur = rl.rlim_max;
    if (rl.rlim_max != rl.rlim_cur && setrlimit(RLIMIT_MEMLOCK, &n) == 0 &&
        (rl.rlim_max == RLIM_INFINITY || (u64)rl.rlim_max >= bytes))
        return true;
    getrlimit(RLIMIT_MEMLOCK, &rl);
    detail =
        "RLIMIT_MEMLOCK is " + std::to_string((u64)rl.rlim_cur >> 10) + " KB (hard " +
        (rl.rlim_max == RLIM_INFINITY ? std::string("unlimited") : std::to_string((u64)rl.rlim_max >> 10) + " KB") +
        "), needed " + std::to_string(bytes >> 10) + " KB and could not raise it";
    return false;
}
void* alloc(u64 bytes, bool, bool& got, bool& locked, std::string& note, const std::atomic<bool>* stop) {
    got = false;
    locked = false;
    std::string limitDetail;
    raise_memlock_limit(bytes, limitDetail);
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return nullptr;
    madvise(p, bytes, MADV_HUGEPAGE);
    int err = 0;
    const LockProgress lp = lock_in_slices((u8*)p, bytes, stop, kLockSlice, [&](u8* q, u64 n) {
        if (mlock(q, n) == 0)
            return true;
        err = errno;
        return false;
    });
    locked = !lp.failed && !lp.stopped;
    const std::string of =
        lp.done ? " after " + std::to_string(lp.done >> 20) + " of " + std::to_string(bytes >> 20) + " MB" : "";
    if (lp.failed)
        note = "mlock failed" + of + ": " + strerror(err) + " (see ulimit -l / RLIMIT_MEMLOCK" +
               (limitDetail.empty() ? "" : "; " + limitDetail) + ")";
    else if (lp.stopped)
        note = "stopped while locking the memory (" + std::to_string(lp.done >> 20) + " of " +
               std::to_string(bytes >> 20) + " MB locked)";
    return p;
}
void release(void* p, u64 b) {
    munmap(p, b);
}
} // namespace plat

} // namespace ddr5
#endif // !_WIN32
