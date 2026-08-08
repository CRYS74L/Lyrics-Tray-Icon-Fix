Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
exe = root & "\bin\Lyrics Tray Icon Fix v0.112.exe"
bin = root & "\bin"
marker = root & "\watchdog-stop.txt"

oldFiles = Array( _
  "bin\Lyrics Tray Icon Fix Hook v0.105.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Hook v0.105.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Helper v0.105.exe", _
  "bin\Lyrics Tray Icon Fix v0.105.exe", _
  "bin\Lyrics Tray Icon Fix Copy v0.105.exe", _
  "bin\Lyrics Tray Icon Fix Hook v0.106.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Hook v0.106.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Helper v0.106.exe", _
  "bin\Lyrics Tray Icon Fix v0.106.exe", _
  "bin\Lyrics Tray Icon Fix Copy v0.106.exe", _
  "bin\Lyrics Tray Icon Fix Hook v0.107.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Hook v0.107.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Helper v0.107.exe", _
  "bin\Lyrics Tray Icon Fix v0.107.exe", _
  "bin\Lyrics Tray Icon Fix Copy v0.107.exe", _
  "bin\Lyrics Tray Icon Fix Hook v0.108.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Hook v0.108.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Helper v0.108.exe", _
  "bin\Lyrics Tray Icon Fix v0.108.exe", _
  "bin\Lyrics Tray Icon Fix Copy v0.108.exe", _
  "bin\Lyrics Tray Icon Fix Hook v0.109.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Hook v0.109.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Helper v0.109.exe", _
  "bin\Lyrics Tray Icon Fix v0.109.exe", _
  "bin\Lyrics Tray Icon Fix Copy v0.109.exe", _
  "bin\Lyrics Tray Icon Fix Hook v0.110.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Hook v0.110.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Helper v0.110.exe", _
  "bin\Lyrics Tray Icon Fix v0.110.exe", _
  "bin\Lyrics Tray Icon Fix Copy v0.110.exe", _
  "bin\Lyrics Tray Icon Fix Hook v0.111.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Hook v0.111.dll", _
  "bin\Lyrics Tray Icon Fix PS Restore Helper v0.111.exe", _
  "bin\Lyrics Tray Icon Fix v0.111.exe", _
  "bin\Lyrics Tray Icon Fix Copy v0.111.exe")
For Each oldFile In oldFiles
  If fso.FileExists(root & "\" & oldFile) Then
    On Error Resume Next
    fso.DeleteFile root & "\" & oldFile, True
    On Error GoTo 0
  End If
Next

If fso.FileExists(marker) Then
  fso.DeleteFile marker, True
End If

Do
  If fso.FileExists(marker) Then
    Exit Do
  End If
  shell.Run """" & exe & """ start", 0, True
  If fso.FileExists(marker) Then
    Exit Do
  End If
Loop
