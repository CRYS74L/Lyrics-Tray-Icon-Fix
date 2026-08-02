Option Explicit
Dim shell, fso, root, q, exe, runKey, statusFile, stream, statusText, autoValue, autoText
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
q = Chr(34)

exe = root & "\bin\Lyrics Tray Icon Fix v0.59.exe"
runKey = "HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Lyrics Tray Icon Fix"
statusFile = root & "\status-raw.txt"
shell.Run q & root & "\capture-status-v0.59.cmd" & q, 0, True
Set stream = CreateObject("ADODB.Stream")
stream.Type = 2
stream.Charset = "utf-8"
stream.Open
stream.LoadFromFile statusFile
statusText = stream.ReadText(-1)
stream.Close

statusText = Replace(statusText, "Version: v0.59", "版本：v0.59")
statusText = Replace(statusText, "Notify icon rules:", "托盘图标规则：")
statusText = Replace(statusText, "Window hide rules:", "窗口隐藏规则：")
statusText = Replace(statusText, "GUID icon rules:", "GUID 图标规则：")
statusText = Replace(statusText, "Shell notify block rules:", "Shell 创建拦截规则：")
statusText = Replace(statusText, "exe=", "")
statusText = Replace(statusText, "class=", "：")
statusText = Replace(statusText, "text=any", "")
statusText = Replace(statusText, "uid=", "UID=")
statusText = Replace(statusText, "LYRICIFY LITE.EXE", "Lyricify Lite.exe")
statusText = Replace(statusText, "BETTERLYRICS.WINUI3.EXE", "BetterLyrics.WinUI3.exe")
statusText = Replace(statusText, "EXPLORER.EXE", "explorer.exe")
statusText = Replace(statusText, "GOOGLEDRIVEFS.EXE", "GoogleDriveFS.exe")
statusText = Replace(statusText, "H.NotifyIcon_", "H.NotifyIcon_*")
statusText = Replace(statusText, "ATL:", "ATL:*")
statusText = Replace(statusText, "Background: running", "后台服务：运行中")
statusText = Replace(statusText, "Background: stopped", "后台服务：未运行")
statusText = Replace(statusText, "Explorer restart watcher: ready", "Explorer 重建监听：已就绪")
statusText = Replace(statusText, "Explorer restart watcher: not_ready", "Explorer 重建监听：未就绪")
statusText = Replace(statusText, "Create-stage hook:", "创建阶段 Hook：")
statusText = Replace(statusText, "PS Tray Factory restore guard:", "PS Tray Factory 防恢复：")
statusText = Replace(statusText, "thread_hook=", "线程 Hook=")
statusText = Replace(statusText, "iat=", "IAT=")
statusText = Replace(statusText, "not_ready", "未就绪")
statusText = Replace(statusText, "ready", "已就绪")
statusText = Replace(statusText, "Current matched notify icons:", "当前匹配托盘图标：")
statusText = Replace(statusText, "Current hidden windows:", "当前隐藏窗口：")
statusText = Replace(statusText, "Current hidden GUID icons:", "当前隐藏 GUID 图标：")

On Error Resume Next
autoValue = shell.RegRead(runKey)
If Err.Number = 0 Then
  autoText = "已开启"
Else
  autoText = "未开启"
End If
On Error GoTo 0
statusText = statusText & vbCrLf & vbCrLf & "开机自启状态：" & autoText
ShowMessage statusText

Sub ShowMessage(text)
  Dim msgFile, stream, copyExe
  msgFile = root & "\popup-message.txt"
  Set stream = CreateObject("ADODB.Stream")
  stream.Type = 2
  stream.Charset = "utf-8"
  stream.Open
  stream.WriteText text
  stream.SaveToFile msgFile, 2
  stream.Close
  copyExe = root & "\bin\Lyrics Tray Icon Fix Copy v0.59.exe"
  shell.Run q & copyExe & q & " " & q & msgFile & q, 0, True
  MsgBox "【内容已自动复制到剪贴板】" & vbCrLf & vbCrLf & text, vbInformation, "Lyrics Tray Icon Fix"
End Sub
