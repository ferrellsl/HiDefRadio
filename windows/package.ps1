# Builds HiDef Radio for Windows and makes the release files in windows\dist:
#   HiDefRadio-Windows-<version>.zip         (unzip and run HiDefRadio.exe)
#   HiDefRadio-Windows-<version>-Setup.exe   (installer)
# Needs MSYS2 with mingw-w64-x86_64-gcc, cmake and ninja (C:\msys64), and Inno Setup 6.
param([string]$Version = "1.0.0", [string]$Msys = "C:\msys64\mingw64\bin",
      [string]$Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe")
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$env:PATH = "$Msys;$env:PATH"

# (quoted: PowerShell would split "-D...=3.5" at the dot)
cmake -S $here -B "$here\build" -G Ninja "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
      "-DCMAKE_C_COMPILER=gcc" "-DCMAKE_CXX_COMPILER=g++" | Out-Null
if ($LASTEXITCODE) { throw "cmake configure failed" }
ninja -C "$here\build" HiDefRadio
if ($LASTEXITCODE) { throw "build failed" }

$name = "HiDefRadio-Windows-$Version"
$dist = "$here\dist"
$out = "$dist\$name"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$here\build\HiDefRadio.exe" $out
Copy-Item -Recurse "$here\build\data" "$out\data"
Copy-Item "$here\README.md" "$out\README.txt"

$zip = "$dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal

& $Iscc /Q "/DAppVersion=$Version" "/DSourceDir=$out" "$here\installer\HiDefRadio.iss"
if ($LASTEXITCODE) { throw "installer failed" }
Get-ChildItem $dist -File | Format-Table Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } } -AutoSize
