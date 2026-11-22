#pragma once

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace steamfix {

namespace constants {
inline constexpr wchar_t renderer64_name[] = L"GameOverlayRenderer64.dll";
inline constexpr wchar_t renderer32_name[] = L"GameOverlayRenderer.dll";
inline constexpr wchar_t session_mutex_name[] = L"Global\\D2SteamFix-1085660";
} // namespace constants

[[noreturn]] inline void throw_win32(const DWORD code = GetLastError()) {
    throw std::system_error(static_cast<int>(code), std::system_category());
}

inline bool is_missing_error(const DWORD code) noexcept {
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
}

class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) : value_(value) {
        if (value == nullptr || value == INVALID_HANDLE_VALUE) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
        }
    }
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] HANDLE release() noexcept {
        const HANDLE old = value_;
        value_ = nullptr;
        return old;
    }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ == value) return;
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_ = nullptr;
};

class LocalMemory {
public:
    explicit LocalMemory(void* value = nullptr) noexcept : value_(value) {}
    ~LocalMemory() { if (value_ != nullptr) LocalFree(value_); }
    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;
private:
    void* value_;
};

inline std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("wide string is too large to convert");
    }
    int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    DWORD flags = WC_ERR_INVALID_CHARS;
    if (needed <= 0) {
        flags = 0;
        needed = WideCharToMultiByte(CP_UTF8, flags, value.data(), static_cast<int>(value.size()),
                                     nullptr, 0, nullptr, nullptr);
    }
    if (needed <= 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    std::string result(static_cast<size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, flags, value.data(), static_cast<int>(value.size()),
                            result.data(), needed, nullptr, nullptr) == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    }
    return result;
}

inline std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("narrow string is too large to convert");
    }
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (needed <= 0) {
        code_page = CP_ACP;
        flags = 0;
        needed = MultiByteToWideChar(code_page, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
    }
    if (needed <= 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    std::wstring result(static_cast<size_t>(needed), L'\0');
    if (MultiByteToWideChar(code_page, flags, value.data(), static_cast<int>(value.size()),
                            result.data(), needed) == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    }
    return result;
}

inline std::string display_path(const std::filesystem::path& value) {
    return narrow(value.native());
}

inline std::string windows_error(DWORD code = GetLastError()) {
    return std::system_error(static_cast<int>(code), std::system_category()).what();
}

inline std::vector<std::byte> query_token_info(const HANDLE token, const TOKEN_INFORMATION_CLASS which,
                                               const size_t minimum, const char* invalid_size_message) {
    DWORD needed = 0;
    if (GetTokenInformation(token, which, nullptr, 0, &needed)) {
        throw std::runtime_error(invalid_size_message);
    }
    const DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) throw_win32(error);
    if (static_cast<size_t>(needed) < minimum) throw std::runtime_error(invalid_size_message);
    std::vector<std::byte> buffer(needed);
    if (!GetTokenInformation(token, which, buffer.data(), needed, &needed)) throw_win32();
    if (static_cast<size_t>(needed) < minimum) throw std::runtime_error(invalid_size_message);
    buffer.resize(needed);
    return buffer;
}

inline BY_HANDLE_FILE_INFORMATION inspect_regular_file(const HANDLE handle, const bool require_single_link,
                                                       const char* invalid_message) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information)) throw_win32();
    if ((information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        (require_single_link && information.nNumberOfLinks != 1)) {
        throw std::runtime_error(invalid_message);
    }
    return information;
}

} // namespace steamfix
