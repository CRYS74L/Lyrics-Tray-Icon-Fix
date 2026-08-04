# Lyrics Tray Icon Fix

这是一个为个人用途编写的 Windows 托盘图标修复工具，用来隐藏 BetterLyrics、Lyricify Lite、Windows 音频、Windows 麦克风隐私指示器以及 Google Drive 相关托盘中错误或不需要显示的特定图标。

当前版本：`v0.75`。

v0.75 在 v0.66 文件状态基础上增加一项事件触发的清理：服务启动和 Explorer 重建后，限次删除 GoogleDrive `UID=11376` 重复图标，避免再次出现两个 Google Drive 托盘图标。脚本名、二进制名、README 和 Release 均统一为 v0.75。

v0.75 在 v0.65 基础上补上 `CTRL_CLOSE_EVENT` 处理。系统重启/关机时隐藏控制台会收到该信号，v0.75 会同步写入 `watchdog-stop.txt`，避免 watchdog 在进程退出后再次拉起并触发 `0xc0000142`。同时启动脚本会检测是否已有 watchdog 在运行，避免重复启动第二个 watchdog；watchdog 在进程退出后也会等待 2 秒再决定是否重启，进一步避开关机窗口。

v0.75 在 v0.64 的内联拦截基础上，修复关机或注销阶段 Watchdog 和 Explorer 自恢复再次拉起进程导致 `0xc0000142` 弹窗的问题。检测到关机/注销时会写入 `watchdog-stop.txt`，并停止 Explorer 冷启动恢复，避免系统关闭过程中重新创建本工具进程。

v0.64 在 Explorer 中直接内联拦截 `shell32!Shell_NotifyIconW/A` 函数入口，不再依赖模块导入表或延迟导入，确保麦克风/音频图标无论由哪个模块创建都能被拦截，同时避免 v0.62/v0.63 的导入表崩溃风险。

## 用途

本项目不是通用托盘管理器，也不是 PS Tray Factory 的替代品。它只是补足我当前环境里 PS Tray Factory 对部分托盘图标隐藏不稳定的问题。

当前只匹配以下窄规则：

```text
LYRICIFY LITE.EXE + H.NotifyIcon_* + UID=0
BETTERLYRICS.WINUI3.EXE + H.NotifyIcon_* + UID=0
EXPLORER.EXE + ATL:* + UID=100
EXPLORER.EXE + ATL:* + UID=101
GOOGLEDRIVEFS.EXE + ATL:* + UID=11376
GOOGLEDRIVEFS.EXE + DriveDot
```

其中 `EXPLORER.EXE + ATL:* + UID=100` 用于处理已观察到的 Windows 音频托盘图标，例如“音频服务未运行。”和“音箱 (USB): 26%”。

`EXPLORER.EXE + ATL:* + UID=100/101` 分别处理已观察到的 Windows 音频图标和麦克风隐私指示器，例如“音频服务未运行。”与“微信 正在使用你的麦克风”。两者使用相同的创建阶段拦截和当前图标清理路径，不再向 PS Tray Factory 的隐藏列表写入音频规则。所有匹配同时限定所有者进程、窗口类前缀和 UID，避免影响 `explorer.exe` 的其他托盘图标。

Lyricify Lite 和 BetterLyrics 的 `H.NotifyIcon` 窗口会注册两个图标：一个是正常 GUID 图标，另一个是同一 `HWND/UID=0` 的非 GUID 重复图标。v0.75 不在这两个进程里修改 `H.NotifyIcon.dll` 导入表，而是在 `H.NotifyIcon` 消息窗口出现后启动一次最多 10 秒的启动期清理线程，每 200ms 检查一次该非 GUID 图标，发现即删除，成功后线程退出；正常 GUID 图标不会被删除。该清理只覆盖启动窗口创建阶段，之后保持静止，不持续轮询。

v0.75 不再为 Lyricify/BetterLyrics 向 PS Tray Factory 的 `AutoHideFiles` 写入随机 `H.NotifyIcon_*` 类名规则，避免旧记录堆积和 PS Tray Factory 托盘列表被反复刷新。

Google Drive 当前有两个不同的通知图标。v0.75 隐藏 `GOOGLEDRIVEFS.EXE + ATL:* + UID=11376`、除主 GUID 外的其他 GUID 图标，以及 `GOOGLEDRIVEFS.EXE + DriveDot` 中不需要的图标窗口，并保留 `IconGuid={6BBAE539-2232-434A-A4E5-9A33560C6283}` 的主 Google Drive 图标。对于无法通过线程 Hook 加载的第二个 `GoogleDriveFS.exe` 进程，v0.75 会仅对该 Google Drive 进程执行 DLL 加载兜底，并在该进程内监听 `DriveDot` 窗口创建后立即隐藏。Explorer 不再执行进程内 DLL 注入，并同时拦截宽字符和 ANSI 版本的 `Shell_NotifyIcon`；PS Tray Factory 恢复 Google Drive 多余图标时也会被拦截。

## 使用

普通使用请从 GitHub Releases 下载完整包，解压到固定目录后双击：

```text
1 - 启动并设置开机自启 v0.75.vbs
```

停止当前服务：

```text
2 - 停止当前服务 v0.75.vbs
```

停止并取消开机自启：

```text
3 - 停止并取消开机自启 v0.75.vbs
```

检测状态：

```text
4 - 检测状态与自启状态 v0.75.vbs
```

开机启动项只使用当前用户的：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
```

启动链路：

```text
HKCU Run -> wscript.exe -> run-hidden.vbs -> bin\Lyrics Tray Icon Fix v0.75.exe start
```

## 实现方式

v0.75 仍使用 Windows 消息 Hook，但只在目标进程的 GUI 线程上安装：

```text
WH_CALLWNDPROC
WH_GETMESSAGE
```

它不是持续轮询托盘，也不主动广播 PS Tray Factory 的刷新消息。后台进程主要等待事件，Hook DLL 只对命中的目标进程和目标图标规则执行处理。Explorer 和 Google Drive 的 Hook 就绪后会立即返回，不再逐条检查普通窗口消息，因此不会拖慢桌面框选或右键菜单。

对 Google Drive 以及 Explorer 的 UID 100/101，v0.75 使用创建阶段拦截。Explorer 的图标由 `stobject.dll` 的延迟导入提交，因此工具按 PE 延迟导入名称精确修改该模块的 `Shell_NotifyIconW` 和 `Shell_NotifyIconA` 槽；UID 100/101 即使带 `NIF_GUID` 也会被拦截。Explorer 只安装一个用于注入 DLL 的线程 Hook，不再对每个 Explorer 线程逐消息检查。Google Drive 只保留主 GUID，其余 GUID 图标和 `DriveDot` 窗口都会被隐藏，避免重启或麦克风调用后重新出现多余图标。若目标槽已由其他组件接管，本工具会保存现有函数作为下一跳，未命中隐藏规则的调用仍交回原有函数，以保留无关图标行为。

v0.75 直接把当前会话的 `explorer.exe` 进程句柄加入后台无限事件等待。Explorer 退出时，控制器保持目标进程 Hook 持续运行，只在最长 60 秒的 Shell 重建窗口内通过进程快照取得新的 Explorer PID 并替换等待句柄，不依赖 Win11 重建后可能不存在的 `Shell_TrayWnd`。只有 60 秒内始终找不到新 Explorer 时，才退回完整冷启动恢复路径。平时没有轮询，短时检测只会由 Explorer 进程退出事件触发。`status` 中的 `Explorer restart watcher` 可确认当前 Explorer 进程句柄等待已经建立。

PS Tray Factory 3.0.3.198 会从缓存的 `NOTIFYICONDATA` 调用 32 位 `Shell_NotifyIconA`，重新恢复已经被删除的 Explorer 音频图标。v0.75 由独立的 32 位辅助进程只对属于 `PSTrayFactory.exe` 且具有消息队列的线程安装线程级 `WH_CALLWNDPROC` Hook，并只修改该进程主模块的 `Shell_NotifyIconA` 导入槽。它拦截所有者为 `explorer.exe`、窗口类为 `ATL:*`、UID 为 100 或 101 的恢复，以及 Google Drive 多余图标的恢复；其他 PS Tray Factory 调用全部原样转发。辅助器在安装线程 Hook 后只向 PS Tray Factory 自己的窗口投递一次 `WM_NULL`，使 IAT 防恢复 Hook 每次启动都立即就绪。PS Tray Factory 重启时由进程退出事件触发重新安装，不使用定时轮询，也不监听全部窗口事件。

`status` 中的 `Create-stage hook` 和 `PS Tray Factory restore guard` 只有在对应目标模块的导入槽实际改写成功后才显示 `ready`，可用于区分“后台进程存在”和“创建阶段拦截已生效”。

服务停止或 Hook DLL 正常卸载时，会把修改过的导入槽恢复为安装前保存的下一跳，避免后续升级堆叠旧 Hook。v0.75 每个目标模块只改写一个实际命中的 `Shell_NotifyIconW` 或 `Shell_NotifyIconA` 导入槽，并用原子安装状态阻止并发重复改写。若 Explorer 或 Google Drive 冷启动时目标模块尚未加载，Hook DLL会在后续真实窗口消息到来时重新检查；从未就绪变为就绪的瞬间重新执行创建阶段拦截，不使用定时器。

## 构建

源码构建需要 Zig C 编译器。构建脚本会优先查找仓库上一层目录的：

```text
tools\zig\zig.exe
```

如果不存在，则使用 `PATH` 里的 `zig.exe`。

然后运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

构建产物会输出到：

```text
bin\
```

## 安全说明

本工具不读取游戏内存，不扫描游戏进程内存，不注入游戏专用逻辑。它只对命中规则的少数进程安装线程级 Hook，不安装全局 Windows 消息 Hook。

如果特别在意游戏反作弊风险，请在启动相关游戏前自行评估是否停用本工具。

## 许可

MIT License。
