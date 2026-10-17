#requires -Version 5
<#
  Builds dist\WandLike-Setup.exe.
  Usage: .\build.ps1 [-Version 1.2.3]     (default: version in wandlike\version.py)
#>
param([string]$Version)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if ($Version) {
    $v = Get-Content wandlike\version.py -Raw
    $v = $v -replace '__version__ = "[^"]*"', "__version__ = `"$Version`""
    Set-Content wandlike\version.py $v -NoNewline -Encoding utf8
}
$null = (Get-Content wandlike\version.py -Raw) -match '__version__ = "([^"]+)"'
$ver = $Matches[1]
Write-Host "Building WandLike $ver"

if (-not (Test-Path .venv-build)) { python -m venv .venv-build }
$py = ".venv-build\Scripts\python.exe"
& $py -m pip install --quiet --disable-pip-version-check pyinstaller
if ($LASTEXITCODE) { throw "pip failed" }

& $py tools\make_icon.py assets\app.ico
Remove-Item build, dist, *.spec -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory build\payload -Force | Out-Null

# 1) the application
& $py -m PyInstaller --noconfirm --onefile --noconsole --name WandLike --icon "$PSScriptRoot\assets\app.ico" `
    --add-data "$PSScriptRoot\wandlike\games;wandlike\games" --add-data "$PSScriptRoot\assets;assets" `
    --distpath build\app --workpath build\work-app --specpath build "$PSScriptRoot\run_app.py"
if ($LASTEXITCODE) { throw "app build failed" }
Copy-Item build\app\WandLike.exe build\payload\WandLike.exe
Copy-Item assets\app.ico build\payload\app.ico
Set-Content build\payload\version.txt $ver -NoNewline

# 2) the installer / launcher / uninstaller, embedding the app
& $py -m PyInstaller --noconfirm --onefile --noconsole --name WandLike-Setup --icon "$PSScriptRoot\assets\app.ico" `
    --add-data "$PSScriptRoot\build\payload;payload" `
    --distpath dist --workpath build\work-setup --specpath build "$PSScriptRoot\installer\setup_main.py"
if ($LASTEXITCODE) { throw "setup build failed" }

$hash = (Get-FileHash dist\WandLike-Setup.exe -Algorithm SHA256).Hash.ToLower()
Set-Content dist\WandLike-Setup.exe.sha256 "$hash  WandLike-Setup.exe"
Write-Host "dist\WandLike-Setup.exe  sha256 $hash"
