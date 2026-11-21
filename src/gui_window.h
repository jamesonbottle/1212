#pragma once
// gui_window.h - the main window of the GUI: class registration, control creation, layout and message handling.
// The state of the window lives in one object (MainWindow::Impl) that the window owns through GWLP_USERDATA, not in
// globals. Run control and the log are in gui_run.h; plain data and rules are in gui_model.h.
#include <memory>
#include "gui_win.h"

namespace ddr5::gui {

class MainWindow {
  public:
    struct Impl; // defined in gui_window.cpp

    // Registers the class, creates and shows the window. Returns nullptr (after showing a message) when that fails.
    static std::unique_ptr<MainWindow> create(HINSTANCE inst, int show);

    // Joins the worker thread of a run that is still going (there is none once the window was closed normally).
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // For the message loop: lets the window handle Tab, arrows and Enter for its controls.
    bool translate_dialog(MSG* msg);

  private:
    MainWindow();
    std::unique_ptr<Impl> impl_;
};

} // namespace ddr5::gui
