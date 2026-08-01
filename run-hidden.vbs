Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
exe = root & "\bin\Lyrics Tray Icon Fix.exe"
shell.Run """" & exe & """ start", 0, False