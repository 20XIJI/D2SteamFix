# D2SteamFix

[English](README.md)

D2SteamFix 是 Destiny 2 的启动包装程序，用于缓解 Steam 平台上的帧生成时间不稳与卡顿问题。它在启动 Destiny 2 之前临时移除 Steam 覆盖层渲染器 DLL 的执行权限，并在退出游戏后恢复原有权限。

需要注意的是，屏蔽 Steam 覆盖层 DLL 会同时影响依赖覆盖层的功能，包括 Steam Input、通知、游戏录制与远程同乐。本程序只临时修改覆盖层 DLL 的 ACL 元数据，不改动文件内容，不注入代码，不读取进程内存，不修改游戏文件，不更改 Steam 配置，也不绕过 BattlEye。

## 安装

下载 [steamfix.exe](https://pkg.d2checkpoint.com/D2SteamFix/steamfix.exe)，直接放入游戏所在目录。

然后在 Steam 中打开 Destiny 2 的启动选项：

1. 在 Steam 库中右键 **Destiny 2**，选择**属性**。
2. 在**通用**选项卡下找到**启动选项**输入框，粘贴下面的命令。

```text
"C:\路径\Destiny 2\steamfix.exe" %command%
```

把 `C:\路径\Destiny 2` 替换为 Destiny 2 的实际安装位置。下面的示例路径对应 Steam 的默认安装位置；游戏装在其他盘时，请使用对应的盘符与完整路径：

```text
"C:\Program Files (x86)\Steam\steamapps\common\Destiny 2\steamfix.exe" %command%
```

![Steam 启动选项](https://static.d2checkpoint.com/steamfix_launch_options.png)

## 确认是否生效

从 Steam 启动 Destiny 2，在游戏中按 **Shift+Tab**。覆盖层没有弹出，说明渲染器已被屏蔽，D2SteamFix 正在生效。退出游戏后覆盖层会自动恢复。

D2SteamFix 把 `steamfix.log` 写在游戏可执行文件旁边，也就是 `steamfix.exe` 所在的目录。反馈问题时请附上这个文件；错误对话框中还会显示错误代码，含义见 [docs/EXIT_CODES.md](docs/EXIT_CODES.md)。

## 覆盖层一直无法恢复时

渲染器权限在游戏退出时恢复。如果 Destiny 2、Steam 或计算机异常终止，恢复步骤就不会执行，覆盖层会持续失效，直到 D2SteamFix 再次获得运行机会。

双击一次 `steamfix.exe` 即可修复。在没有游戏可启动的情况下运行时，它会恢复已保存的权限、清除遗留状态，然后提示这个程序应当配置在 Steam 启动选项中——该提示属于正常现象，看到它时修复已经完成。请先关闭 Destiny 2：D2SteamFix 会等待所有 Destiny 进程退出后才处理权限。

## 卸载

先修复，再删除，顺序不能颠倒：

1. 按上一节的方式双击一次 `steamfix.exe`，确保没有遗留的权限状态。
2. 在 Steam 的 Destiny 2 属性中清空启动选项。
3. 从游戏目录删除 `steamfix.exe`、`steamfix.log` 与 `steamfix.log.old`。

在 `steamfix.dat` 仍然存在的情况下删除 `steamfix.exe`，会导致 Steam 覆盖层永久失效，而且现场不再留下任何能解释原因的线索。如果已经发生，把 `steamfix.exe` 放回游戏目录，双击一次，然后再删除这两个文件。

## 管理员权限

D2SteamFix 拒绝以提权令牌运行，因为那会让 Destiny 2 也以管理员身份启动。它自己从不请求提权——程序清单声明为 `asInvoker`——所以一旦检测到提权令牌，该令牌来自启动它的程序。

常见原因是 Steam 带有兼容性标记。请右键 `steam.exe`，选择**属性** → **兼容性**，取消勾选**以管理员身份运行此程序**（**更改所有用户的设置**中同样取消），然后重启 Steam。错误提示会指出令牌来自哪个进程。

完全关闭用户账户控制（`EnableLUA=0`）的系统，以及内置 Administrator 账户，属于另一种情况：该账户的每个进程本来就持有完整管理员令牌，没有可降的权限。这种情况下 D2SteamFix 会继续运行，并在 `steamfix.log` 中记录一条警告。

## 从源码构建

### 前置条件

构建 D2SteamFix 需要 Windows 与 Microsoft Visual C++ 工具链。

安装 Visual Studio 或 Visual Studio 生成工具，勾选"使用 C++ 的桌面开发"工作负载，并确保包含 MSVC 编译器与 Windows SDK。在 Visual Studio 开发人员命令提示符中执行以下命令：

```batch
make.cmd configure
```

```batch
make.cmd build
```

## Windows Defender

发布的二进制文件可能被 Windows Defender 误报。这是已知问题，微软已确认属于误报并移除了该检测。

如果 Windows Defender 仍然报告旧的检测结果，[请按这份说明清除缓存的检测记录并更新恶意软件定义](docs/WINDOWS_DEFENDER.md)。
