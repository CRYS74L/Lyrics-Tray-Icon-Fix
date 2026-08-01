Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
exe = root & "\bin\Lyrics Tray Icon Fix.exe"
bin = root & "\bin"

If fso.FolderExists(bin) Then
  For Each file In fso.GetFolder(bin).Files
    If LCase(Right(file.Name, 15)) = ".delete-pending" Then
      On Error Resume Next
      fso.DeleteFile file.Path, True
      On Error GoTo 0
    End If
  Next
End If

shell.Run """" & exe & """ start", 0, False
