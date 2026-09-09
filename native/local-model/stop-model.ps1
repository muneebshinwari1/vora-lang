$ErrorActionPreference = 'Stop'
$stateFile = Join-Path $PSScriptRoot 'server-process.json'
$expectedExe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'llama-b10859/llama-server.exe'))
if (!(Test-Path -LiteralPath $stateFile)) { Write-Output 'No owned server process record.'; return }
$recorded = Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json
if ($recorded.executable -ne $expectedExe) { throw 'Process record does not match this local runtime.' }
$serverProcess = Get-Process -Id $recorded.pid -ErrorAction SilentlyContinue
if (!$serverProcess) { Write-Output 'Recorded server is already stopped.'; return }
if ($serverProcess.Path -ne $expectedExe -or $serverProcess.StartTime.ToUniversalTime().Ticks -ne ([DateTime]$recorded.startTimeUtc).ToUniversalTime().Ticks) {
    throw 'PID identity changed; refusing to stop the process.'
}
Stop-Process -InputObject $serverProcess
Write-Output "Stopped owned model server: PID $($recorded.pid)"
