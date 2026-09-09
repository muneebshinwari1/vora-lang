$ErrorActionPreference = 'Stop'
& cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build "$PSScriptRoot\build" --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
& ctest --test-dir "$PSScriptRoot\build" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
Write-Host "Built: $PSScriptRoot\dist\vora.exe"
