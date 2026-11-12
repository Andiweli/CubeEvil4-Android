# Builds the CubeEvil4 Texture Packer for Windows with Visual Studio (C++ workload) and CMake.
# Result: port\tools\texpack\build\Release\CubeEvil4-TexturePacker.exe (one file, no DLLs) and the tests.
# Optional: -Out <folder> copies the .exe there.
param([string]$Out = "")
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $here "build"
cmake -S $here -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }
& (Join-Path $build "Release\texpack_tests.exe")
if ($LASTEXITCODE -ne 0) { throw "tests failed" }
$exe = Join-Path $build "Release\CubeEvil4-TexturePacker.exe"
if ($Out -ne "") {
  New-Item -ItemType Directory -Force $Out | Out-Null
  Copy-Item $exe $Out -Force
  Write-Host "Copied to $Out"
}
Write-Host "Built $exe"
