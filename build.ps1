#requires -Version 5
<#
  Release build:  .\build.ps1 [-Version 2.0.1]   ->  dist\Hexwright-Setup.exe   (no console)
  Debug build:    .\build.ps1 -Debug             ->  dist\Hexwright-Debug.exe   (console window + debug.log)
                  .\build.ps1 --debug            (same thing)
  Tests only:     .\build.ps1 -TestOnly

  Needs: Visual Studio 2022+ or Build Tools with "Desktop development with C++" (MSVC x64, CMake and Ninja
  come with it). Everything else is vendored in third_party/.
#>
param([string]$Version, [Alias('Debug')][switch]$DebugBuild, [switch]$TestOnly, [switch]$SkipTests)
if ($Version -eq '--debug') { $Version = $null; $DebugBuild = $true }
if ($args -contains '--debug') { $DebugBuild = $true }
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
if ($Version -and $Version -notmatch '^\d+(\.\d+){1,3}$') { throw "Invalid -Version '$Version' (expected e.g. 2.0.1)" }

. .\tools\vsenv.ps1
function Run($exe, [string[]]$a) { & $exe @a; if ($LASTEXITCODE) { throw "$exe failed ($LASTEXITCODE)" } }

New-Item -ItemType Directory dist -Force | Out-Null
$verArg = if ($Version) { @("-DHX_VERSION=$Version") } else { @() }

if ($DebugBuild) {
    Write-Host "Building Hexwright (DEBUG)"
    Run cmake (@("-S", ".", "-B", "build\dbg", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DHX_DEBUG_BUILD=ON") + $verArg)
    Run cmake @("--build", "build\dbg", "--target", "hexwright")
    Copy-Item build\dbg\Hexwright-Debug.exe dist\Hexwright-Debug.exe -Force
    Write-Host "`ndist\Hexwright-Debug.exe  (run it from a terminal; the log is also written to %APPDATA%\Hexwright\debug.log)"
    return
}

Write-Host "Building Hexwright (release)"
Run cmake (@("-S", ".", "-B", "build\win", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release") + $verArg)
if ($TestOnly) {
    Run cmake @("--build", "build\win", "--target", "hextests")
    Run build\win\hextests.exe @()
    return
}
Run cmake @("--build", "build\win")
if (-not $SkipTests) { Run build\win\hextests.exe @() }
Copy-Item build\win\Hexwright-Setup.exe dist\Hexwright-Setup.exe -Force
Copy-Item build\win\hexctl.exe dist\hexctl.exe -Force
Write-Host "`ndist\Hexwright-Setup.exe  (installs to %LOCALAPPDATA%\Hexwright)`ndist\hexctl.exe          (authoring CLI)"
