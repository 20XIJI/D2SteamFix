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
    Entry{L"D2SteamFix was not started by Steam with a Destiny 2 command.\n\n"
          L"This program must be run from Steam using launch options:\n"
          L"\"C:\\path\\to\\steamfix.exe\" %command%\n\nDestiny 2 was not started.",
          L"D2SteamFix 不是由 Steam 以 Destiny 2 的启动命令调起的。\n\n"
          L"请在 Steam 的启动选项中按以下格式使用本程序：\n"
          L"\"C:\\路径\\steamfix.exe\" %command%\n\nDestiny 2 未启动。"},
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
    Entry{L"Steam and Destiny 2 will close so Steam can repair its files.",
          L"即将关闭 Steam 和 Destiny 2，以便 Steam 修复其文件。"},
    Entry{L"Continue", L"继续"},
    Entry{L"Cancel", L"取消"},
    Entry{L"Locate Steam's steam.exe", L"定位 Steam 的 steam.exe"},
    Entry{L"Select Steam's steam.exe file.\n\n", L"请选择 Steam 的 steam.exe 文件。\n\n"},
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
    Entry{L"Steam did not forward a Destiny 2 command to D2SteamFix.\n\nCheck the launch options: "
          L"steamfix.exe must come before %command%, with the whole path in quotes.\n\n"
          L"\"C:\\path\\to\\steamfix.exe\" %command%",
          L"Steam 没有向 D2SteamFix 转发 Destiny 2 的启动命令。\n\n请检查启动选项："
          L"steamfix.exe 必须写在 %command% 之前，且完整路径需要用引号括起。\n\n"
          L"\"C:\\路径\\steamfix.exe\" %command%"},
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
