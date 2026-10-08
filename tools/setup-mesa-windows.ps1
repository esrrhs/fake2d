# Installs the Mesa3D llvmpipe software OpenGL next to a Windows binary.
#
# GitHub Windows runners (and VMs / RDP machines without a real GPU) have no
# hardware OpenGL: WGL fails with "The driver does not appear to support
# OpenGL" (GLFW 65542). Mesa's llvmpipe is a pure-CPU OpenGL implementation
# shipped as a drop-in opengl32.dll; the DLL in the application directory is
# loaded before System32\opengl32.dll, so no system changes are needed.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\setup-mesa-windows.ps1 `
#     -TargetDir build\bin
#
# Then run with:
#   $env:GALLIUM_DRIVER="llvmpipe"; $env:LIBGL_ALWAYS_SOFTWARE="1"
param(
    [Parameter(Mandatory = $true)]
    [string]$TargetDir,
    [string]$Version = "26.2.4",
    # MSVC or MinGW runtime build; MinGW matches the MSYS2 toolchain used in CI.
    [ValidateSet("mingw", "msvc")]
    [string]$Flavor = "mingw"
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$TargetDir = (Resolve-Path -LiteralPath $TargetDir).Path
$work = Join-Path $env:TEMP "mesa-dist-win-$Version-$([guid]::NewGuid().Guid)"
New-Item -ItemType Directory -Path $work -Force | Out-Null

$pkg = "mesa3d-$Version-release-$Flavor.7z"
$url = "https://github.com/pal1000/mesa-dist-win/releases/download/$Version/$pkg"
$archive = Join-Path $work $pkg
Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing

$sevenZip = @(
    "$env:ProgramFiles\7-Zip\7z.exe",
    "${env:ProgramFiles(x86)}\7-Zip\7z.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sevenZip) {
    $cmd = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($cmd) { $sevenZip = $cmd.Source }
}
if (-not $sevenZip) { throw "7-Zip not found (install 7zip or use a runner that ships it)" }

& $sevenZip x $archive "-o$work\mesa" -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw "7z extraction failed" }

$src = Join-Path $work "mesa\x64"
if (-not (Test-Path (Join-Path $src "opengl32.dll"))) {
    throw "x64\\opengl32.dll missing in $pkg"
}
Copy-Item -Path (Join-Path $src "*") -Destination $TargetDir -Recurse -Force

foreach ($dll in @("opengl32.dll", "libgallium_wgl.dll")) {
    if (-not (Test-Path (Join-Path $TargetDir $dll))) {
        throw "$dll was not installed into $TargetDir"
    }
}
Write-Host "Mesa $Version llvmpipe installed into $TargetDir"
