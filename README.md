# Lyrics Tray Icon Fix

这是一个为个人用途编写的 Windows 托盘图标修复工具，用来隐藏 BetterLyrics、Lyricify Lite、Windows 音频、Windows 麦克风隐私指示器以及 Google Drive 相关托盘中错误或不需要显示的特定图标。

当前版本：`v0.32-stobject-shell-notify-hook`。

## 用途

本项目不是通用托盘管理器，也不是 PS Tray Factory 的替代品。它只是补足我当前环境里 PS Tray Factory 对部分托盘图标隐藏不稳定的问题。

当前只匹配以下窄规则：

```text
LYRICIFY LITE.EXE + H.NotifyIcon_* + UID=0
BETTERLYRICS.WINUI3.EXE + H.NotifyIcon_* + UID=0
EXPLORER.EXE + ATL:* + UID=100
EXPLORER.EXE + ATL:* + UID=101
GOOGLEDRIVEFS.EXE + ATL:* + UID=11376
```

其中 `EXPLORER.EXE + ATL:* + UID=100` 用于处理已观察到的 Windows 音频托盘图标，例如“音频服务未运行。”和“音箱 (USB): 26%”。

`EXPLORER.EXE + ATL:* + UID=101` 用于处理已观察到的 Windows 麦克风隐私指示器，例如“微信 正在使用你的麦克风”。v0.32 会在负责系统托盘对象的 `stobject.dll` 创建或修改这个图标时拦截它，并在服务启动时对已存在的图标做一次性清理。规则同时限定进程、窗口类前缀和 UID，避免影响 explorer.exe 的其他托盘图标。

Google Drive 当前有两个不同的通知图标。v0.32 沿用 v0.28 的处理方式：隐藏 `GOOGLEDRIVEFS.EXE + ATL:* + UID=11376` 这个图标，并保留 `IconGuid={6BBAE539-2232-434A-A4E5-9A33560C6283}` 的 Google Drive 图标。

## 使用

普通使用请从 GitHub Releases 下载完整包，解压到固定目录后运行：

```text
Install and start v0.32.cmd
```

查看状态：

```text
Status v0.32.cmd
```

停止并移除开机启动：

```text
Stop and disable v0.32.cmd
```

开机启动项使用当前用户的：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
```

启动链路：

```text
HKCU Run -> wscript.exe -> run-hidden.vbs -> bin\Lyrics Tray Icon Fix.exe start
```

## 实现方式

v0.32 仍使用 Windows 消息 Hook：

```text
WH_CALLWNDPROC
WH_GETMESSAGE
```

它不是持续轮询托盘，也不主动广播 PS Tray Factory 的刷新消息。后台进程主要等待事件，Hook DLL 只对命中的目标进程和目标图标规则执行处理。

对 Google Drive 和麦克风隐私指示器，v0.32 使用创建阶段拦截，并在服务启动时只做一次当前图标清理，避免在目标应用或 Explorer 的消息流里反复删除图标。Explorer 的麦克风图标由 `stobject.dll` 的延迟导入提交，因此 v0.32 按 PE 延迟导入名称精确修改该模块的 `Shell_NotifyIconW` 槽；Google Drive 仍修改自身主模块的普通导入槽。若目标槽已由 PS Tray Factory 接管，本工具会保存现有函数作为下一跳，未命中隐藏规则的调用仍交回 PS Tray Factory，以保留其自定义图标等功能。控制进程保持消息循环，确保全局 Hook 实际派发到目标进程。

`status` 中的 `Create-stage hook` 只有在目标模块的导入槽实际改写成功后才显示 `ready`，可用于区分“后台进程存在”和“创建阶段拦截已生效”。

服务停止或 Hook DLL 正常卸载时，会把修改过的导入槽恢复为安装前保存的下一跳，避免后续升级堆叠旧 Hook。

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

本工具不读取游戏内存，不扫描游戏进程内存，不注入游戏专用逻辑。它会安装全局 Windows 消息 Hook，因此 Hook DLL 会被加载到多个 GUI 进程中，这是该技术路线的正常表现。

如果特别在意游戏反作弊风险，请在启动相关游戏前自行评估是否停用本工具。

## 许可

MIT License。
