#include "steamfix/app.hpp"

#include "steamfix/acl.hpp"
#include "steamfix/command_line.hpp"
#include "steamfix/error.hpp"
#include "steamfix/elevation.hpp"
#include "steamfix/i18n.hpp"
#include "steamfix/logging.hpp"
#include "steamfix/overlay.hpp"
#include "steamfix/paths.hpp"
#include "steamfix/process.hpp"
#include "steamfix/repair.hpp"
#include "steamfix/steam.hpp"
#include "steamfix/ui.hpp"
#include "steamfix/win32.hpp"

#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace steamfix::app {
namespace {

struct Invocation {
    fs::path launcher;
    fs::path game_dir;
    std::vector<std::wstring> args;
};

bool validate_app_ids(const std::optional<std::wstring>& app_id,
                      const std::optional<std::wstring>& game_id) noexcept;
Invocation validate_invocation(const fs::path& launcher, const std::vector<std::wstring>& args,
                               const std::optional<std::wstring>& app_id,
                               const std::optional<std::wstring>& game_id);

std::optional<std::wstring> environment(const wchar_t* name) {
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        SetLastError(ERROR_SUCCESS);
        const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
        if (needed == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
            return std::wstring{};
        }
        std::wstring value(needed, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
        if (written == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
            return std::wstring{};
        }
        if (written < needed) {
            value.resize(written);
            return value;
        }
    }
    throw std::runtime_error("environment variable changed repeatedly while it was being read");
}

std::wstring optional_id(const std::optional<std::wstring>& value) {
    return value ? L"Some(\"" + *value + L"\")" : L"None";
}

std::vector<std::wstring> command_line_arguments() {
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (raw == nullptr) throw_win32();
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> guard(raw, &LocalFree);
    std::vector<std::wstring> result;
    for (int index = 1; index < count; ++index) result.emplace_back(raw[index]);
    return result;
}

class SessionMutex final {
public:
    explicit SessionMutex(UniqueHandle handle) : handle_(std::move(handle)) {}
    ~SessionMutex() { ReleaseMutex(handle_.get()); }
    SessionMutex(const SessionMutex&) = delete;
    SessionMutex& operator=(const SessionMutex&) = delete;
private:
    UniqueHandle handle_;
};

SessionMutex session_mutex() {
    SetLastError(ERROR_SUCCESS);
    HANDLE raw = CreateMutexW(nullptr, TRUE, constants::session_mutex_name);
    if (raw == nullptr) {
        throw AppError(ExitCode::existing_session, "single-instance check", windows_error());
    }
    const bool already_exists = GetLastError() == ERROR_ALREADY_EXISTS;
    UniqueHandle handle(raw);
    if (already_exists) {
        throw AppError(ExitCode::existing_session, "single-instance check",
                       "another D2SteamFix session is already active on this computer")
            .with_guidance(i18n::text(i18n::Msg::session_already_active));
    }
    return SessionMutex(std::move(handle));
}


void require_regular_file(const fs::path& path, const ExitCode code, const char* stage,
                          const char* missing_detail) {
    std::error_code error;
    const bool regular = fs::is_regular_file(path, error);
    if (error) {
        throw AppError(code, stage, display_path(path) + ": " + error.message());
    }
    if (!regular) throw AppError(code, stage, missing_detail);
}

// Steam injects the renderer into the process it launches from %command%, which is this wrapper.
// Before protection exists that is expected and harmless: the deny ACE stops destiny2.exe from
// mapping the DLL later, and an image already loaded here is not inherited by a child process.
// Afterwards it is a real failure, because it means the ACE did not take.
enum class OverlayCheck {
    advisory,
    fatal,
};

void check_overlay(const OverlayCheck severity, const Logger& log) {
    try {
        const std::optional<fs::path> loaded = overlay::loaded_renderer();
        if (!loaded) return;
        if (severity == OverlayCheck::advisory) {
            log.warn("Steam injected " + display_path(*loaded) +
                     " into the wrapper before protection was established; this does not affect "
                     "the deny ACE applied to the game");
            return;
        }
        throw AppError(ExitCode::overlay_loaded, "self overlay check",
                       "Steam injected " + display_path(*loaded) +
                       " after protection was established");
    } catch (const AppError&) {
        throw;
    } catch (const std::exception& error) {
        const std::string detail =
            "could not safely enumerate wrapper modules: " + std::string(error.what());
        if (severity == OverlayCheck::advisory) {
            log.warn(detail);
            return;
        }
        throw AppError(ExitCode::overlay_loaded, "self overlay check", detail);
    }
}

struct ChildProcess {
    UniqueHandle process;
    DWORD pid;
};

ChildProcess launch(const Invocation& invocation, const fs::path& steam_directory, const Logger& log) {
    std::wstring command_line;
    command_line::append_quoted(command_line, invocation.launcher.native());
    for (const std::wstring& argument : invocation.args) {
        command_line::append_quoted(command_line, argument);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION information{};
    const bool intentionally_elevated = elevation::inherited_steam_directory().has_value();
    bool created = false;
    if (!intentionally_elevated) {
        created = elevation::launch_as_desktop_user(invocation.launcher, command_line,
                                                    invocation.game_dir, information);
    }
    if (!created) {
        if (!intentionally_elevated && elevation::is_elevated()) {
            log.warn("desktop-user launch unavailable; launching from the existing administrator context");
        }
        created = CreateProcessW(invocation.launcher.c_str(), command_line.data(), nullptr, nullptr,
                                  FALSE, 0, nullptr, invocation.game_dir.c_str(), &startup, &information) != FALSE;
    }
    if (!created) {
        const DWORD error = GetLastError();
        if (error != ERROR_ELEVATION_REQUIRED && !elevation::permission_denied(error)) {
            throw AppError(ExitCode::process_creation, "launcher creation", windows_error(error));
        }
        if (elevation::is_elevated()) {
            throw AppError(ExitCode::process_creation, "launcher creation",
                           "Windows still denied the launcher while D2SteamFix was running as administrator: " +
                           windows_error(error));
        }
        log.info("Windows denied launcher creation; restoring permissions before requesting an administrator launch");
        throw elevation::RelaunchRequired(error, invocation.game_dir, invocation.launcher, steam_directory);
    }
    UniqueHandle process(information.hProcess);
    const UniqueHandle thread(information.hThread);
    return {std::move(process), information.dwProcessId};
}

std::string existing_processes(const std::vector<std::pair<DWORD, std::wstring>>& existing) {
    std::ostringstream output;
    output << "verified Destiny processes already running: [";
    for (size_t index = 0; index < existing.size(); ++index) {
        if (index != 0) output << ", ";
        output << '(' << existing[index].first << ", " << narrow(existing[index].second) << ')';
    }
    output << ']';
    return output.str();
}

std::vector<fs::path> discover_renderers(const fs::path& steam_directory, const Logger& log) {
    const fs::path renderer64 = steam_directory / constants::renderer64_name;
    try {
        require_regular_file(renderer64, ExitCode::steam_discovery, "renderer discovery",
                             "GameOverlayRenderer64.dll is missing or is not a regular file");
    } catch (AppError& error) {
        error.with_guidance(i18n::text(i18n::Msg::steam_discovery_guidance));
        throw;
    }
    std::vector<fs::path> targets{renderer64};
    log.info("selected renderer DLL: " + display_path(renderer64));
    const fs::path renderer32 = steam_directory / constants::renderer32_name;
    std::error_code renderer32_error;
    const bool renderer32_exists = fs::is_regular_file(renderer32, renderer32_error);
    if (renderer32_error) {
        throw AppError(ExitCode::steam_discovery, "renderer discovery",
                       display_path(renderer32) + ": " + renderer32_error.message())
            .with_guidance(i18n::text(i18n::Msg::steam_discovery_guidance));
    }
    if (renderer32_exists) {
        log.info("selected renderer DLL: " + display_path(renderer32));
        targets.push_back(renderer32);
    } else {
        log.warn("optional GameOverlayRenderer.dll is absent");
    }
    return targets;
}

[[noreturn]] void rethrow_session_error(const std::exception_ptr& session_error,
                                        const bool launcher_started) {
    try {
        std::rethrow_exception(session_error);
    } catch (const elevation::Cancelled&) {
        throw;
    } catch (const elevation::RelaunchRequired&) {
        throw;
    } catch (AppError& error) {
        if (launcher_started) error.after_launch();
        throw;
    } catch (const std::exception& error) {
        AppError converted(ExitCode::internal, "post-launch session", error.what());
        if (launcher_started) converted.after_launch();
        throw converted;
    } catch (...) {
        AppError converted(ExitCode::internal, "post-launch session", "unknown exception");
        if (launcher_started) converted.after_launch();
        throw converted;
    }
}

int run(const Logger& log, const fs::path& resolved_launcher,
        const std::vector<std::wstring>& forwarded, const fs::path& recovery) {
    const std::optional<std::wstring> app_id = environment(L"SteamAppId");
    const std::optional<std::wstring> game_id = environment(L"SteamGameId");
    log.info(narrow(L"inherited App IDs: SteamAppId=" + optional_id(app_id) +
                    L", SteamGameId=" + optional_id(game_id)));
    const Invocation invocation = validate_invocation(resolved_launcher, forwarded, app_id, game_id);
    log.info("validated launcher path: " + display_path(invocation.launcher));
    log.info("relevant App ID: 1085660");
    fs::path steam_directory;
    try {
        if (const std::optional<fs::path> inherited = elevation::inherited_steam_directory()) {
            steam_directory = *inherited;
        } else {
            steam_directory = steam::discover();
        }
    } catch (const std::exception& error) {
        throw AppError(ExitCode::steam_discovery, "Steam discovery", error.what())
            .with_guidance(i18n::text(i18n::Msg::steam_discovery_guidance));
    }
    log.info("Steam directory: " + display_path(steam_directory));
    check_overlay(OverlayCheck::advisory, log);
    std::vector<std::pair<DWORD, std::wstring>> existing;
    try {
        existing = process::matching_in_dir(invocation.game_dir);
    } catch (const std::exception& error) {
        throw AppError(ExitCode::existing_session, "existing-session check", error.what());
    }
    if (!existing.empty()) {
        throw AppError(ExitCode::existing_session, "existing-session check", existing_processes(existing))
            .with_guidance(i18n::text(i18n::Msg::destiny_running));
    }
    const std::vector<fs::path> targets = discover_renderers(steam_directory, log);

    const acl::RecoveryInspection inspection = acl::inspect_recovery(targets, recovery, log);
    if (inspection.status == acl::RecoveryStatus::absent) {
        if (repair::needed(steam_directory, log)) {
            log.warn("renderer execute access is blocked without a recovery backup; offering Steam repair");
            static_cast<void>(repair::run(invocation.game_dir, recovery,
                                          repair::Trigger::blocked_without_backup, log));
            return 0;
        }
    } else if (inspection.status == acl::RecoveryStatus::unusable) {
        bool healthy = false;
        try {
            const bool live_blocked = repair::needed(steam_directory, log) ||
                                      acl::any_has_current_user_execute_deny(targets);
            const bool saved_blocked =
                !inspection.saved_paths.empty() &&
                (repair::any_execute_blocked(inspection.saved_paths, log) ||
                 acl::any_has_current_user_execute_deny(inspection.saved_paths));
            healthy = !live_blocked && !saved_blocked;
        } catch (const elevation::Cancelled&) {
            throw;
        } catch (const std::exception& error) {
            log.warn("could not verify renderer permissions while inspecting stale recovery state: " +
                     std::string(error.what()));
            healthy = false;
        }
        if (healthy) {
            log.warn("existing steamfix.dat could not be reused (" + inspection.reason +
                     "); live renderer permissions are verified healthy, removing stale recovery state");
            acl::remove_stale_recovery(recovery, log);
        } else {
            log.warn("existing steamfix.dat could not be reused (" + inspection.reason +
                     ") while renderer execute access is blocked or unverifiable; preserving "
                     "recovery state and offering Steam repair");
            static_cast<void>(repair::run(invocation.game_dir, recovery,
                                          repair::Trigger::unusable_backup_blocked, log));
            return 0;
        }
    }

    acl::Protection protection = acl::Protection::apply(targets, recovery, log);
    log.info("renderer execute denial established for the current user");
    bool launcher_started = false;
    std::exception_ptr session_error;
    std::optional<int> exit_status;
    try {
        check_overlay(OverlayCheck::fatal, log);
        ChildProcess child = launch(invocation, steam_directory, log);
        launcher_started = true;
        log.info("launched destiny2launcher.exe; beginning fail-closed process monitoring");
        exit_status = process::monitor(child.process.get(), child.pid, invocation.game_dir, log);
    } catch (...) {
        session_error = std::current_exception();
    }

    try {
        protection.restore(log);
    } catch (const AppError& cleanup_error) {
        std::string detail = cleanup_error.detail();
        if (session_error) {
            try {
                std::rethrow_exception(session_error);
            } catch (const std::exception& error) {
                detail += "; preceding session failure: " + std::string(error.what());
            } catch (...) {
                detail += "; preceding session failure: unknown exception";
            }
        }
        AppError combined(ExitCode::overlay_protection, cleanup_error.stage(), std::move(detail));
        if (!cleanup_error.guidance().empty()) {
            combined.with_guidance(cleanup_error.guidance());
        } else {
            combined.with_guidance(i18n::text(i18n::Msg::overlay_protection_guidance));
        }
        if (launcher_started) combined.after_launch();
        throw combined;
    }
    if (session_error) rethrow_session_error(session_error, launcher_started);
    if (exit_status) {
        log.info("original launcher exit status: " + std::to_string(*exit_status));
    } else {
        log.warn("original launcher exit status was unavailable");
    }
    log.info("permission cleanup successful");
    return exit_status.value_or(0);
}

std::wstring error_code_suffix(const ExitCode code) {
    return i18n::text(i18n::Msg::error_code) + std::to_wstring(static_cast<int>(code));
}

std::wstring failure_message(const AppError& error) {
    // The exit code is what docs/EXIT_CODES.md is indexed by, and what makes a user report
    // identifiable; show it even when the rest of the text is friendly guidance.
    const std::wstring code_line = error_code_suffix(error.code());
    if (error.is_repair_failure()) {
        return i18n::text(i18n::Msg::repair_failure_intro) + widen(error.detail()) +
               i18n::text(i18n::Msg::see_log_only) + code_line;
    }
    if (!error.guidance().empty()) return error.guidance() + code_line;
    const std::wstring launch_status = error.launcher_started()
        ? i18n::text(i18n::Msg::launcher_started) : i18n::text(i18n::Msg::game_not_started);
    return i18n::text(i18n::Msg::failure_intro) + widen(error.stage()) +
           i18n::text(i18n::Msg::failure_stage_suffix) + widen(error.detail()) +
           i18n::text(i18n::Msg::see_log) + launch_status + code_line;
}

bool validate_app_ids(const std::optional<std::wstring>& app_id,
                      const std::optional<std::wstring>& game_id) noexcept {
    const auto invalid = [](const std::optional<std::wstring>& value) {
        return value && *value != L"1085660";
    };
    if (invalid(app_id) || invalid(game_id)) return false;
    return (app_id && *app_id == L"1085660") || (game_id && *game_id == L"1085660");
}

fs::path resolve_launcher(const std::wstring& argument) {
    const fs::path raw(argument);
    if (!paths::name_is(raw, L"destiny2launcher.exe")) {
        throw AppError(ExitCode::validation, "command validation",
                       "the forwarded executable must be destiny2launcher.exe, but was " +
                       display_path(raw.filename()))
            .with_guidance(i18n::text(i18n::Msg::wrong_forwarded_executable));
    }
    fs::path launcher;
    try {
        launcher = fs::canonical(raw);
    } catch (const std::exception& error) {
        throw AppError(ExitCode::validation, "command validation",
                       "launcher is not an existing regular file: " + std::string(error.what()));
    }
    require_regular_file(launcher, ExitCode::validation, "command validation",
                         "launcher is not an existing regular file");
    if (!paths::name_is(launcher, L"destiny2launcher.exe")) {
        throw AppError(ExitCode::validation, "command validation",
                       "launcher is not a regular destiny2launcher.exe file");
    }
    return launcher;
}

Invocation validate_invocation(const fs::path& launcher, const std::vector<std::wstring>& args,
                               const std::optional<std::wstring>& app_id,
                               const std::optional<std::wstring>& game_id) {
    const fs::path game_dir = launcher.parent_path();
    if (game_dir.empty()) {
        throw AppError(ExitCode::validation, "command validation", "launcher has no containing directory");
    }
    require_regular_file(game_dir / L"destiny2.exe", ExitCode::validation,
                         "command validation",
                         "destiny2.exe is not a regular file beside the launcher");
    if (!validate_app_ids(app_id, game_id)) {
        throw AppError(ExitCode::validation, "Steam App ID validation",
                       "SteamAppId and SteamGameId must be absent or 1085660, and at least one must equal 1085660")
            .with_guidance(i18n::text(i18n::Msg::wrong_steam_app_id));
    }
    return {launcher, game_dir, std::vector<std::wstring>(args.begin() + 1, args.end())};
}

} // namespace

int entry() {
    std::vector<std::wstring> forwarded;
    try {
        forwarded = command_line_arguments();
        if (const std::optional<int> helper = elevation::dispatch_helper(forwarded)) return *helper;
        elevation::accept_elevated_launch(forwarded);
    } catch (const std::exception& error) {
        ui::error(i18n::text(i18n::Msg::command_line_failure) + widen(error.what()) +
                  error_code_suffix(ExitCode::invalid_invocation));
        return static_cast<int>(ExitCode::invalid_invocation);
    }
    fs::path executable;
    try {
        executable = process::current_executable();
    } catch (const std::exception& error) {
        ui::error(i18n::text(i18n::Msg::executable_path_failure) + widen(error.what()) + L"\n\n" +
                  i18n::text(i18n::Msg::game_not_started) + error_code_suffix(ExitCode::internal));
        return static_cast<int>(ExitCode::internal);
    }
    std::optional<fs::path> resolved_launcher;
    fs::path game_directory;
    try {
        if (forwarded.empty()) {
            game_directory = fs::canonical(executable.parent_path());
        } else {
            resolved_launcher = resolve_launcher(forwarded.front());
            game_directory = resolved_launcher->parent_path();
        }
    } catch (const AppError& error) {
        ui::error(failure_message(error));
        return static_cast<int>(error.code());
    } catch (const std::exception& error) {
        ui::error(i18n::text(i18n::Msg::game_directory_failure) + widen(error.what()) + L"\n\n" +
                  i18n::text(i18n::Msg::game_not_started) +
                  error_code_suffix(ExitCode::log_initialization));
        return static_cast<int>(ExitCode::log_initialization);
    }
    std::unique_ptr<Logger> log;
    try {
        log = std::make_unique<Logger>(game_directory / L"steamfix.log");
    } catch (const elevation::Cancelled&) {
        return 0;
    } catch (const std::exception& error) {
        ui::error(i18n::text(i18n::Msg::log_open_failure) + widen(error.what()) + L"\n\n" +
                  i18n::text(i18n::Msg::game_not_started) +
                  error_code_suffix(ExitCode::log_initialization));
        return static_cast<int>(ExitCode::log_initialization);
    }
    log->info("startup; wrapper path: " + display_path(executable) +
              "; game directory: " + display_path(game_directory));
    try {
        const fs::path recovery = acl::recovery_path(game_directory);
        log->info("ACL recovery state path: " + display_path(recovery));
        if (forwarded.empty()) {
            static_cast<void>(repair::run(game_directory, recovery,
                                          repair::Trigger::direct_launch, *log));
            log->info("direct Steam repair finished; final exit status: 0");
            return 0;
        }
        const SessionMutex mutex = session_mutex();
        const int code = run(*log, *resolved_launcher, forwarded, recovery);
        log->info("final exit status: " + std::to_string(code));
        return code;
    } catch (const elevation::RelaunchRequired& request) {
        // The protected session and its mutex have both unwound before UAC.
        try {
            log->info("original renderer permissions restored; requesting an administrator D2SteamFix session");
            std::vector<std::wstring> retry = forwarded;
            // Resolve a relative Steam command once, in its original directory.
            retry.front() = request.launcher().native();
            const std::optional<int> code = elevation::relaunch_as_administrator(retry, request.directory(),
                                                                                     request.steam_directory());
            if (code) {
                log->info("administrator D2SteamFix session exit status: " + std::to_string(*code));
            } else {
                log->warn("administrator D2SteamFix session exited; Windows did not provide its exit status");
            }
            return code.value_or(0);
        } catch (const elevation::Cancelled&) {
            log->info("Windows permission request cancelled; original permissions remain restored");
            return 0;
        } catch (const std::exception& error) {
            log->error("administrator session launch or monitoring failed: " + std::string(error.what()));
            ui::error(i18n::text(i18n::Msg::admin_session_failure_intro) + widen(error.what()) +
                      i18n::text(i18n::Msg::admin_session_failure_outro) +
                      error_code_suffix(ExitCode::process_creation));
            return static_cast<int>(ExitCode::process_creation);
        }
    } catch (const elevation::Cancelled&) {
        log->info("Windows permission request cancelled; no launcher was started");
        return 0;
    } catch (const AppError& error) {
        log->error(std::string(error.what()) + "; helper exit code=" +
                   std::to_string(static_cast<int>(error.code())));
        ui::error(failure_message(error));
        return static_cast<int>(error.code());
    } catch (const std::exception& error) {
        log->error("unexpected internal failure: " + std::string(error.what()) + "; helper exit code=19");
        ui::error(i18n::text(i18n::Msg::internal_failure) + i18n::text(i18n::Msg::see_log) +
                  i18n::text(i18n::Msg::game_not_started) + error_code_suffix(ExitCode::internal));
        return static_cast<int>(ExitCode::internal);
    } catch (...) {
        log->error("unexpected internal failure; helper exit code=19");
        ui::error(i18n::text(i18n::Msg::internal_failure) + i18n::text(i18n::Msg::see_log) +
                  i18n::text(i18n::Msg::game_not_started) + error_code_suffix(ExitCode::internal));
        return static_cast<int>(ExitCode::internal);
    }
}

} // namespace steamfix::app
