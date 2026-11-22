#include "steamfix/repair.hpp"

#include "steamfix/command_line.hpp"
#include "steamfix/elevation.hpp"
#include "steamfix/error.hpp"
#include "steamfix/i18n.hpp"
#include "steamfix/logging.hpp"
#include "steamfix/paths.hpp"
#include "steamfix/process.hpp"
#include "steamfix/steam.hpp"
#include "steamfix/ui.hpp"
#include "steamfix/win32.hpp"

#include <aclapi.h>
#include <commctrl.h>
#include <commdlg.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cwchar>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace steamfix::repair {
namespace {

constexpr std::array<const wchar_t*, 2> renderer_names{
    constants::renderer64_name, constants::renderer32_name};

class MutexLock {
public:
    MutexLock(const wchar_t* name, const DWORD timeout) {
        handle_ = UniqueHandle(CreateMutexW(nullptr, FALSE, name));
        const DWORD result = WaitForSingleObject(handle_.get(), timeout);
        if (result == WAIT_TIMEOUT) {
            throw std::runtime_error("another D2SteamFix session did not finish; close it and retry repair");
        }
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) throw_win32();
    }
    ~MutexLock() { ReleaseMutex(handle_.get()); }
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;
private:
    UniqueHandle handle_;
};

void require_regular(const HANDLE handle, const fs::path& path) {
    const std::string message =
        "repair target is not a regular, single-link, non-reparse file: " + display_path(path);
    static_cast<void>(inspect_regular_file(handle, true, message.c_str()));
}

std::optional<UniqueHandle> open_existing(const fs::path& path, const DWORD access) {
    try {
        UniqueHandle handle = elevation::open_file(
            path, access | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT);
        require_regular(handle.get(), path);
        return handle;
    } catch (const std::system_error& error) {
        if (is_missing_error(static_cast<DWORD>(error.code().value()))) return std::nullopt;
        throw;
    }
}

UniqueHandle current_token() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &raw)) {
        throw_win32();
    }
    return UniqueHandle(raw);
}

bool access_allowed(const HANDLE file, const HANDLE token, const DWORD access) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD status = GetSecurityInfo(file, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &descriptor);
    if (status != ERROR_SUCCESS) throw_win32(status);
    LocalMemory memory(descriptor);
    HANDLE raw = nullptr;
    if (!DuplicateToken(token, SecurityImpersonation, &raw)) throw_win32(GetLastError());
    const UniqueHandle impersonation(raw);
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    std::vector<std::byte> privileges(sizeof(PRIVILEGE_SET) + 32U * sizeof(LUID_AND_ATTRIBUTES));
    DWORD length = static_cast<DWORD>(privileges.size());
    DWORD granted = 0;
    BOOL allowed = FALSE;
    if (!AccessCheck(descriptor, impersonation.get(), access, &mapping,
                      reinterpret_cast<PPRIVILEGE_SET>(privileges.data()), &length, &granted, &allowed)) {
        throw_win32(GetLastError());
    }
    return allowed != FALSE;
}

const wchar_t* confirm_instruction(const Trigger trigger) noexcept {
    switch (trigger) {
    case Trigger::direct_launch:
        return i18n::literal(i18n::Msg::repair_confirm_direct);
    case Trigger::blocked_without_backup:
        return i18n::literal(i18n::Msg::repair_confirm_blocked_without_backup);
    case Trigger::unusable_backup_blocked:
        return i18n::literal(i18n::Msg::repair_confirm_unusable_backup_blocked);
    }
    return i18n::literal(i18n::Msg::repair_confirm_direct);
}

bool confirm(const Trigger trigger) {
    const TASKDIALOG_BUTTON buttons[]{
        {IDOK, i18n::literal(i18n::Msg::repair_continue_button)},
        {IDCANCEL, i18n::literal(i18n::Msg::repair_cancel_button)},
    };
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof(dialog);
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.pszWindowTitle = L"D2SteamFix";
    dialog.pszMainIcon = TD_INFORMATION_ICON;
    dialog.pszMainInstruction = confirm_instruction(trigger);
    dialog.cButtons = static_cast<UINT>(std::size(buttons));
    dialog.pButtons = buttons;
    dialog.nDefaultButton = IDCANCEL;
    int selected = IDCANCEL;
    const HRESULT result = TaskDialogIndirect(&dialog, &selected, nullptr, nullptr);
    if (FAILED(result)) throw std::runtime_error("could not display Steam repair confirmation");
    return selected == IDOK;
}

fs::path validate_steam(const fs::path& executable) {
    if (!paths::name_is(executable, L"steam.exe")) {
        throw std::runtime_error("selected file is not steam.exe");
    }
    const fs::path directory = fs::canonical(executable.parent_path());
    const auto file = open_existing(directory / L"steam.exe", FILE_READ_ATTRIBUTES);
    if (!file) throw std::runtime_error("steam.exe is missing from the selected directory");
    return directory;
}

std::optional<fs::path> locate_steam(const Logger& log) {
    try {
        return validate_steam(steam::discover() / L"steam.exe");
    } catch (const std::exception& error) {
        log.warn("Steam repair discovery needs a file selection: " + std::string(error.what()));
    }
    for (;;) {
        std::wstring file(32768, L'\0');
        OPENFILENAMEW picker{};
        picker.lStructSize = sizeof(picker);
        picker.lpstrFilter = L"Steam (steam.exe)\0steam.exe\0\0";
        picker.lpstrFile = file.data();
        picker.nMaxFile = static_cast<DWORD>(file.size());
        picker.lpstrTitle = i18n::literal(i18n::Msg::repair_locate_steam_title);
        picker.lpstrDefExt = L"exe";
        picker.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                       OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
        if (!GetOpenFileNameW(&picker)) {
            const DWORD error = CommDlgExtendedError();
            if (error == 0) return std::nullopt;
            throw std::runtime_error("Steam file selection failed: " + std::to_string(error));
        }
        file.resize(std::wcslen(file.c_str()));
        try {
            return validate_steam(fs::path(file));
        } catch (const std::exception& error) {
            log.warn("selected Steam path was invalid: " + std::string(error.what()));
            ui::error(i18n::literal(i18n::Msg::repair_select_steam_error));
        }
    }
}

bool is_exiting_process_error(const DWORD code) noexcept {
    return code == ERROR_INVALID_PARAMETER || code == ERROR_GEN_FAILURE ||
           code == ERROR_PARTIAL_COPY;
}

bool is_transient_query_error(const DWORD code) noexcept {
    return is_exiting_process_error(code) || code == ERROR_ACCESS_DENIED;
}

struct Candidate {
    DWORD pid;
    std::wstring name;
};

struct Termination {
    UniqueHandle process;
    Candidate candidate;
};

std::vector<Termination> prepare_shutdown(const fs::path& steam_directory) {
    std::vector<Termination> result;
    for (const process::ProcessInfo& item : process::snapshot()) {
        if (!process::is_destiny_name(item.name) && !process::is_steam_name(item.name)) continue;
        try {
            std::optional<fs::path> prefiltered_image;
            try {
                const UniqueHandle query = process::open_query(item.pid);
                const fs::path image = process::image_path(query.get());
                if (!paths::name_is(image, item.name)) continue;
                if (!process::is_destiny_name(item.name) &&
                    !process::is_steam_process(image, steam_directory)) {
                    continue;
                }
                prefiltered_image = image;
            } catch (const std::system_error& error) {
                const DWORD code = static_cast<DWORD>(error.code().value());
                if (is_exiting_process_error(code)) continue;
                if (code != ERROR_ACCESS_DENIED) throw;
            }

            UniqueHandle handle = elevation::open_process_for_termination(item.pid);
            if (WaitForSingleObject(handle.get(), 0) == WAIT_OBJECT_0) continue;
            const fs::path image = process::image_path(handle.get());
            if (!paths::name_is(image, item.name)) continue;
            if (prefiltered_image && !paths::equal_windows(image, *prefiltered_image)) continue;
            if (!process::is_destiny_name(item.name) &&
                !process::is_steam_process(image, steam_directory)) {
                continue;
            }
            result.push_back({std::move(handle), {item.pid, item.name}});
        } catch (const std::system_error& error) {
            if (is_exiting_process_error(static_cast<DWORD>(error.code().value()))) continue;
            throw;
        }
    }
    return result;
}

void shutdown(const fs::path& steam_directory, const Logger& log) {
    // Prepare every handle before terminating anything, so UAC cancellation is harmless.
    std::vector<Termination> targets = prepare_shutdown(steam_directory);
    for (unsigned pass = 0; pass < 8; ++pass) {
        // Stop the game before Steam, including processes created while Steam was closing.
        for (const bool destiny : {true, false}) {
            for (const Termination& target : targets) {
                if (process::is_destiny_name(target.candidate.name) != destiny) continue;
                if (WaitForSingleObject(target.process.get(), 0) == WAIT_OBJECT_0) continue;
                if (!TerminateProcess(target.process.get(), ERROR_CANCELLED)) {
                    const DWORD error = GetLastError();
                    if (WaitForSingleObject(target.process.get(), 0) != WAIT_OBJECT_0) throw_win32(error);
                }
                const DWORD waited = WaitForSingleObject(target.process.get(), 10000);
                if (waited != WAIT_OBJECT_0) {
                    if (waited == WAIT_FAILED) throw_win32();
                    throw std::runtime_error("a Steam or Destiny process did not exit during repair");
                }
                log.info("Steam repair closed " + narrow(target.candidate.name) +
                         "; pid=" + std::to_string(target.candidate.pid));
            }
        }
        targets = prepare_shutdown(steam_directory);
        if (targets.empty()) return;
        Sleep(250);
    }
    throw std::runtime_error("Steam or Destiny processes kept restarting; renderer files were preserved");
}

void remove_file(const fs::path& path, const Logger& log) {
    auto handle = open_existing(path, DELETE);
    if (!handle) return;
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(handle->get(), FileDispositionInfo,
                                     &disposition, sizeof(disposition))) {
        throw_win32(GetLastError());
    }
    handle.reset();
    log.info("Steam repair removed " + display_path(path));
}

bool remove_renderers(const fs::path& directory, const Logger& log) {
    std::vector<std::pair<fs::path, UniqueHandle>> opened;
    bool renderer32_was_present = false;
    for (const wchar_t* name : renderer_names) {
        const fs::path path = directory / name;
        auto handle = open_existing(path, DELETE);
        if (handle) {
            if (paths::name_is(path, constants::renderer32_name)) renderer32_was_present = true;
            opened.emplace_back(path, std::move(*handle));
        }
    }
    // Acquire and validate both delete handles before removing either renderer.
    for (auto& [path, handle] : opened) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(handle.get(), FileDispositionInfo,
                                        &disposition, sizeof(disposition))) {
            throw_win32(GetLastError());
        }
        handle.reset();
        log.info("Steam repair removed " + display_path(path));
    }
    return renderer32_was_present;
}

UniqueHandle launch_repair(const fs::path& directory, const Logger& log) {
    const fs::path executable = directory / L"steam.exe";
    std::wstring line;
    command_line::append_quoted(line, executable.native());
    line += L" -forcesteamupdate -forcepackagedownload -exitsteam";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION information{};
    if (!elevation::launch_as_desktop_user(executable, line, directory, information)) {
        if (elevation::is_elevated()) log.warn("desktop launch token unavailable; Steam repair inherits administrator context");
        if (!CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, directory.c_str(), &startup, &information)) {
            throw_win32(GetLastError());
        }
    }
    UniqueHandle child(information.hProcess);
    const UniqueHandle thread(information.hThread);
    log.info("started Steam forced package repair; pid=" + std::to_string(information.dwProcessId));
    return child;
}

bool steam_client_alive(const fs::path& directory) {
    for (const process::ProcessInfo& item : process::snapshot()) {
        if (!process::is_steam_name(item.name) ||
            _wcsicmp(item.name.c_str(), L"steamservice.exe") == 0) {
            continue;
        }
        try {
            const UniqueHandle handle = process::open_query(item.pid);
            if (process::is_inside_steam(process::image_path(handle.get()), directory)) return true;
        } catch (const std::system_error& error) {
            if (is_transient_query_error(static_cast<DWORD>(error.code().value()))) return true;
            throw;
        }
    }
    return false;
}

void wait_for_repair(const HANDLE child, const fs::path& directory, const Logger& log) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::minutes(20);
    std::optional<Clock::time_point> quiet_since;
    for (;;) {
        const DWORD wait = WaitForSingleObject(child, 500);
        if (wait != WAIT_TIMEOUT && wait != WAIT_OBJECT_0) throw_win32();
        if (wait == WAIT_OBJECT_0 && !steam_client_alive(directory)) {
            const auto now = Clock::now();
            if (!quiet_since) quiet_since = now;
            if (now - *quiet_since >= std::chrono::seconds(3)) {
                DWORD code = 0;
                if (!GetExitCodeProcess(child, &code)) throw_win32();
                log.info("Steam repair processes exited; original bootstrap status=" + std::to_string(code));
                return;
            }
        } else {
            quiet_since.reset();
        }
        if (Clock::now() >= deadline) {
            throw std::runtime_error("Steam repair did not exit within 20 minutes; recovery state was preserved");
        }
        if (wait == WAIT_OBJECT_0) Sleep(250);
    }
}

UniqueHandle verification_token(const Logger& log) {
    if (elevation::is_elevated()) {
        if (std::optional<UniqueHandle> desktop = elevation::desktop_user_token()) {
            return std::move(*desktop);
        }
        log.info("same-user normal desktop token unavailable; verifying renderer access in caller context");
    }
    return current_token();
}

void verify_repaired(const fs::path& directory, const bool renderer32_was_present,
                     const Logger& log) {
    const UniqueHandle token = verification_token(log);
    for (const wchar_t* name : renderer_names) {
        const fs::path path = directory / name;
        const auto file = open_existing(path, READ_CONTROL);
        if (!file && paths::name_is(path, constants::renderer32_name) && !renderer32_was_present) {
            log.info("optional GameOverlayRenderer.dll was absent before repair and remains absent");
            continue;
        }
        if (!file) throw std::runtime_error("Steam repair did not recreate " + display_path(path));
        if (!access_allowed(file->get(), token.get(), FILE_GENERIC_READ | FILE_GENERIC_EXECUTE)) {
            throw std::runtime_error("Steam recreated a renderer without usable read/execute permissions: " + display_path(path));
        }
        log.info("verified repaired renderer and read/execute access: " + display_path(path));
    }
}

} // namespace

bool any_execute_blocked(const std::vector<fs::path>& paths, const Logger& log) {
    const UniqueHandle token = verification_token(log);
    for (const fs::path& path : paths) {
        const auto file = open_existing(path, READ_CONTROL);
        if (file && !access_allowed(file->get(), token.get(), FILE_EXECUTE)) return true;
    }
    return false;
}

bool needed(const fs::path& steam_directory, const Logger& log) {
    return any_execute_blocked(
        {steam_directory / constants::renderer64_name, steam_directory / constants::renderer32_name},
        log);
}

bool run(const fs::path& game_directory, const fs::path& recovery_path, const Trigger trigger,
         const Logger& log) {
    try {
        const MutexLock repair_lock(L"Global\\D2SteamFix-Repair-1085660", 0);
        const fs::path game = fs::canonical(game_directory);
        if (!paths::name_is(recovery_path, L"steamfix.dat") ||
            !paths::equal_windows(fs::canonical(recovery_path.parent_path()), game)) {
            throw std::runtime_error("repair recovery path does not match the game directory");
        }
        const std::optional<fs::path> directory = locate_steam(log);
        if (!directory || !confirm(trigger)) {
            log.info("Steam repair cancelled; recovery state was preserved");
            return false;
        }
        log.info("Steam repair confirmed; Steam directory=" + display_path(*directory) +
                 "; game directory=" + display_path(game));
        elevation::ensure_broker();
        shutdown(*directory, log);
        // An active wrapper must finish restoring and release its renderer handles first.
        const MutexLock session_lock(constants::session_mutex_name, 150000);
        // Do not touch renderer files if a new Steam or Destiny process appeared.
        shutdown(*directory, log);
        const bool renderer32_was_present = remove_renderers(*directory, log);
        const UniqueHandle child = launch_repair(*directory, log);
        wait_for_repair(child.get(), *directory, log);
        verify_repaired(*directory, renderer32_was_present, log);
        remove_file(recovery_path, log);
        fs::path temporary = recovery_path;
        temporary += L".tmp";
        remove_file(temporary, log);
        fs::path legacy = recovery_path;
        legacy += L".old";
        remove_file(legacy, log);
        log.info("Steam repair completed; recovery state removed and logs retained");
        ui::info(i18n::literal(i18n::Msg::repair_completed));
        return true;
    } catch (const std::system_error& error) {
        if (static_cast<DWORD>(error.code().value()) == ERROR_CANCELLED) {
            log.info("Steam repair elevation declined; recovery state was preserved");
            return false;
        }
        throw AppError(ExitCode::overlay_protection, "Steam file repair", error.what())
            .as_repair_failure();
    } catch (const AppError&) {
        throw;
    } catch (const std::exception& error) {
        throw AppError(ExitCode::overlay_protection, "Steam file repair", error.what())
            .as_repair_failure();
    }
}

} // namespace steamfix::repair
