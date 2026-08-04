$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspace = Split-Path -Parent $root
$zig = Join-Path $workspace 'tools\zig\zig.exe'
if (-not (Test-Path -LiteralPath $zig)) {
  $zigCommand = Get-Command zig.exe -ErrorAction SilentlyContinue
  if (-not $zigCommand) {
    throw 'zig.exe not found. Install Zig and add it to PATH, or put it at ..\tools\zig\zig.exe.'
  }
  $zig = $zigCommand.Source
}
$bin = Join-Path $root 'bin'
$zigCache = Join-Path $root '.zig-cache'
$zigGlobalCache = Join-Path $root '.zig-global-cache'

New-Item -ItemType Directory -Force -Path $bin | Out-Null
New-Item -ItemType Directory -Force -Path $zigCache | Out-Null
New-Item -ItemType Directory -Force -Path $zigGlobalCache | Out-Null
$env:ZIG_LOCAL_CACHE_DIR = $zigCache
$env:ZIG_GLOBAL_CACHE_DIR = $zigGlobalCache

& $zig cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE `
  -I (Join-Path $root 'src') `
  (Join-Path $root 'tests\rules_test.c') `
  (Join-Path $root 'src\rules.c') `
  -o (Join-Path $bin 'rules_test.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $bin 'rules_test.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -O2 -shared `
  -I (Join-Path $root 'src') `
  (Join-Path $root 'src\hook_dll.c') `
  (Join-Path $root 'src\rules.c') `
  -o (Join-Path $bin 'Lyrics Tray Icon Fix Hook v0.74.dll') `
  -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -target x86-windows-gnu -O2 -shared `
  (Join-Path $root 'src\pstf_hook_dll.c') `
  -o (Join-Path $bin 'Lyrics Tray Icon Fix PS Restore Hook v0.74.dll') `
  -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -target x86-windows-gnu -DUNICODE -D_UNICODE -O2 -municode `
  (Join-Path $root 'src\pstf_helper.c') `
  -o (Join-Path $bin 'Lyrics Tray Icon Fix PS Restore Helper v0.74.exe') `
  -luser32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -O2 -municode `
  -I (Join-Path $root 'src') `
  (Join-Path $root 'src\controller.c') `
  (Join-Path $root 'src\rules.c') `
  -o (Join-Path $bin 'Lyrics Tray Icon Fix v0.74.exe') `
  -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -target x86_64-windows-gnu -O2 -municode `
  (Join-Path $root 'src\copy_text.c') `
  -o (Join-Path $bin 'Lyrics Tray Icon Fix Copy v0.74.exe') `
  -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'build: ok'
