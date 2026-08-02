#include "steamfix/app.hpp"

#include "steamfix/acl.hpp"
#include "steamfix/command_line.hpp"
#include "steamfix/error.hpp"
#include "steamfix/i18n.hpp"
#include "steamfix/logging.hpp"
#include "steamfix/overlay.hpp"
#include "steamfix/paths.hpp"
#include "steamfix/process.hpp"
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
Invocation validate_invocation(const std::vector<std::wstring>& args,
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
    if (raw == nullptr) throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> guard(raw, &LocalFree);
    std::vector<std::wstring> result;
    for (int index = 1; index < count; ++index) result.emplace_back(raw[index]);
    return result;
}

fs::path current_executable() {
    std::wstring buffer(32768, L'\0');
    const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size()) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    }
    buffer.resize(written);
    return fs::path(buffer);
}

fs::path current_directory() {
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    if (needed == 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetCurrentDirectoryW(needed, buffer.data());
    if (written == 0 || written >= needed) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    }
    buffer.resize(written);
    return fs::path(buffer);
}

UniqueHandle session_mutex() {
    SetLastError(ERROR_SUCCESS);
    HANDLE raw = CreateMutexW(nullptr, TRUE, L"Global\\D2SteamFix-1085660");
    if (raw == nullptr) {
        throw AppError(ExitCode::existing_session, "single-instance check", windows_error());
    }
    const bool already_exists = GetLastError() == ERROR_ALREADY_EXISTS;
    UniqueHandle handle(raw);
    if (already_exists) {
        throw AppError(ExitCode::existing_session, "single-instance check",
                       "another D2SteamFix session is already active on this computer");
    }
    return handle;
}

enum class Elevation {
    // Either a limited (UAC-filtered) token or a plain standard-user token.
    none,
    // An unfiltered administrator token that the user cannot opt out of: UAC is disabled
    // machine-wide (EnableLUA=0), or this is the built-in Administrator account running with
    // FilterAdministratorToken=0. Refusing here would leave no way to run the wrapper at all.
    unfiltered_admin,
    // A genuinely elevated token. The manifest requests asInvoker, so this can only have come
    // from the launching process or a "run as administrator" compatibility layer.
    elevated,
};

bool token_reports_elevated(const HANDLE token) {
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    }
    if (returned != sizeof(elevation)) throw std::runtime_error("invalid token-elevation result size");
    return elevation.TokenIsElevated != 0;
}

Elevation process_elevation() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    }
    const UniqueHandle token(raw);
    TOKEN_ELEVATION_TYPE type{};
    DWORD returned = 0;
    if (!GetTokenInformation(token.get(), TokenElevationType, &type, sizeof(type), &returned)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    }
    if (returned != sizeof(type)) throw std::runtime_error("invalid token-elevation-type result size");
    if (type == TokenElevationTypeFull) return Elevation::elevated;
    if (type == TokenElevationTypeLimited) return Elevation::none;
    // TokenElevationTypeDefault covers both a standard user and an administrator on a system
    // where UAC token filtering is not in effect. Only the latter reports an elevated token.
    return token_reports_elevated(token.get()) ? Elevation::unfiltered_admin : Elevation::none;
}

std::string elevation_name(const Elevation value) {
    switch (value) {
        case Elevation::none: return "none";
        case Elevation::unfiltered_admin: return "unfiltered administrator token";
        case Elevation::elevated: return "elevated";
    }
    return "unknown";
}

struct ParentProcess {
    DWORD pid;
    std::wstring name;
    std::optional<bool> elevated;
};

// Best-effort identification of the process that launched this one, used only to make the
// privilege-check failure actionable.
// ponytail: the pid comes from a Toolhelp32 snapshot and could in principle name a recycled
// process; acceptable for diagnostic text, revisit if it ever drives a decision.
std::optional<ParentProcess> immediate_parent() {
    try {
        const std::vector<DWORD> chain = process::parent_chain();
        if (chain.empty()) return std::nullopt;
        const DWORD pid = chain.front();
        const UniqueHandle handle(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid));
        ParentProcess parent{pid, process::image_path(handle.get()).filename().native(), std::nullopt};
        try {
            HANDLE raw = nullptr;
            if (!OpenProcessToken(handle.get(), TOKEN_QUERY, &raw)) {
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
            }
            const UniqueHandle token(raw);
            parent.elevated = token_reports_elevated(token.get());
        } catch (const std::exception&) {
            // Leave the elevation unknown; the caller falls back to naming both candidates.
        }
        return parent;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

struct ElevationReport {
    // English, and the only half that reaches the log.
    std::string detail;
    // The user's language, shown in the message box.
    std::wstring guidance;
};

ElevationReport elevated_report() {
    const std::optional<ParentProcess> parent = immediate_parent();
    ElevationReport report{
        "running with an administrator token that was not requested; the manifest declares asInvoker",
        i18n::text(i18n::Msg::elevated_intro)};
    if (!parent) {
        report.guidance += i18n::text(i18n::Msg::elevated_fix_both);
        return report;
    }
    const std::wstring source = parent->name + L" (pid " + std::to_wstring(parent->pid) + L")";
    report.detail += "; launched by " + narrow(source);
    report.guidance += i18n::text(i18n::Msg::elevated_source) + source;
    if (parent->elevated.value_or(false)) {
        report.detail += " which is itself elevated";
        report.guidance += i18n::text(i18n::Msg::elevated_fix_named);
    } else {
        report.detail += " which does not report an elevated token";
        report.guidance += i18n::text(i18n::Msg::elevated_fix_both);
    }
    return report;
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

ChildProcess launch(const Invocation& invocation) {
    std::wstring command_line;
    command_line::append_quoted(command_line, invocation.launcher.native());
    for (const std::wstring& argument : invocation.args) {
        command_line::append_quoted(command_line, argument);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION information{};
    if (!CreateProcessW(invocation.launcher.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, invocation.game_dir.c_str(), &startup, &information)) {
        throw AppError(ExitCode::process_creation, "launcher creation", windows_error());
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

int run(const Logger& log, const std::vector<std::wstring>& forwarded, const fs::path& recovery) {
    const std::optional<std::wstring> app_id = environment(L"SteamAppId");
    const std::optional<std::wstring> game_id = environment(L"SteamGameId");
    log.info(narrow(L"inherited App IDs: SteamAppId=" + optional_id(app_id) +
                    L", SteamGameId=" + optional_id(game_id)));
    const Invocation invocation = validate_invocation(forwarded, app_id, game_id);
    log.info("validated launcher path: " + display_path(invocation.launcher));
    log.info("relevant App ID: 1085660");
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

    fs::path steam_directory;
    try {
        steam_directory = steam::discover();
    } catch (const std::exception& error) {
        throw AppError(ExitCode::steam_discovery, "Steam discovery", error.what());
    }
    log.info("Steam directory: " + display_path(steam_directory));
    const fs::path renderer64 = steam_directory / L"GameOverlayRenderer64.dll";
    require_regular_file(renderer64, ExitCode::steam_discovery, "renderer discovery",
                         "GameOverlayRenderer64.dll is missing or is not a regular file");
    std::vector<fs::path> targets{renderer64};
    log.info("selected renderer DLL: " + display_path(renderer64));
    const fs::path renderer32 = steam_directory / L"GameOverlayRenderer.dll";
    std::error_code renderer32_error;
    const bool renderer32_exists = fs::is_regular_file(renderer32, renderer32_error);
    if (renderer32_error) {
        throw AppError(ExitCode::steam_discovery, "renderer discovery",
                       display_path(renderer32) + ": " + renderer32_error.message());
    }
    if (renderer32_exists) {
        log.info("selected renderer DLL: " + display_path(renderer32));
        targets.push_back(renderer32);
    } else {
        log.warn("optional GameOverlayRenderer.dll is absent");
    }

    acl::Protection protection = acl::Protection::apply(targets, recovery, log);
    log.info("renderer execute denial established for the current user");
    bool launcher_started = false;
    std::exception_ptr session_error;
    std::optional<int> exit_status;
    try {
        check_overlay(OverlayCheck::fatal, log);
        ChildProcess child = launch(invocation);
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
        if (launcher_started) combined.after_launch();
        throw combined;
    }
    if (session_error) {
        try {
            std::rethrow_exception(session_error);
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
    if (exit_status) {
        log.info("original launcher exit status: " + std::to_string(*exit_status));
    } else {
        log.warn("original launcher exit status was unavailable");
    }
    log.info("permission cleanup successful");
    return exit_status.value_or(0);
}

std::wstring failure_message(const AppError& error) {
    // The exit code is what docs/EXIT_CODES.md is indexed by, and what makes a user report
    // identifiable; show it even when the rest of the text is friendly guidance.
    const std::wstring code_line =
        i18n::text(i18n::Msg::error_code) + std::to_wstring(static_cast<int>(error.code()));
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

Invocation validate_invocation(const std::vector<std::wstring>& args,
                               const std::optional<std::wstring>& app_id,
                               const std::optional<std::wstring>& game_id) {
    if (args.empty()) {
        throw AppError(ExitCode::invalid_invocation, "invalid invocation",
                       "no Destiny 2 command was forwarded")
            .with_guidance(i18n::text(i18n::Msg::invalid_invocation));
    }
    const fs::path raw(args.front());
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
    const fs::path game_dir = launcher.parent_path();
    if (game_dir.empty()) {
        throw AppError(ExitCode::validation, "command validation", "launcher has no containing directory");
    }
    require_regular_file(game_dir / L"destiny2.exe", ExitCode::validation,
                         "command validation",
                         "destiny2.exe is not a regular file beside the launcher");
    if (!validate_app_ids(app_id, game_id)) {
        throw AppError(ExitCode::validation, "Steam App ID validation",
                       "SteamAppId and SteamGameId must be absent or 1085660, and at least one must equal 1085660");
    }
    return {launcher, game_dir, std::vector<std::wstring>(args.begin() + 1, args.end())};
}

} // namespace

int entry() {
    fs::path executable;
    try {
        executable = current_executable();
    } catch (const std::exception& error) {
        ui::error(i18n::text(i18n::Msg::executable_path_failure) + widen(error.what()) + L"\n\n" +
                  i18n::text(i18n::Msg::game_not_started));
        return static_cast<int>(ExitCode::internal);
    }
    fs::path cwd;
    try {
        cwd = current_directory();
    } catch (const std::exception& error) {
        ui::error(i18n::text(i18n::Msg::working_directory_failure) + widen(error.what()) + L"\n\n" +
                  i18n::text(i18n::Msg::game_not_started));
        return static_cast<int>(ExitCode::log_initialization);
    }
    std::unique_ptr<Logger> log;
    try {
        log = std::make_unique<Logger>(cwd / L"steamfix.log");
    } catch (const std::exception& error) {
        ui::error(i18n::text(i18n::Msg::log_open_failure) + widen(error.what()) + L"\n\n" +
                  i18n::text(i18n::Msg::game_not_started));
        return static_cast<int>(ExitCode::log_initialization);
    }
    log->info("startup; wrapper path: " + display_path(executable) +
              "; current working directory: " + display_path(cwd));
    try {
        const std::vector<std::wstring> forwarded = command_line_arguments();
        const UniqueHandle mutex = session_mutex();
        const fs::path recovery = acl::recovery_path(cwd);
        log->info("ACL recovery state path: " + display_path(recovery));
        std::error_code recovery_error;
        const bool recovery_exists = fs::exists(recovery, recovery_error);
        if (recovery_error) {
            throw AppError(ExitCode::overlay_protection, "ACL recovery safety check",
                           display_path(recovery) + ": " + recovery_error.message());
        }
        if (recovery_exists) {
            ui::info(i18n::literal(i18n::Msg::recovery_prompt));
            try {
                process::wait_for_destiny_quiescence(*log);
            } catch (const std::exception& error) {
                throw AppError(ExitCode::overlay_protection, "ACL recovery safety check",
                               "could not establish that Destiny is closed; recovery state was preserved: " +
                               std::string(error.what()));
            }
        }
        const bool recovered = acl::recover_pending(recovery, *log);
        // Reported before the privilege gate: someone who double-clicks steamfix.exe (elevated or
        // not) needs to be told it belongs in Steam's launch options, not about their token.
        if (forwarded.empty()) {
            const std::wstring prefix =
                recovered ? i18n::text(i18n::Msg::recovered_prefix) : std::wstring{};
            throw AppError(ExitCode::invalid_invocation, "invalid invocation",
                           recovered ? "restored an interrupted session; no Destiny 2 command "
                                       "was forwarded"
                                     : "no Destiny 2 command was forwarded")
                .with_guidance(prefix + i18n::text(i18n::Msg::invalid_invocation));
        }
        const Elevation elevation = process_elevation();
        log->info("token elevation: " + elevation_name(elevation));
        switch (elevation) {
            case Elevation::none:
                break;
            case Elevation::unfiltered_admin:
                log->warn("UAC token filtering is not in effect on this system (UAC disabled, or the "
                          "built-in Administrator account); every process of this account runs with a "
                          "full administrator token, so refusing would not lower the game's privileges. "
                          "Continuing.");
                break;
            case Elevation::elevated: {
                const ElevationReport report = elevated_report();
                throw AppError(ExitCode::validation, "privilege check", report.detail)
                    .with_guidance(report.guidance);
            }
        }
        const int code = run(*log, forwarded, recovery);
        log->info("final exit status: " + std::to_string(code));
        return code;
    } catch (const AppError& error) {
        log->error(std::string(error.what()) + "; helper exit code=" +
                   std::to_string(static_cast<int>(error.code())));
        ui::error(failure_message(error));
        return static_cast<int>(error.code());
    } catch (const std::exception& error) {
        log->error("unexpected internal failure: " + std::string(error.what()) + "; helper exit code=19");
        ui::error(i18n::text(i18n::Msg::internal_failure) + i18n::text(i18n::Msg::see_log) +
                  i18n::text(i18n::Msg::game_not_started));
        return static_cast<int>(ExitCode::internal);
    } catch (...) {
        log->error("unexpected internal failure; helper exit code=19");
        ui::error(i18n::text(i18n::Msg::internal_failure) + i18n::text(i18n::Msg::see_log) +
                  i18n::text(i18n::Msg::game_not_started));
        return static_cast<int>(ExitCode::internal);
    }
}

} // namespace steamfix::app
