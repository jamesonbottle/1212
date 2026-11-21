#pragma once
// gui_run.h - starts and stops the engine for the GUI. Owns the Run, the worker thread and the log file.
// No Win32 calls: the window passes in the log paths and a callback for "the run is done", so this compiles (and is
// checked by CI) on every platform.
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "gui_model.h"
#include "log.h"
#include "run.h"
#include "suite.h"

namespace ddr5::gui {

// Where the log is written: `primary` first, `fallback` when that cannot be opened (either may be empty).
struct LogPaths {
    std::filesystem::path primary;
    std::filesystem::path fallback;
};

class GuiRun {
  public:
    // Called on the worker thread with the exit code of run_suite() when it returns. It must only post a message.
    using DoneFn = std::function<void(int)>;

    GuiRun() = default;
    ~GuiRun(); // joins the worker: the Run must outlive it
    GuiRun(const GuiRun&) = delete;
    GuiRun& operator=(const GuiRun&) = delete;

    // Opens the log, creates the Run and starts the worker. On failure (the Run's memory, the OS refusing a new
    // thread) nothing is running, the log is closed and `why` says what failed.
    bool start(const Settings& s, const LogPaths& paths, DoneFn onDone, std::string& why);

    bool busy() const { return busy_.load(); }
    bool has_run() const { return run_ != nullptr; }

    // Asks the run to stop (no-op when none is going). The worker still has to report its end.
    void request_stop();

    // UI thread, after the done message arrived: joins the worker and closes the log. Returns the last line the
    // engine wrote (the reason shown when a run could not start).
    std::string finish();

    // Closes the log if it is open (window destroyed).
    void close_log();
    // The same without waiting: false when the log is busy (force quit: another thread is writing it).
    bool try_close_log();

    // Follow the run. status() needs has_run().
    Run::Status status() const { return run_->status(); }
    u64 error_count() const { return run_ ? run_->error_count() : 0; }
    bool log_failed() const { return logFailed_.load(); }
    LogPlace log_place() const { return logPlace_; }

  private:
    void open_log(const LogPaths& paths, const Settings& s);
    void write_log(const std::string& text);

    Logger logger_; // declared before run_, so it outlives it
    std::unique_ptr<Run> run_;
    std::thread worker_;
    std::atomic<bool> busy_{false};

    std::mutex logMu_; // guards log_ and lastLine_ (the engine writes from its worker threads)
    std::ofstream log_;
    std::string lastLine_;
    LogPlace logPlace_ = LogPlace::Exe;
    std::atomic<bool> logFailed_{false};
};

} // namespace ddr5::gui
