# Dot-source this to get an MSVC (x64) build environment in the current PowerShell session:
#     . .\tools\vsenv.ps1
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$root = $null
if (Test-Path $vswhere) {
    $root = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $root) { throw "No Visual Studio / Build Tools with the C++ x64 toolset found. Install 'Desktop development with C++'." }
$bat = Join-Path $root "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}
Write-Host "MSVC environment ready: $((Get-Command cl).Source)"
