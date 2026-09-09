$ErrorActionPreference = 'Stop'
$modelRoot = $PSScriptRoot
$serverExe = [IO.Path]::GetFullPath((Join-Path $modelRoot 'llama-b10859/llama-server.exe'))
$modelFile = [IO.Path]::GetFullPath((Join-Path $modelRoot 'Qwen3-0.6B-Q8_0.gguf'))
$stateFile = Join-Path $modelRoot 'server-process.json'
function Wait-ModelReady([System.Diagnostics.Process] $OwnedProcess) {
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while ([DateTime]::UtcNow -lt $deadline) {
        $OwnedProcess.Refresh()
        if ($OwnedProcess.HasExited) { throw 'Owned model server exited during startup. Inspect server.stderr.log.' }
        try {
            $health = Invoke-RestMethod -Uri 'http://127.0.0.1:18080/health' -TimeoutSec 2
            if ($health.status -eq 'ok') { return }
        } catch {
            # The server returns 503 while loading, or may not have bound its port yet.
        }
        Start-Sleep -Seconds 1
    }
    throw 'Model did not become ready within 90 seconds. Inspect server.stderr.log; the owned process is recorded for stop-model.ps1.'
}
if (!(Test-Path -LiteralPath $serverExe) -or !(Test-Path -LiteralPath $modelFile)) {
    throw 'Pinned llama.cpp runtime or Qwen model is missing; see native/docs/model-setup.md.'
}
if (Test-Path -LiteralPath $stateFile) {
    $recorded = Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json
    $existing = Get-Process -Id $recorded.pid -ErrorAction SilentlyContinue
    if ($existing -and $existing.Path -eq $serverExe -and $existing.StartTime.ToUniversalTime().Ticks -eq ([DateTime]$recorded.startTimeUtc).ToUniversalTime().Ticks) {
        Wait-ModelReady $existing
        Write-Output "Owned model server already running: PID $($existing.Id), http://127.0.0.1:18080"
        return
    }
}
$listener = Get-NetTCPConnection -LocalPort 18080 -State Listen -ErrorAction SilentlyContinue
if ($listener) { throw 'Port 18080 is already in use; refusing to replace or stop another process.' }
$arguments = @(
    '-m', ('"' + $modelFile + '"'),
    '--host', '127.0.0.1', '--port', '18080',
    '--threads', '4', '--threads-batch', '4', '--threads-http', '2',
    '--ctx-size', '4096', '--parallel', '2', '--alias', 'local',
    '--jinja', '--chat-template-kwargs', '"{\"enable_thinking\":false}"', '--offline'
) -join ' '
$serverProcess = Start-Process -FilePath $serverExe -ArgumentList $arguments -WorkingDirectory $modelRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $modelRoot 'server.stdout.log') -RedirectStandardError (Join-Path $modelRoot 'server.stderr.log')
@{
    pid = $serverProcess.Id
    executable = $serverExe
    startTimeUtc = $serverProcess.StartTime.ToUniversalTime().ToString('o')
    arguments = $arguments
    endpoint = 'http://127.0.0.1:18080/v1/chat/completions'
} | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8
Wait-ModelReady $serverProcess
Write-Output "Started owned model server: PID $($serverProcess.Id), http://127.0.0.1:18080"
Write-Output 'Model health is ok; chat completions are ready.'
