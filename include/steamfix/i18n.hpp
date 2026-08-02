#pragma once

#include <string>

namespace steamfix::i18n {

// Identifiers for text shown to the user in a message box. Only this text follows the system
// language: steamfix.log and the technical detail carried by AppError stay English so that
// user-supplied reports remain readable to maintainers.
enum class Msg {
    invalid_invocation,
    recovered_prefix,
    executable_path_failure,
    working_directory_failure,
    log_open_failure,
    recovery_prompt,
    elevated_intro,
    elevated_source,
    elevated_fix_named,
    elevated_fix_both,
    internal_failure,
    failure_intro,
    failure_stage_suffix,
    see_log,
    error_code,
    destiny_running,
    wrong_forwarded_executable,
    launcher_started,
    game_not_started,
    format_failure,
    count,
};

// Allocation-free, safe to call from a noexcept failure path.
[[nodiscard]] const wchar_t* literal(Msg id) noexcept;

[[nodiscard]] std::wstring text(Msg id);

} // namespace steamfix::i18n
