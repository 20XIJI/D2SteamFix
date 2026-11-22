#pragma once

#include <filesystem>
#include <vector>

namespace steamfix {
class Logger;
}

namespace steamfix::repair {

enum class Trigger {
    direct_launch,
    blocked_without_backup,
    unusable_backup_blocked,
};

// Check effective execute access before accepting permissions as a new baseline.
bool needed(const std::filesystem::path& steam_directory, const Logger& log);
bool any_execute_blocked(const std::vector<std::filesystem::path>& paths, const Logger& log);

// Cancellation leaves applications, renderer files, and recovery state untouched.
bool run(const std::filesystem::path& game_directory,
         const std::filesystem::path& recovery_path, Trigger trigger, const Logger& log);

} // namespace steamfix::repair
