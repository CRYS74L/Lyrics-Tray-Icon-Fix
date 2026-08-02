Option Explicit
Dim shell, fso, root, q, launcher, wscriptPath, runKey, value
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
q = Chr(34)

launcher = root & "\run-hidden.vbs"
wscriptPath = shell.ExpandEnvironmentStrings("%WINDIR%") & "\System32\wscript.exe"
runKey = "HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Lyrics Tray Icon Fix"
value = q & wscriptPath & q & " " & q & launcher & q

shell.RegWrite runKey, value, "REG_SZ"
shell.Run q & launcher & q, 0, True
ShowMessage "已设置开机自启并启动 Lyrics Tray Icon Fix。"

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
