# Builds OpenDial for Windows and makes the release files in windows\dist:
#   OpenDial-Windows-<version>.zip         (unzip and run OpenDial.exe)
#   OpenDial-Windows-<version>-Setup.exe   (installer)
# -Arch x86 makes the 32-bit build instead (in build32, with "-x86" in the file names).
# Needs MSYS2 (C:\msys64) with mingw-w64-x86_64-gcc, cmake and ninja (for x86:
# mingw-w64-i686-gcc, cmake and ninja), and Inno Setup 6.
param([string]$Version = "1.0.0", [ValidateSet("x64", "x86")][string]$Arch = "x64", [string]$Msys = "",
      [string]$Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe")
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
if (-not $Msys) { $Msys = if ($Arch -eq "x86") { "C:\msys64\mingw32\bin" } else { "C:\msys64\mingw64\bin" } }
$build = if ($Arch -eq "x86") { "$here\build32" } else { "$here\build" }
$suffix = if ($Arch -eq "x86") { "-x86" } else { "" }
$env:PATH = "$Msys;$env:PATH"

# (quoted: PowerShell would split "-D...=3.5" at the dot)
cmake -S $here -B $build -G Ninja "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
      "-DCMAKE_C_COMPILER=gcc" "-DCMAKE_CXX_COMPILER=g++" | Out-Null
if ($LASTEXITCODE) { throw "cmake configure failed" }
ninja -C $build HiDefRadio
if ($LASTEXITCODE) { throw "build failed" }

$name = "OpenDial-Windows-$Version$suffix"
$dist = "$here\dist"
$out = "$dist\$name"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$build\OpenDial.exe" $out
Copy-Item -Recurse "$build\data" "$out\data"
Copy-Item "$here\README.md" "$out\README.txt"

$zip = "$dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal

& $Iscc /Q "/DAppVersion=$Version" "/DSourceDir=$out" "/DArch=$Arch" "$here\installer\HiDefRadio.iss"
if ($LASTEXITCODE) { throw "installer failed" }
Get-ChildItem $dist -File | Format-Table Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } } -AutoSize
