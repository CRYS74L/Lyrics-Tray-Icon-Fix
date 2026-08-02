param([string]$Action, [switch]$NoMessage)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'bin\Lyrics Tray Icon Fix.exe'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runName = 'Lyrics Tray Icon Fix'
$launcher = Join-Path $root 'run-hidden.vbs'

function Show-Popup {
    param([string]$Text, [string]$Title = 'Lyrics Tray Icon Fix')
    if ($NoMessage) {
        Write-Output $Text
        return
    }

    $form = New-Object System.Windows.Forms.Form
    $form.Text = $Title
    $form.Width = 760
    $form.Height = 540
    $form.StartPosition = 'CenterScreen'
    $form.Font = New-Object System.Drawing.Font('Microsoft YaHei UI', 9)

    $box = New-Object System.Windows.Forms.TextBox
    $box.Multiline = $true
    $box.ReadOnly = $true
    $box.ScrollBars = 'Both'
    $box.WordWrap = $true
    $box.Dock = 'Fill'
    $box.Text = $Text

    $button = New-Object System.Windows.Forms.Button
    $button.Text = '确定'
    $button.DialogResult = 'OK'
    $button.Dock = 'Bottom'
    $button.Height = 34

    $form.Controls.Add($box)
    $form.Controls.Add($button)
    $form.AcceptButton = $button
    $form.ShowDialog() | Out-Null
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

function Translate-ExeName {
    param([string]$Name)
    switch ($Name.ToUpperInvariant()) {
        'LYRICIFY LITE.EXE' { return 'Lyricify Lite.exe' }
        'BETTERLYRICS.WINUI3.EXE' { return 'BetterLyrics.WinUI3.exe' }
        'EXPLORER.EXE' { return 'explorer.exe' }
        'GOOGLEDRIVEFS.EXE' { return 'GoogleDriveFS.exe' }
        default { return $Name }
    }
}

function Get-ChineseStatus {
    param([string]$RawStatus)

    $out = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($RawStatus -split "`r?`n")) {
        if ($line -match '^Version:\s*(.+)$') {
            $out.Add('版本：' + $matches[1])
        } elseif ($line -match '^Notify icon rules:$') {
            $out.Add('托盘图标规则：')
        } elseif ($line -match '^\s*(\d+)\.\s*exe=(.+?)\s+class=(\S+)\s+uid=(\d+)$') {
            $out.Add(('  {0}. {1}：{2}，UID={3}' -f $matches[1], (Translate-ExeName $matches[2]), $matches[3], $matches[4]))
        } elseif ($line -match '^Window hide rules:$') {
            $out.Add('窗口隐藏规则：')
        } elseif ($line -match '^\s*(\d+)\.\s*exe=(\S+)\s*class=(\S+)\s*text=(\S+)$') {
            $out.Add(('  {0}. {1}：{2}' -f $matches[1], (Translate-ExeName $matches[2]), $matches[3]))
        } elseif ($line -match '^GUID icon rules:$') {
            $out.Add('GUID 图标规则：')
        } elseif ($line -match '^Shell notify block rules:$') {
            $out.Add('Shell 创建拦截规则：')
        } elseif ($line -match '^\s*(\d+)\.\s*exe=(.+?)\s+class=(\S+)\s*uid=(\d+)$') {
            $out.Add(('  {0}. {1}：{2}，UID={3}' -f $matches[1], (Translate-ExeName $matches[2]), $matches[3], $matches[4]))
        } elseif ($line -match '^Background:\s*(.+)$') {
            $state = if ($matches[1] -match 'running') { '运行中' } else { '未运行' }
            $out.Add('后台服务：' + $state)
        } elseif ($line -match '^Explorer restart watcher:\s*(.+)$') {
            $state = if ($matches[1] -match 'ready') { '已就绪' } else { '未就绪' }
            $out.Add('Explorer 重建监听：' + $state)
        } elseif ($line -match '^Create-stage hook:\s*(.+)$') {
            $value = $matches[1] -replace 'explorer=', 'explorer=' -replace 'google_drive=', 'google_drive='
            $value = $value -replace 'ready', '已就绪' -replace 'not_ready', '未就绪'
            $out.Add('创建阶段 Hook：' + $value)
        } elseif ($line -match '^PS Tray Factory restore guard:\s*(.+)$') {
            $value = $matches[1] -replace 'thread_hook=', '线程 Hook=' -replace 'iat=', 'IAT='
            $value = $value -replace 'not_ready', '未就绪' -replace 'ready', '已就绪'
            $out.Add('PS Tray Factory 防恢复：' + $value)
        } elseif ($line -match '^Current matched notify icons:\s*(\d+)$') {
            $out.Add('当前匹配托盘图标：' + $matches[1])
        } elseif ($line -match '^Current hidden windows:\s*(\d+)$') {
            $out.Add('当前隐藏窗口：' + $matches[1])
        } elseif ($line -match '^Current hidden GUID icons:\s*(\d+)$') {
            $out.Add('当前隐藏 GUID 图标：' + $matches[1])
        } elseif ($line.Trim().Length -gt 0) {
            $out.Add($line)
        }
    }
    return ($out -join "`r`n")
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
            $raw = if (Test-Path -LiteralPath $tmp) { Get-Content -LiteralPath $tmp -Raw } else { '' }
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
            $auto = Get-ItemProperty -Path $runKey -Name $runName -ErrorAction SilentlyContinue
            $autoText = if ($auto) { '已开启' } else { '未开启' }
            $statusText = Get-ChineseStatus $raw
            $statusText += "`r`n`r`n开机自启状态：$autoText"
            Show-Popup $statusText
        }
        default {
            Show-Popup '未知操作。'
        }
    }
} catch {
    Show-Popup ('操作失败：' + $_.Exception.Message)
    exit 1
}
