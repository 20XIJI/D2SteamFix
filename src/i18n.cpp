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
    Entry{L"D2SteamFix restored renderer permissions left by an interrupted session.\n\n",
          L"D2SteamFix 已恢复上次中断的会话遗留的渲染器权限。\n\n"},
    Entry{L"D2SteamFix could not determine its executable path: ",
          L"D2SteamFix 无法确定自身的可执行文件路径："},
    Entry{L"D2SteamFix could not determine the current working directory for its log and "
          L"recovery files.\n\n",
          L"D2SteamFix 无法确定用于存放日志与恢复文件的当前工作目录。\n\n"},
    Entry{L"D2SteamFix could not open steamfix.log in the current working directory. "
          L"Ensure the working directory is writable.\n\n",
          L"D2SteamFix 无法在当前工作目录中打开 steamfix.log。请确认该目录可写。\n\n"},
    Entry{L"D2SteamFix found interrupted permission recovery state.\n\n"
          L"Close Destiny 2 before continuing. After you select OK, D2SteamFix waits for every "
          L"Destiny process to exit, then requires 30 seconds of quiet before restoring "
          L"permissions. It gives up after two minutes of waiting.",
          L"D2SteamFix 发现上次中断留下的权限恢复状态。\n\n"
          L"请先关闭 Destiny 2 再继续。点击「确定」后，D2SteamFix 会等待所有 Destiny 进程退出，"
          L"再确认连续 30 秒无进程活动，然后恢复权限。等待超过两分钟则放弃。"},
    Entry{L"D2SteamFix is running with an administrator token. It never asks for one: its "
          L"manifest declares asInvoker, so the token came from whatever started it.",
          L"D2SteamFix 正在以管理员令牌运行。它自己从不请求提权——程序清单声明为 asInvoker——"
          L"因此这个令牌来自启动它的程序。"},
    Entry{L"\n\nElevated by: ", L"\n\n提权来源："},
    Entry{L"\n\nRight-click that program, select Properties -> Compatibility, and clear \"Run "
          L"this program as an administrator\" (clear it under \"Change settings for all users\" "
          L"as well), then start it again.",
          L"\n\n右键该程序，选择「属性」→「兼容性」，取消勾选「以管理员身份运行此程序」"
          L"（「更改所有用户的设置」中同样取消），然后重新启动它。"},
    Entry{L"\n\nRight-click steamfix.exe and Steam, select Properties -> Compatibility, and clear "
          L"\"Run this program as an administrator\" on both (clear it under \"Change settings "
          L"for all users\" as well), then restart Steam.",
          L"\n\n分别右键 steamfix.exe 与 Steam，选择「属性」→「兼容性」，两者都取消勾选"
          L"「以管理员身份运行此程序」（「更改所有用户的设置」中同样取消），然后重启 Steam。"},
    Entry{L"D2SteamFix encountered an unexpected internal failure.",
          L"D2SteamFix 遇到意外的内部错误。"},
    Entry{L"D2SteamFix failed during ", L"D2SteamFix 在「"},
    Entry{L":\n\n", L"」阶段失败：\n\n"},
    Entry{L"\n\nSee steamfix.log in the current working directory.\n\n",
          L"\n\n详见当前工作目录下的 steamfix.log。\n\n"},
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

// ponytail: a single Chinese variant selected on the primary language id. Add SUBLANGID
// handling if traditional-Chinese wording is ever asked for.
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
