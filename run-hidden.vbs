Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
exe = root & "\bin\Lyrics Tray Icon Fix v0.67.exe"
bin = root & "\bin"
marker = root & "\watchdog-stop.txt"
lock = root & "\watchdog.lock"

If fso.FileExists(marker) Then
  fso.DeleteFile marker, True
End If

On Error Resume Next
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.64.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.65.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix Hook v0.66.dll", True
fso.DeleteFile bin & "\Lyrics Tray Icon Fix PS Restore Hook v0.66.dll", True
On Error GoTo 0

On Error Resume Next
If fso.FileExists(lock) Then
  Set wmi = GetObject("winmgmts:\\.\root\cimv2")
  Set procs = wmi.ExecQuery("SELECT ProcessId FROM Win32_Process WHERE Name = 'wscript.exe' AND CommandLine LIKE '%run-hidden.vbs%'")
  If Err.Number = 0 Then
    count = 0
    For Each proc In procs
      count = count + 1
    Next
    If count <= 1 Then
      fso.DeleteFile lock, True
    End If
  End If
End If
If fso.FileExists(lock) Then
  WScript.Quit
End If
Err.Clear
Set lockStream = fso.CreateTextFile(lock, False)
If Err.Number <> 0 Then
  Err.Clear
  WScript.Quit
End If
On Error GoTo 0
lockStream.Close

Do
  If fso.FileExists(marker) Then
    StopWatchdog
  End If
  shell.Run """" & exe & """ start", 0, True
  If fso.FileExists(marker) Then
    StopWatchdog
  End If
  WScript.Sleep 2000
  If fso.FileExists(marker) Then
    StopWatchdog
  End If
Loop

Sub StopWatchdog
  On Error Resume Next
  fso.DeleteFile lock, True
  On Error GoTo 0
  WScript.Quit
End Sub
