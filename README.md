# Lyrics Tray Icon Fix

这是一个为个人用途编写的 Windows 托盘图标修复工具，用来隐藏 BetterLyrics、Lyricify Lite、Windows 音频以及 Google Drive 相关托盘中错误或不需要显示的特定图标。

当前版本：`v0.23-target-google-drive-livedot`。

## 用途

本项目不是通用托盘管理器，也不是 PS Tray Factory 的替代品。它只是补足我当前环境里 PS Tray Factory 对部分托盘图标隐藏不稳定的问题。

当前只匹配以下窄规则：

```text
LYRICIFY LITE.EXE + H.NotifyIcon_* + UID=0
BETTERLYRICS.WINUI3.EXE + H.NotifyIcon_* + UID=0
EXPLORER.EXE + ATL:* + UID=100
GOOGLEDRIVEFS.EXE + DriveDot + 窗口标题=Google 云端硬盘实时编辑状态
```

其中 `EXPLORER.EXE + ATL:* + UID=100` 用于处理已观察到的 Windows 音频托盘图标，例如“音频服务未运行。”和“音箱 (USB): 26%”。规则同时限定进程、窗口类前缀和 UID，避免影响 explorer.exe 的其他托盘图标。

`GOOGLEDRIVEFS.EXE + DriveDot + 窗口标题=Google 云端硬盘实时编辑状态` 用于隐藏 Google Drive 额外出现、且不能被 PS Tray Factory 按传统托盘图标识别的实时编辑状态窗口。它不会删除截图中 PS Tray Factory 可识别的 `ATL:* + UID=11376` 正常 Google Drive 托盘图标。

## 使用

普通使用请从 GitHub Releases 下载完整包，解压到固定目录后运行：

```text
Install and start v0.23.cmd
```

查看状态：

```text
Status v0.23.cmd
```

停止并移除开机启动：

```text
Stop and disable v0.23.cmd
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

v0.23 仍使用 Windows 消息 Hook：

```text
WH_CALLWNDPROC
WH_GETMESSAGE
```

它不是持续轮询托盘，也不主动广播 PS Tray Factory 的刷新消息。后台进程主要等待事件，Hook DLL 只对命中的目标进程和目标窗口规则执行处理。

## 构建

源码构建需要 Zig C 编译器，放置在仓库上一级目录：

```text
tools\zig\zig.exe
```

然后运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

构建产物会输出到：

```text
bin\
```

## 安全说明

本工具不读取游戏内存，不扫描进程内存，不注入游戏专用逻辑。它会安装全局 Windows 消息 Hook，因此 Hook DLL 会被加载到多个 GUI 进程中，这是该技术路线的正常表现。

如果特别在意游戏反作弊风险，请在启动相关游戏前自行评估是否停用本工具。

## 许可

MIT License。
