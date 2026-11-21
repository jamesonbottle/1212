// gui_run.cpp - see gui_run.h. No Win32 calls in this file.
#include "gui_run.h"
#include <ctime>
#include <utility>
#include "build_version.h"
#include "start_guard.h"

namespace ddr5::gui {

namespace {

// The first line of every run in the log.
std::string header_line(const Settings& s) {
    std::tm lt{};
    const std::time_t now = std::time(nullptr);
#if defined(_WIN32)
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    std::string h;
    appendf(h,
            "---- DDR5 RAM Test run %04d-%02d-%02d %02d:%02d:%02d  %llu MB, %d threads, cache=%d rng=%d set=%d fpu=%d "
            "stop-on-error=%d, version %s ----\n",
            lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
            static_cast<unsigned long long>(s.megabytes), s.threads, static_cast<int>(s.cacheMode),
            static_cast<int>(s.rngMode), static_cast<int>(s.suite), static_cast<int>(s.stressFpu),
            static_cast<int>(s.stopOnError), DDR5_VERSION);
    return h;
}

} // namespace

GuiRun::~GuiRun() {
    if (worker_.joinable())
        worker_.join();
}

void GuiRun::open_log(const LogPaths& paths, const Settings& s) {
    std::lock_guard<std::mutex> l(logMu_);
    lastLine_.clear();
    logFailed_ = false;
    logPlace_ = LogPlace::Exe;
    if (log_.is_open())
        log_.close();
    log_.clear();
    log_.open(paths.primary, std::ios::app);
    if (!log_.is_open()) {
        log_.clear();
        if (!paths.fallback.empty()) {
            log_.open(paths.fallback, std::ios::app);
            logPlace_ = LogPlace::Temp;
        }
        if (!log_.is_open()) {
            log_.clear();
            logPlace_ = LogPlace::None;
        }
    }
    if (log_.is_open()) {
        log_ << header_line(s);
        log_.flush();
        if (!log_)
            logFailed_ = true;
    }
}

// The Logger's sink: the engine calls it from its worker threads.
void GuiRun::write_log(const std::string& text) {
    std::lock_guard<std::mutex> l(logMu_);
    if (log_.is_open()) {
        log_ << text;
        log_.flush();
        if (!log_)
            logFailed_ = true;
    }
    const size_t e = text.find_last_not_of("\r\n");
    if (e != std::string::npos) {
        size_t b = text.find_last_of('\n', e);
        b = b == std::string::npos ? 0 : b + 1;
        lastLine_ = text.substr(b, e - b + 1);
    }
}

bool GuiRun::start(const Settings& s, const LogPaths& paths, DoneFn onDone, std::string& why) {
    why.clear();
    if (busy_.load()) {
        why = "a run is already active";
        return false;
    }
    if (worker_.joinable())
        worker_.join();
    open_log(paths, s);
    logger_.sink = [this](const std::string& text) {
        write_log(text);
    };
    // Everything that can throw (the Run's memory, the OS refusing a new thread) happens here
    std::unique_ptr<Run> run;
    std::thread worker;
    if (!try_start(
            [&] {
                run = std::make_unique<Run>(logger_);
                Run* r = run.get();
                worker = std::thread([s, r, onDone] {
                    const int rc = run_suite(s, *r);
                    if (onDone)
                        onDone(rc);
                });
            },
            why)) {
        std::lock_guard<std::mutex> l(logMu_);
        if (log_.is_open()) {
            log_ << "run did not start: " << why << '\n'; // best effort, the header line is already there
            log_.close();
        }
        return false;
    }
    run_ = std::move(run);
    worker_ = std::move(worker);
    busy_ = true;
    return true;
}

void GuiRun::request_stop() {
    if (!busy_.load() || !run_)
        return;
    run_->stop = true;
}

std::string GuiRun::finish() {
    if (worker_.joinable())
        worker_.join();
    busy_ = false;
    std::string last;
    std::lock_guard<std::mutex> l(logMu_);
    last = lastLine_;
    if (log_.is_open())
        log_.close();
    return last;
}

void GuiRun::close_log() {
    std::lock_guard<std::mutex> l(logMu_);
    if (log_.is_open())
        log_.close();
}

bool GuiRun::try_close_log() {
    if (!logMu_.try_lock())
        return false;
    if (log_.is_open())
        log_.close();
    logMu_.unlock();
    return true;
}

} // namespace ddr5::gui
