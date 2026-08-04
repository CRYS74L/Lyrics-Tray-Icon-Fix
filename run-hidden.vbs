Option Explicit
Dim shell, fso, root, exe, bin, marker, lockFile
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
exe = root & "\bin\Lyrics Tray Icon Fix v0.68.exe"
bin = root & "\bin"
marker = root & "\watchdog-stop.txt"
lockFile = root & "\watchdog.lock"

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
On Error GoTo 0

If Not AcquireWatchdogLock(lockFile) Then
  WScript.Quit
End If

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

ReleaseWatchdogLock lockFile
WScript.Quit

Function AcquireWatchdogLock(lockFile)
  Dim stream, watchdogCount
  On Error Resume Next
  If fso.FileExists(lockFile) Then
    watchdogCount = CountWatchdogProcesses()
    If watchdogCount > 1 Then
      Err.Clear
      On Error GoTo 0
      AcquireWatchdogLock = False
      Exit Function
    End If
    If watchdogCount > 0 And Not IsLockStale(lockFile) Then
      Err.Clear
      On Error GoTo 0
      AcquireWatchdogLock = False
      Exit Function
    End If
    Err.Clear
    fso.DeleteFile lockFile, True
    If Err.Number <> 0 Then
      Err.Clear
      On Error GoTo 0
      AcquireWatchdogLock = False
      Exit Function
    End If
  End If
  Set stream = fso.CreateTextFile(lockFile, False)
  If Err.Number = 0 Then
    stream.WriteLine Now
    stream.Close
    Err.Clear
    On Error GoTo 0
    AcquireWatchdogLock = True
    Exit Function
  End If
  Err.Clear
  On Error GoTo 0
  AcquireWatchdogLock = False
End Function

Function CountWatchdogProcesses()
  Dim wmi, procs, proc
  CountWatchdogProcesses = 0
  On Error Resume Next
  Set wmi = GetObject("winmgmts:\\.\root\cimv2")
  Set procs = wmi.ExecQuery("SELECT ProcessId FROM Win32_Process WHERE Name = 'wscript.exe' AND CommandLine LIKE '%run-hidden.vbs%'")
  For Each proc In procs
    CountWatchdogProcesses = CountWatchdogProcesses + 1
  Next
  Err.Clear
  On Error GoTo 0
End Function

Function IsLockStale(lockFile)
  Dim stream, stamp
  IsLockStale = True
  On Error Resume Next
  Set stream = fso.OpenTextFile(lockFile, 1)
  If Err.Number = 0 Then
    stamp = stream.ReadLine
    stream.Close
    If IsDate(stamp) Then
      If DateDiff("s", stamp, Now) < 5 Then
        IsLockStale = False
      End If
    End If
  End If
  Err.Clear
  On Error GoTo 0
End Function

Sub ReleaseWatchdogLock(lockFile)
  On Error Resume Next
  If fso.FileExists(lockFile) Then
    fso.DeleteFile lockFile, True
  End If
  On Error GoTo 0
End Sub
