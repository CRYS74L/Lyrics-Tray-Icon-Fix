Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
exe = root & "\bin\Lyrics Tray Icon Fix v0.60.exe"
bin = root & "\bin"
marker = root & "\watchdog-stop.txt"

If fso.FileExists(marker) Then
  fso.DeleteFile marker, True
End If

Do
  shell.Run """" & exe & """ start", 0, True
  If fso.FileExists(marker) Then
    Exit Do
  End If
  WScript.Sleep 500
Loop
