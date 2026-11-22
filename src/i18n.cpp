#include "steamfix/i18n.hpp"

#include <windows.h>

#include <array>
#include <cstddef>

namespace steamfix::i18n {
namespace {

struct Entry {
    const wchar_t* en;
    const wchar_t* zh;
};

// Indexed by Msg; keep this table in the same order as the enumeration.
constexpr std::array entries{
    Entry{L"D2SteamFix could not read its command line: ",
          L"D2SteamFix 无法读取命令行参数："},
    Entry{L"D2SteamFix could not determine its executable path: ",
          L"D2SteamFix 无法确定自身的可执行文件路径："},
    Entry{L"D2SteamFix could not determine the Destiny 2 directory for its log and "
          L"recovery files.\n\n",
          L"D2SteamFix 无法确定用于存放日志与恢复文件的 Destiny 2 目录。\n\n"},
    Entry{L"D2SteamFix could not open steamfix.log in the Destiny 2 directory. "
          L"Ensure the game directory is writable.\n\n",
          L"D2SteamFix 无法在 Destiny 2 目录中打开 steamfix.log。请确认游戏目录可写。\n\n"},
    Entry{L"D2SteamFix could not complete its administrator session.\n\n",
          L"D2SteamFix 未能完成管理员会话。\n\n"},
    Entry{L"\n\nOriginal renderer permissions were restored before the administrator session "
          L"was requested.",
          L"\n\n在请求管理员会话之前，已恢复原始渲染器权限。"},
    Entry{L"Steam file repair could not finish.\n\n",
          L"Steam 文件修复未能完成。\n\n"},
    Entry{L"Opening steamfix.exe directly starts Steam overlay file repair. To play Destiny 2, "
          L"launch it from Steam.\n\nSteam and Destiny 2 will close so Steam can repair its files.",
          L"直接打开 steamfix.exe 会启动 Steam 覆盖层文件修复。如需游玩 Destiny 2，"
          L"请从 Steam 启动游戏。\n\n继续后将关闭 Steam 和 Destiny 2，以便 Steam 修复其文件。"},
    Entry{L"Steam overlay permissions are blocked and no recovery backup was found.\n\n"
          L"Steam and Destiny 2 will close so Steam can repair its files.",
          L"Steam 覆盖层文件权限已被阻止，且未找到恢复备份。\n\n"
          L"继续后将关闭 Steam 和 Destiny 2，以便 Steam 修复其文件。"},
    Entry{L"Steam overlay permissions are blocked and the existing recovery backup cannot be "
          L"reused.\n\nSteam and Destiny 2 will close so Steam can repair its files.",
          L"Steam 覆盖层文件权限已被阻止，且现有恢复备份无法复用。\n\n"
          L"继续后将关闭 Steam 和 Destiny 2，以便 Steam 修复其文件。"},
    Entry{L"Continue", L"继续"},
    Entry{L"Cancel", L"取消"},
    Entry{L"Locate Steam's steam.exe", L"定位 Steam 的 steam.exe"},
    Entry{L"The selected file is not a valid Steam installation. Select Steam's steam.exe file.",
          L"所选文件不是有效的 Steam 安装。请选择 Steam 目录下的 steam.exe 文件。"},
    Entry{L"Steam repaired its files. You can start Steam again.",
          L"Steam 已完成文件修复，现在可以重新启动 Steam。"},
    Entry{L"D2SteamFix encountered an unexpected internal failure.",
          L"D2SteamFix 遇到意外的内部错误。"},
    Entry{L"D2SteamFix failed during ", L"D2SteamFix 在「"},
    Entry{L":\n\n", L"」阶段失败：\n\n"},
    Entry{L"\n\nSee steamfix.log in the Destiny 2 directory.\n\n",
          L"\n\n详见 Destiny 2 目录下的 steamfix.log。\n\n"},
    Entry{L"\n\nSee steamfix.log in the Destiny 2 directory.",
          L"\n\n详见 Destiny 2 目录下的 steamfix.log。"},
    Entry{L"\n\nError code ", L"\n\n错误代码 "},
    Entry{L"Destiny 2 is already running.\n\nClose the game completely and try again. If it is "
          L"not visible, end destiny2.exe and destiny2launcher.exe in Task Manager.",
          L"Destiny 2 已在运行。\n\n请完全关闭游戏后重试。如果界面上看不到游戏，"
          L"请在任务管理器中结束 destiny2.exe 与 destiny2launcher.exe。"},
    Entry{L"Another D2SteamFix session is already running.\n\nWait for it to finish, or close "
          L"any running steamfix.exe and Destiny 2 processes before trying again.",
          L"另一个 D2SteamFix 会话已在运行。\n\n请等待其结束，或关闭正在运行的 "
          L"steamfix.exe 与 Destiny 2 进程后再重试。"},
    Entry{L"Steam did not forward a Destiny 2 command to D2SteamFix.\n\nCheck the launch options: "
          L"steamfix.exe must come before %command%, with the whole path in quotes.\n\n"
          L"\"C:\\path\\to\\steamfix.exe\" %command%",
          L"Steam 没有向 D2SteamFix 转发 Destiny 2 的启动命令。\n\n请检查启动选项："
          L"steamfix.exe 必须写在 %command% 之前，且完整路径需要用引号括起。\n\n"
          L"\"C:\\路径\\steamfix.exe\" %command%"},
    Entry{L"D2SteamFix must be launched from Steam for Destiny 2.\n\nSet Destiny 2's Steam "
          L"launch options to:\n\"C:\\path\\to\\steamfix.exe\" %command%\n\n"
          L"Destiny 2 was not started.",
          L"D2SteamFix 必须通过 Steam 为 Destiny 2 启动。\n\n请将 Steam 中 Destiny 2 的"
          L"启动选项设为：\n\"C:\\路径\\steamfix.exe\" %command%\n\nDestiny 2 未启动。"},
    Entry{L"D2SteamFix could not locate Steam or its overlay renderer DLL.\n\nMake sure Steam "
          L"is installed and up to date, or double-click steamfix.exe to run Steam file repair."
          L"\n\nSee steamfix.log in the Destiny 2 directory.\n\nDestiny 2 was not started.",
          L"D2SteamFix 无法定位 Steam 或其覆盖层渲染器 DLL。\n\n请确认 Steam 已正确安装并更新，"
          L"或直接双击 steamfix.exe 运行 Steam 文件修复。\n\n详见 Destiny 2 目录下的 "
          L"steamfix.log。\n\nDestiny 2 未启动。"},
    Entry{L"D2SteamFix could not update or restore Steam overlay permissions.\n\nDouble-click "
          L"steamfix.exe directly to run Steam file repair and restore Steam's overlay files."
          L"\n\nSee steamfix.log in the Destiny 2 directory.",
          L"D2SteamFix 无法修改或恢复 Steam 覆盖层文件权限。\n\n请直接双击 steamfix.exe "
          L"运行 Steam 文件修复，以恢复 Steam 覆盖层文件。\n\n详见 Destiny 2 目录下的 "
          L"steamfix.log。"},
    Entry{L"The Destiny 2 launcher was started.", L"Destiny 2 启动器已启动。"},
    Entry{L"Destiny 2 was not started.", L"Destiny 2 未启动。"},
    Entry{L"D2SteamFix could not format this message.", L"D2SteamFix 无法显示此消息。"},
};

static_assert(entries.size() == static_cast<std::size_t>(Msg::count));

bool prefer_chinese() noexcept {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

} // namespace

const wchar_t* literal(const Msg id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index >= entries.size()) return L"";
    return prefer_chinese() ? entries[index].zh : entries[index].en;
}

std::wstring text(const Msg id) { return literal(id); }

} // namespace steamfix::i18n
