Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
exe = root & "\bin\Lyrics Tray Icon Fix v0.74.exe"
bin = root & "\bin"
marker = root & "\watchdog-stop.txt"

If fso.FileExists(marker) Then
  fso.DeleteFile marker, True
End If

On Error Resume Next
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.64.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.65.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.66.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.66.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.67.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.67.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.68.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.68.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.69.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.69.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.70.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.70.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.71.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.71.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.72.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.72.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.73.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.73.dll", True
On Error GoTo 0

Do
  If fso.FileExists(marker) Then
    Exit Do
  End If
  shell.Run """" & exe & """ start", 0, True
  If fso.FileExists(marker) Then
    Exit Do
  End If
  WScript.Sleep 2000
  If fso.FileExists(marker) Then
    Exit Do
  End If
Loop
