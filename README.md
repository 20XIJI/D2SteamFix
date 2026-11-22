# D2SteamFix

[English](README.en.md)

D2SteamFix 是 Steam 版 Destiny 2 的启动包装程序，用于缓解帧生成时间不稳与卡顿问题。它在启动 Destiny 2 之前临时屏蔽 Steam 覆盖层渲染器，并在游戏退出后恢复原有权限。

屏蔽 Steam 覆盖层会同时影响 Steam Input、通知、游戏录制与远程同乐等功能。正常启动游戏时，D2SteamFix 只修改覆盖层 DLL 的权限元数据，不改动文件内容；独立的修复模式则会通过 Steam 重新下载覆盖层文件。D2SteamFix 不注入代码，不读取进程内存，不修改游戏文件，不更改 Steam 配置，也不绕过 BattlEye。

## 安装

下载 [steamfix.exe](https://github.com/20XIJI/D2SteamFix/releases/latest/download/steamfix.exe)，直接放入 Destiny 2 游戏目录。

然后在 Steam 中配置 Destiny 2 的启动选项：

1. 在 Steam 库中右键 **Destiny 2**，选择**属性**。
2. 在**通用**选项卡下找到**启动选项**输入框，粘贴下面的命令：

```text
"C:\路径\Destiny 2\steamfix.exe" %command%
```

把 `C:\路径\Destiny 2` 替换为 Destiny 2 的实际安装位置。以下为 Steam 默认安装路径示例，如游戏安装在其他盘符，请使用完整路径：

```text
"C:\Program Files (x86)\Steam\steamapps\common\Destiny 2\steamfix.exe" %command%
```

![Steam 启动选项](https://static.d2checkpoint.com/steamfix_launch_options.png)

## 正常启动与确认生效

像往常一样从 Steam 启动 Destiny 2。D2SteamFix 会在游戏启动前屏蔽覆盖层，并在游戏退出后自动恢复权限。如果游戏目录或覆盖层文件需要管理员权限，程序会按需请求 Windows UAC 授权。

进入游戏后按 **Shift+Tab**，若覆盖层没有弹出，说明 D2SteamFix 正在生效；退出游戏后覆盖层会自动恢复。

## 异常中断的会话

D2SteamFix 会将原始覆盖层权限备份 `steamfix.dat` 与日志 `steamfix.log` 保存在 Destiny 2 游戏目录中（以 Steam 转发的 `destiny2launcher.exe` 所在目录为准，不受当前工作目录影响）。

如果游戏、Steam 或系统异常中断，下一次从 Steam 启动时会自动复用已有的 `steamfix.dat` 备份，保持覆盖层处于屏蔽状态，直到 Destiny 2 正常退出后再恢复原始权限。在恢复成功前请勿删除 `steamfix.dat`，否则可能导致其他游戏的 Steam 覆盖层保持失效。

若保存的备份与当前渲染器文件或权限不再匹配，D2SteamFix 会保留原备份并报告不匹配；此时可直接双击 `steamfix.exe` 使用下方的修复模式。

## 修复 Steam 覆盖层权限

在 Destiny 2 目录中直接双击 `steamfix.exe` 即可进入 Steam 覆盖层修复模式（适用于恢复备份丢失或不可用的场景）。正常通过 Steam 启动时，若检测到覆盖层文件已被禁止执行且缺少备份，D2SteamFix 也会主动提示进入修复模式。

确认弹窗会说明将关闭 Steam 与 Destiny 2 以便 Steam 修复文件。点击继续并同意可能出现的 Windows 权限提示后，D2SteamFix 会关闭相关进程、删除受影响的覆盖层 DLL，并启动 Steam 重新下载完整副本；若无法自动定位 Steam，请在弹出的文件选择框中选中 `steam.exe`。

Steam 退出且程序校验修复后的渲染器权限正常后，D2SteamFix 会自动清理游戏目录中的 `steamfix.dat` 及残留的 `.tmp` / `.old` 恢复文件，保留日志并退出。随后重新启动 Steam 即可。在确认前取消修复不会对任何文件或进程做出改动。

## 卸载

1. 正常退出 Destiny 2，让 D2SteamFix 恢复覆盖层权限；如果退出后游戏目录仍残留 `steamfix.dat`，请先完成恢复或执行上述修复流程。
2. 在 Steam 的 Destiny 2 **属性 → 启动选项** 中清空 D2SteamFix 命令。
3. 从游戏目录删除 `steamfix.exe`（以及不再需要的 `steamfix.log`）。

## 故障排查

启动失败时请查看 Destiny 2 目录下的 `steamfix.log`。错误对话框会显示错误代码，对应含义见 [docs/EXIT_CODES.md](docs/EXIT_CODES.md)。反馈问题时请附上错误信息与相关日志（提交前建议检查并隐藏个人路径）。

### Windows Defender

Windows Defender 可能把该二进制文件判为误报。未签名的自编译可执行文件容易触发启发式检测；本仓库的发布版由 GitHub Actions 从源码构建，与微软此前确认误报并移除检测的上游二进制并非同一个文件。

如果 Defender 报告检测结果，[请按这份说明清除缓存的检测记录并更新恶意软件定义](docs/WINDOWS_DEFENDER.md)。每个发布版的完整构建过程可在仓库的 Actions 页面查看。

## 开发与构建

构建前置条件与命令见 [docs/BUILDING.md](docs/BUILDING.md)，仓库开发约定见 [AGENTS.md](AGENTS.md)。
