# build-windows-exe.ps1 - package the Clawdmeter tray app as a single .exe
#
# Produces dist\Clawdmeter.exe: a windowless, self-contained tray app (bundles
# Python + bleak + pystray + Pillow + the brand logo). No Python install is
# needed to run it. Double-click to start; right-click the tray icon -> Quit.
#
# Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File build-windows-exe.ps1
#   .\build-windows-exe.ps1 -Console     # console build, shows daemon log output
#
# PyInstaller is a build-only dependency: it is installed into .venv, not added
# to daemon\requirements-windows.txt.

param([switch]$Console)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Log {
    param([string]$Msg)
    $ts = Get-Date -Format "HH:mm:ss"
    Write-Host "[$ts] $Msg"
}

$RepoRoot = $PSScriptRoot
if (-not $RepoRoot) {
    $RepoRoot = (Get-Location).Path
}
Set-Location $RepoRoot

$VenvPython = Join-Path $RepoRoot ".venv\Scripts\python.exe"
if (-not (Test-Path $VenvPython)) {
    Log "Creating virtual environment at .venv..."
    python -m venv .venv
    if ($LASTEXITCODE -ne 0) { throw "python -m venv failed" }
}

Log "Installing dependencies + PyInstaller into .venv..."
& $VenvPython -m pip install --quiet -r daemon\requirements-windows.txt pyinstaller
if ($LASTEXITCODE -ne 0) { throw "pip install failed" }

# Render the tray logo to a multi-size .ico for the exe's file icon.
New-Item -ItemType Directory -Force build | Out-Null
$IcoPath = Join-Path $RepoRoot "build\clawdmeter.ico"
& $VenvPython -c "from daemon.icon_assets import load_logo_rgba; load_logo_rgba('firmware/src/logo.h').save(r'$IcoPath', sizes=[(16,16),(32,32),(48,48),(64,64)])"
if ($LASTEXITCODE -ne 0) { throw "icon render failed" }

$Name = "Clawdmeter"
$ModeFlag = "--windowed"
if ($Console) {
    $Name = "Clawdmeter-console"
    $ModeFlag = "--console"
}

Log "Building dist\$Name.exe..."
& $VenvPython -m PyInstaller `
    --noconfirm --clean --onefile $ModeFlag `
    --name $Name `
    --icon $IcoPath `
    --paths $RepoRoot `
    --add-data "$(Join-Path $RepoRoot 'firmware\src\logo.h');firmware/src" `
    --hidden-import pystray._win32 `
    --collect-submodules winrt `
    --workpath build\pyinstaller `
    --specpath build `
    daemon\tray_windows.py
if ($LASTEXITCODE -ne 0) { throw "PyInstaller build failed" }

Log "Done: $(Join-Path $RepoRoot "dist\$Name.exe")"
