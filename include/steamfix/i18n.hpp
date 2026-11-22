#pragma once

#include <string>

namespace steamfix::i18n {

// Identifiers for text shown to the user in a message box. Only this text follows the system
// language: steamfix.log and the technical detail carried by AppError stay English so that
// user-supplied reports remain readable to maintainers.
enum class Msg {
    command_line_failure,
    executable_path_failure,
    game_directory_failure,
    log_open_failure,
    admin_session_failure_intro,
    admin_session_failure_outro,
    repair_failure_intro,
    repair_confirm_direct,
    repair_confirm_blocked_without_backup,
    repair_confirm_unusable_backup_blocked,
    repair_continue_button,
    repair_cancel_button,
    repair_locate_steam_title,
    repair_select_steam_error,
    repair_completed,
    internal_failure,
    failure_intro,
    failure_stage_suffix,
    see_log,
    see_log_only,
    error_code,
    destiny_running,
    session_already_active,
    wrong_forwarded_executable,
    wrong_steam_app_id,
    steam_discovery_guidance,
    overlay_protection_guidance,
    launcher_started,
    game_not_started,
    format_failure,
    count,
};

// Allocation-free, safe to call from a noexcept failure path.
[[nodiscard]] const wchar_t* literal(Msg id) noexcept;

[[nodiscard]] std::wstring text(Msg id);

} // namespace steamfix::i18n
