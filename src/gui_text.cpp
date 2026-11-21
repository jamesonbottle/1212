// gui_text.cpp - UTF-8 / ANSI to UTF-16 conversion and small text formatting for the GUI.
#include "gui_text.h"
#include <climits>
#include "gui_win.h"

namespace ddr5::gui {

namespace {

std::wstring convert(UINT codePage, const std::string& s) {
    if (s.empty() || s.size() > static_cast<size_t>(INT_MAX))
        return std::wstring();
    const int len = static_cast<int>(s.size());
    const int n = MultiByteToWideChar(codePage, 0, s.data(), len, nullptr, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(codePage, 0, s.data(), len, &w[0], n) <= 0)
        return std::wstring();
    return w;
}

std::wstring two_digits(u64 v) {
    return (v < 10 ? L"0" : L"") + std::to_wstring(v);
}

} // namespace

std::wstring utf8_to_wide(const std::string& s) {
    return convert(CP_UTF8, s);
}

std::wstring ansi_to_wide(const std::string& s) {
    std::wstring w = convert(CP_ACP, s);
    if (w.empty() && !s.empty())
        w = convert(CP_UTF8, s); // the ANSI conversion failed: the text may be UTF-8
    return w;
}

std::wstring duration_text(u64 seconds) {
    return std::to_wstring(seconds / 86400) + L":" + two_digits(seconds / 3600 % 24) + L":" +
           two_digits(seconds / 60 % 60) + L":" + two_digits(seconds % 60);
}

} // namespace ddr5::gui
