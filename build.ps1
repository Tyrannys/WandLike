#requires -Version 5
<#
  Release build:  .\build.ps1 [-Version 1.2.3]   ->  dist\WandLike-Setup.exe   (no console, no debug code)
  Debug build:    .\build.ps1 -Debug             ->  dist\WandLike-Debug.exe   (console window, debug page, logging)
                  .\build.ps1 --debug            (same thing)

  The debug exe is standalone (no installer). The release build excludes the wandlike.debug package and
  web\debug.js entirely, and fails if either ends up inside the exe.
#>
param([string]$Version, [Alias('Debug')][switch]$DebugBuild)
# "--debug" is not a PowerShell switch: it lands in $Version (positional) or $args
if ($Version -eq '--debug') { $Version = $null; $DebugBuild = $true }
if ($args -contains '--debug') { $DebugBuild = $true }
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
if ($Version -and $Version -notmatch '^\d+(\.\d+){1,3}$') { throw "Invalid -Version '$Version' (expected e.g. 1.2.3)" }

if ($Version -and -not $DebugBuild) {
    $v = Get-Content wandlike\version.py -Raw
    $v = $v -replace '__version__ = "[^"]*"', "__version__ = `"$Version`""
    Set-Content wandlike\version.py $v -NoNewline -Encoding utf8
}
$null = (Get-Content wandlike\version.py -Raw) -match '__version__ = "([^"]+)"'
$ver = $Matches[1]
$mode = if ($DebugBuild) { "DEBUG" } else { "release" }
Write-Host "Building WandLike $ver ($mode)"

if (-not (Test-Path .venv-build)) { python -m venv .venv-build }
$py = ".venv-build\Scripts\python.exe"
& $py -m pip install --quiet --disable-pip-version-check pyinstaller pywebview
if ($LASTEXITCODE) { throw "pip failed" }

& $py tools\make_icon.py assets\app.ico
New-Item -ItemType Directory dist -Force | Out-Null
$common = @("--collect-all", "webview", "--collect-all", "clr_loader", "--collect-all", "pythonnet",
            "--add-data", "$PSScriptRoot\wandlike\games;wandlike\games", "--add-data", "$PSScriptRoot\assets;assets",
            "--icon", "$PSScriptRoot\assets\app.ico", "--noconfirm", "--onefile")

if ($DebugBuild) {
    Remove-Item build\debug, dist\WandLike-Debug.exe -Recurse -Force -ErrorAction SilentlyContinue
    & $py -m PyInstaller @common --name WandLike-Debug `
        --add-data "$PSScriptRoot\wandlike\web;wandlike\web" `
        --distpath dist --workpath build\debug\work --specpath build\debug "$PSScriptRoot\run_app_debug.py"
    if ($LASTEXITCODE) { throw "debug build failed" }
    Write-Host "`ndist\WandLike-Debug.exe  (run it from a terminal; log also at %APPDATA%\WandLike\debug.log)"
    return
}

# ---- release ----
Remove-Item build\app, build\work-app, build\work-setup, build\payload, build\web-release, dist\WandLike-Setup.exe -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory build\payload, build\web-release -Force | Out-Null
Get-ChildItem wandlike\web -File | Where-Object Name -ne "debug.js" | Copy-Item -Destination build\web-release

# 1) the application (no console window; debug package and debug.js left out)
& $py -m PyInstaller @common --noconsole --name WandLike --exclude-module wandlike.debug `
    --add-data "$PSScriptRoot\build\web-release;wandlike\web" `
    --distpath build\app --workpath build\work-app --specpath build "$PSScriptRoot\run_app.py"
if ($LASTEXITCODE) { throw "app build failed" }

# guard: debug code must never ship
# Analysis-00.toc is skipped on purpose: it lists our own --exclude-module there
$tocs = Get-ChildItem build\work-app -Recurse -Include PYZ-00.toc, PKG-00.toc, EXE-00.toc
if ($tocs.Count -ne 3) { throw "cannot verify release: PyInstaller .toc files not found" }
$paths = @($tocs | ForEach-Object FullName)
if (@(Select-String -Path $paths -Pattern 'wandlike[./\\]debug').Count) { throw "release build contains the wandlike.debug package" }
if (@(Select-String -Path $paths -Pattern 'debug\.js').Count) { throw "release build contains web\debug.js" }

Copy-Item build\app\WandLike.exe build\payload\WandLike.exe
Copy-Item assets\app.ico build\payload\app.ico
Set-Content build\payload\version.txt $ver -NoNewline

# 2) the installer / launcher / uninstaller, embedding the app
& $py -m PyInstaller --noconfirm --onefile --noconsole --name WandLike-Setup --icon "$PSScriptRoot\assets\app.ico" `
    --add-data "$PSScriptRoot\build\payload;payload" `
    --distpath dist --workpath build\work-setup --specpath build "$PSScriptRoot\installer\setup_main.py"
if ($LASTEXITCODE) { throw "setup build failed" }

$hash = (Get-FileHash dist\WandLike-Setup.exe -Algorithm SHA256).Hash.ToLower()
Write-Host "dist\WandLike-Setup.exe  sha256 $hash"
