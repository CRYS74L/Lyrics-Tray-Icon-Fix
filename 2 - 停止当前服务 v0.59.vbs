Option Explicit
Dim shell, fso, root, q, exe
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
q = Chr(34)

exe = root & "\bin\Lyrics Tray Icon Fix v0.59.exe"
shell.Run q & exe & q & " stop", 0, True
ShowMessage "已停止当前 Lyrics Tray Icon Fix 服务。"

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
