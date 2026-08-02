param([string]$Action)

Add-Type -AssemblyName System.Windows.Forms

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'bin\Lyrics Tray Icon Fix.exe'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runName = 'Lyrics Tray Icon Fix'
$launcher = Join-Path $root 'run-hidden.vbs'

function Show-Popup {
    param([string]$Text, [string]$Title = 'Lyrics Tray Icon Fix')
    [System.Windows.Forms.MessageBox]::Show($Text, $Title, 'OK', 'Information') | Out-Null
}

function Start-Hidden {
    param([string]$FilePath, [string[]]$Arguments)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = ($Arguments -join ' ')
    $psi.WindowStyle = 'Hidden'
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()
    return $process.ExitCode
}

try {
    switch ($Action) {
        'start' {
            New-Item -Path $runKey -Force | Out-Null
            $wscript = Join-Path $env:WINDIR 'System32\wscript.exe'
            $value = '"' + $wscript + '" "' + $launcher + '"'
            Set-ItemProperty -Path $runKey -Name $runName -Value $value -Type String
            Start-Hidden $wscript @($launcher) | Out-Null
            Show-Popup '已设置开机自启并启动 Lyrics Tray Icon Fix。'
        }
        'stop' {
            Start-Hidden $exe @('stop') | Out-Null
            Show-Popup '已停止当前 Lyrics Tray Icon Fix 服务。'
        }
        'disable' {
            Start-Hidden $exe @('stop') | Out-Null
            Remove-ItemProperty -Path $runKey -Name $runName -Force -ErrorAction SilentlyContinue
            Show-Popup '已停止当前服务并取消开机自启。'
        }
        'status' {
            $tmp = Join-Path $env:TEMP ('ltif-status-' + [guid]::NewGuid().ToString('N') + '.txt')
            $process = Start-Process -FilePath $exe -ArgumentList 'status' -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $tmp
            $text = if (Test-Path -LiteralPath $tmp) { Get-Content -LiteralPath $tmp -Raw } else { '' }
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
            $auto = Get-ItemProperty -Path $runKey -Name $runName -ErrorAction SilentlyContinue
            $autoText = if ($auto) { '已开启' } else { '未开启' }
            $text += "`r`n`r`n开机自启状态：$autoText"
            Show-Popup $text
        }
        default {
            Show-Popup '未知操作。'
        }
    }
} catch {
    Show-Popup ('操作失败：' + $_.Exception.Message)
    exit 1
}
