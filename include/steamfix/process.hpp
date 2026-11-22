#pragma once

#include "steamfix/win32.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace steamfix {
class Logger;
}

namespace steamfix::process {

struct ProcessInfo {
    DWORD pid;
    DWORD parent_pid;
    std::wstring name;
};

std::vector<ProcessInfo> snapshot();
UniqueHandle open_query(DWORD pid);
std::filesystem::path image_path(HANDLE process);
std::filesystem::path current_executable();
bool is_destiny_name(const std::wstring& name) noexcept;
bool is_steam_name(const std::wstring& name) noexcept;
bool is_shared_steam_service(const std::filesystem::path& image);
bool is_inside_steam(const std::filesystem::path& image, const std::filesystem::path& directory);
bool is_steam_process(const std::filesystem::path& image, const std::filesystem::path& selected_directory = {});
std::vector<std::pair<DWORD, std::wstring>> matching_in_dir(const std::filesystem::path& directory);
struct MonitorResult {
    std::optional<int> exit_status;
    bool game_observed = false;
    std::chrono::seconds duration{0};
};

MonitorResult monitor(HANDLE child, DWORD child_pid, const std::filesystem::path& game_dir,
                      const Logger& log) noexcept;
std::vector<DWORD> parent_chain();

} // namespace steamfix::process
