$ErrorActionPreference = 'Stop'
$stateFile = Join-Path $PSScriptRoot 'server-process.json'
$expectedExe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../local-model/llama-b10859/llama-server.exe'))
$expectedModel = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'Qwen3-1.7B-Q8_0.gguf'))
if (!(Test-Path -LiteralPath $stateFile)) { Write-Output 'No owned candidate server process record.'; return }
$recorded = Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json
if ($recorded.executable -ne $expectedExe -or $recorded.model -ne $expectedModel -or $recorded.endpoint -ne 'http://127.0.0.1:18081/v1/chat/completions') {
    throw 'Process record does not match this candidate runtime and endpoint.'
}
$serverProcess = Get-Process -Id $recorded.pid -ErrorAction SilentlyContinue
if (!$serverProcess) { Write-Output 'Recorded candidate server is already stopped.'; return }
if ($serverProcess.Path -ne $expectedExe -or $serverProcess.StartTime.ToUniversalTime().Ticks -ne ([DateTime]$recorded.startTimeUtc).ToUniversalTime().Ticks) {
    throw 'PID identity changed; refusing to stop the process.'
}
Stop-Process -InputObject $serverProcess
Write-Output "Stopped owned candidate server: PID $($recorded.pid)"
