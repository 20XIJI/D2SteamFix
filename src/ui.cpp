#include "steamfix/ui.hpp"

#include "steamfix/i18n.hpp"

#include <windows.h>

#include <string>

namespace steamfix::ui {
namespace {

void show(const std::wstring_view message, const UINT flags) noexcept {
    try {
        const std::wstring text(message);
        MessageBoxW(nullptr, text.c_str(), L"D2SteamFix", MB_OK | flags | MB_SETFOREGROUND);
    } catch (...) {
        // i18n::literal does not allocate, so it is safe on this path.
        MessageBoxW(nullptr, i18n::literal(i18n::Msg::format_failure),
                    L"D2SteamFix", MB_OK | flags | MB_SETFOREGROUND);
    }
}

} // namespace

void info(const std::wstring_view message) noexcept { show(message, MB_ICONINFORMATION); }

void error(const std::wstring_view message) noexcept {
    show(message, MB_ICONERROR);
}

} // namespace steamfix::ui
