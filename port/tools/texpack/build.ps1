# Builds the CubeEvil4 Texture Packer for Windows with Visual Studio (C++ workload) and CMake.
# Result in port\tools\texpack\build\Release (each one file, no DLLs):
#   CubeEvil4-TexturePacker.exe          the window version (double-click it)
#   CubeEvil4-TexturePacker-Console.exe  the command line version (same core, same .c4tex files)
#   texpack_tests.exe                    the tests (run here; the build fails when one fails)
# Optional: -Out <folder> copies both .exe files there.
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
$exes = @("CubeEvil4-TexturePacker.exe", "CubeEvil4-TexturePacker-Console.exe") | ForEach-Object { Join-Path $build "Release\$_" }
if ($Out -ne "") {
  New-Item -ItemType Directory -Force $Out | Out-Null
  Copy-Item $exes $Out -Force
  Write-Host "Copied to $Out"
}
$exes | ForEach-Object { Write-Host "Built $_" }
