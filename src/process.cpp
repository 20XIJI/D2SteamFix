#include "steamfix/process.hpp"

#include "steamfix/logging.hpp"
#include "steamfix/paths.hpp"
#include "steamfix/win32.hpp"

#include <shlobj.h>
#include <tlhelp32.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace steamfix::process {
namespace {

using ProcessKey = std::pair<DWORD, std::uint64_t>;

class ReleaseState {
public:
    using Clock = std::chrono::steady_clock;

    // The launcher can exit before destiny2.exe appears. Releasing protection during that window
    // silently defeats the whole wrapper: the game starts afterwards and Steam injects the overlay
    // as usual, with nothing to report. Waiting longer only delays an already-finished session.
    static constexpr auto launcher_exit_grace = std::chrono::seconds(120);
    static constexpr unsigned exit_debounce_polls = 2;

    [[nodiscard]] bool observe(const bool launcher_alive, const bool game_alive,
                               const bool observation_reliable,
                               const Clock::time_point now) noexcept {
        if (game_alive) game_observed_ = true;
        if (!observation_reliable) {
            reset_negative_observations();
            return false;
        }
        if (launcher_alive || game_alive) {
            reset_negative_observations();
            return false;
        }
        if (!game_observed_) {
            if (!launcher_exit_observed_at_) {
                launcher_exit_observed_at_ = now;
                absent_polls_ = 0;
                return false;
            }
            if (now - *launcher_exit_observed_at_ < launcher_exit_grace) {
                absent_polls_ = 0;
                return false;
            }
        }
        if (absent_polls_ != std::numeric_limits<unsigned>::max()) ++absent_polls_;
        return absent_polls_ >= exit_debounce_polls;
    }

    // False means protection was released on the grace period rather than on watching the game
    // exit, which is worth recording: it is the shape a silently ineffective session takes.
    [[nodiscard]] bool game_was_observed() const noexcept { return game_observed_; }

private:
    void reset_negative_observations() noexcept {
        launcher_exit_observed_at_.reset();
        absent_polls_ = 0;
    }

    std::optional<Clock::time_point> launcher_exit_observed_at_;
    unsigned absent_polls_ = 0;
    bool game_observed_ = false;
};

struct ChildPollState {
    bool finished = false;
    std::optional<int> status;
    unsigned wait_failures = 0;
};

struct ScanResult {
    bool launcher_alive = false;
    bool game_alive = false;
    bool uncertain = false;
    std::string uncertainty_detail;
};

void increment_saturated(unsigned& value) noexcept {
    if (value != std::numeric_limits<unsigned>::max()) ++value;
}

bool should_log_poll(const unsigned count) noexcept {
    return count == 1 || count % 20 == 0;
}

std::uint64_t creation_key(const HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) throw_win32();
    return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
}

bool process_name_is(const std::wstring& name, const wchar_t* expected) noexcept {
    return _wcsicmp(name.c_str(), expected) == 0;
}

std::string process_message(const char* event, const std::wstring& name, const DWORD pid) {
    return std::string("observed process ") + event + ": " + narrow(name) +
           ", pid=" + std::to_string(pid);
}

bool process_has_exited(const HANDLE handle, const DWORD pid) noexcept {
    DWORD exit_code = 0;
    if (GetExitCodeProcess(handle, &exit_code) && exit_code != STILL_ACTIVE) return true;
    const HANDLE sync = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (sync == nullptr) return GetLastError() == ERROR_INVALID_PARAMETER;
    const bool exited = WaitForSingleObject(sync, 0) == WAIT_OBJECT_0;
    CloseHandle(sync);
    return exited;
}

void poll_child_exit(const HANDLE child, ChildPollState& state, const Logger& log) {
    if (state.finished) return;
    const DWORD wait = WaitForSingleObject(child, 0);
    if (wait == WAIT_TIMEOUT) {
        if (state.wait_failures != 0) log.info("original launcher handle became queryable again");
        state.wait_failures = 0;
        return;
    }
    if (wait == WAIT_OBJECT_0) {
        DWORD status = 0;
        if (GetExitCodeProcess(child, &status)) {
            if (state.wait_failures != 0) log.info("original launcher handle became queryable again");
            state.wait_failures = 0;
            state.finished = true;
            state.status = static_cast<int>(status);
        } else {
            // The signaled process handle proves the launcher exited.
            // UAC may provide a waitable handle without exit-code rights.
            state.finished = true;
            state.wait_failures = 0;
            log.warn("original launcher exited; exit status is unavailable: " +
                     windows_error(GetLastError()));
        }
        return;
    }
    const std::string detail =
        wait == WAIT_FAILED
            ? "WaitForSingleObject failed: " + windows_error(GetLastError())
            : "WaitForSingleObject returned unexpected status " + std::to_string(wait);
    increment_saturated(state.wait_failures);
    if (should_log_poll(state.wait_failures)) {
        log.warn("original launcher handle unavailable for " +
                 std::to_string(state.wait_failures) +
                 " consecutive polls; retaining renderer protection: " + detail);
    }
}

ScanResult scan_destiny_processes(const fs::path& game_dir, const bool child_finished,
                                  std::map<ProcessKey, std::wstring>& tracked,
                                  std::set<ProcessKey>& previous, const Logger& log) {
    const std::vector<ProcessInfo> items = snapshot();
    std::set<ProcessKey> current;
    ScanResult result{};
    result.launcher_alive = !child_finished;

    for (const ProcessInfo& item : items) {
        const bool launcher = process_name_is(item.name, L"destiny2launcher.exe");
        const bool game = process_name_is(item.name, L"destiny2.exe");
        if (!launcher && !game) continue;
        try {
            const UniqueHandle handle = open_query(item.pid);
            const fs::path path = image_path(handle.get());
            if (!path.has_parent_path() || !paths::equal_windows(path.parent_path(), game_dir)) {
                continue;
            }
            const ProcessKey key{item.pid, creation_key(handle.get())};
            current.insert(key);
            if (!tracked.contains(key)) tracked.emplace(key, item.name);
            result.launcher_alive = result.launcher_alive || launcher;
            result.game_alive = result.game_alive || game;
        } catch (const std::exception& error) {
            result.uncertain = true;
            result.uncertainty_detail = "pid=" + std::to_string(item.pid) +
                                        ", name=" + narrow(item.name) + ": " + error.what();
        }
    }

    for (const ProcessKey& key : current) {
        if (!previous.contains(key)) {
            log.info(process_message("start", tracked.at(key), key.first));
        }
    }
    for (const ProcessKey& key : previous) {
        if (!current.contains(key)) {
            const auto found = tracked.find(key);
            if (found != tracked.end()) {
                log.info(process_message("exit", found->second, key.first));
                tracked.erase(found);
            }
        }
    }
    previous = std::move(current);
    return result;
}

} // namespace

std::vector<ProcessInfo> snapshot() {
    UniqueHandle handle(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(handle.get(), &entry)) throw_win32();
    std::vector<ProcessInfo> result;
    do {
        result.push_back({entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile});
    } while (Process32NextW(handle.get(), &entry));
    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) throw_win32(error);
    return result;
}

UniqueHandle open_query(const DWORD pid) {
    return UniqueHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
}

fs::path image_path(const HANDLE process) {
    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length)) throw_win32();
    buffer.resize(length);
    return fs::path(buffer);
}

fs::path current_executable() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        const DWORD error = GetLastError();
        throw_win32(error != ERROR_SUCCESS ? error : ERROR_BAD_PATHNAME);
    }
    buffer.resize(length);
    return fs::path(buffer);
}

bool is_destiny_name(const std::wstring& name) noexcept {
    return process_name_is(name, L"destiny2.exe") ||
           process_name_is(name, L"destiny2launcher.exe");
}

bool is_steam_name(const std::wstring& name) noexcept {
    constexpr std::array names{
        L"steam.exe", L"steamwebhelper.exe", L"steamservice.exe", L"GameOverlayUI.exe",
        L"steamerrorreporter.exe", L"steamerrorreporter64.exe", L"steam_monitor.exe"};
    for (const wchar_t* candidate : names) {
        if (process_name_is(name, candidate)) return true;
    }
    return false;
}

bool is_shared_steam_service(const fs::path& image) {
    if (!paths::name_is(image, L"steamservice.exe")) return false;
    for (const int folder : {CSIDL_PROGRAM_FILES_COMMONX86, CSIDL_PROGRAM_FILES_COMMON}) {
        std::array<wchar_t, MAX_PATH> directory{};
        if (FAILED(SHGetFolderPathW(nullptr, folder, nullptr, SHGFP_TYPE_CURRENT, directory.data()))) {
            continue;
        }
        if (paths::equal_windows(image, fs::path(directory.data()) / L"Steam" / L"SteamService.exe")) {
            return true;
        }
    }
    return false;
}

bool is_inside_steam(const fs::path& image, const fs::path& directory) {
    if (directory.empty()) return false;
    // Steam's client helpers live in child directories, never in steamapps.
    // Updating Steam may already have renamed or replaced a running image.
    fs::path parent = fs::weakly_canonical(image.parent_path());
    for (unsigned depth = 0; depth < 8 && !parent.empty(); ++depth) {
        if (paths::equal_windows(parent, directory)) return true;
        if (paths::name_is(parent, L"steamapps")) return false;
        const fs::path next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    return false;
}

bool is_steam_process(const fs::path& image, const fs::path& selected_directory) {
    if ((!selected_directory.empty() && is_inside_steam(image, selected_directory)) ||
        is_shared_steam_service(image)) {
        return true;
    }
    fs::path parent = fs::weakly_canonical(image.parent_path());
    for (unsigned depth = 0; depth < 8 && !parent.empty(); ++depth) {
        if (paths::name_is(parent, L"steamapps")) return false;
        const DWORD attributes = GetFileAttributesW((parent / L"steam.exe").c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0) {
            return true;
        }
        const fs::path next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    return false;
}

std::vector<std::pair<DWORD, std::wstring>> matching_in_dir(const fs::path& directory) {
    std::vector<std::pair<DWORD, std::wstring>> result;
    for (const ProcessInfo& item : snapshot()) {
        if (!is_destiny_name(item.name)) continue;
        try {
            const UniqueHandle handle = open_query(item.pid);
            fs::path path;
            try {
                path = image_path(handle.get());
            } catch (const std::system_error&) {
                if (process_has_exited(handle.get(), item.pid)) continue;
                throw;
            }
            if (!path.has_parent_path()) {
                throw std::runtime_error("queried image path has no containing directory: " +
                                         display_path(path));
            }
            if (paths::equal_windows(path.parent_path(), directory)) {
                result.emplace_back(item.pid, item.name);
            }
        } catch (const std::system_error& error) {
            if (static_cast<DWORD>(error.code().value()) == ERROR_INVALID_PARAMETER) continue;
            throw std::runtime_error("could not verify existing Destiny process candidate: pid=" +
                                     std::to_string(item.pid) + ", name=" + narrow(item.name) +
                                     ": " + error.what());
        } catch (const std::exception& error) {
            throw std::runtime_error("could not verify existing Destiny process candidate: pid=" +
                                     std::to_string(item.pid) + ", name=" + narrow(item.name) +
                                     ": " + error.what());
        }
    }
    return result;
}

std::optional<int> monitor(const HANDLE child, const DWORD child_pid, const fs::path& game_dir,
                           const Logger& log) noexcept {
    std::map<ProcessKey, std::wstring> tracked;
    std::set<ProcessKey> previous;
    ReleaseState release;
    ChildPollState child_state;
    unsigned failures = 0;
    bool candidate_uncertain = false;
    unsigned candidate_uncertain_polls = 0;

    for (;;) {
        try {
            poll_child_exit(child, child_state, log);
            try {
                const ScanResult scan = scan_destiny_processes(
                    game_dir, child_state.finished, tracked, previous, log);
                if (!scan.uncertain && candidate_uncertain) {
                    log.info("named Destiny process candidates are verifiable again");
                }
                candidate_uncertain = scan.uncertain;
                if (scan.uncertain) {
                    increment_saturated(candidate_uncertain_polls);
                    if (should_log_poll(candidate_uncertain_polls)) {
                        log.warn("a named Destiny process candidate could not be "
                                 "verified for " +
                                 std::to_string(candidate_uncertain_polls) +
                                 " consecutive polls; retaining renderer protection: " +
                                 scan.uncertainty_detail);
                    }
                } else {
                    candidate_uncertain_polls = 0;
                }
                failures = 0;
                if (release.observe(scan.launcher_alive, scan.game_alive, !scan.uncertain,
                                    ReleaseState::Clock::now())) {
                    break;
                }
            } catch (const std::exception& error) {
                increment_saturated(failures);
                if (should_log_poll(failures)) {
                    log.warn("process observation unavailable for " + std::to_string(failures) +
                             " consecutive polls; retaining renderer protection: " + error.what());
                }
                static_cast<void>(release.observe(false, false, false, ReleaseState::Clock::now()));
            }
        } catch (...) {
            increment_saturated(failures);
            log.warn("process-monitor iteration failed unexpectedly; retaining "
                     "renderer protection");
            static_cast<void>(release.observe(false, false, false, ReleaseState::Clock::now()));
        }
        Sleep(500);
    }
    try {
        log.info("both Destiny process categories closed; original launcher pid=" +
                 std::to_string(child_pid));
    } catch (...) {
        log.info("both Destiny process categories closed");
    }
    if (!release.game_was_observed()) {
        log.warn("released renderer protection without ever observing destiny2.exe: the launcher "
                 "exited and the grace period elapsed. If the game started after that point, the "
                 "Steam overlay was not blocked for this session.");
    }
    return child_state.status;
}

std::vector<DWORD> parent_chain() {
    std::map<DWORD, DWORD> parents;
    for (const ProcessInfo& item : snapshot()) parents[item.pid] = item.parent_pid;
    DWORD current = GetCurrentProcessId();
    std::vector<DWORD> result;
    for (unsigned depth = 0; depth < 8; ++depth) {
        const auto found = parents.find(current);
        if (found == parents.end() || found->second == 0 || found->second == current) break;
        result.push_back(found->second);
        current = found->second;
    }
    return result;
}

} // namespace steamfix::process
