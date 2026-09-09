$ErrorActionPreference = 'Stop'
$modelRoot = $PSScriptRoot
$serverExe = [IO.Path]::GetFullPath((Join-Path $modelRoot '../local-model/llama-b10859/llama-server.exe'))
$modelFile = [IO.Path]::GetFullPath((Join-Path $modelRoot 'Qwen3-1.7B-Q8_0.gguf'))
$stateFile = Join-Path $modelRoot 'server-process.json'
function Wait-ModelReady([System.Diagnostics.Process] $OwnedProcess) {
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while ([DateTime]::UtcNow -lt $deadline) {
        $OwnedProcess.Refresh()
        if ($OwnedProcess.HasExited) { throw 'Owned candidate server exited during startup. Inspect server.stderr.log.' }
        try {
            $health = Invoke-RestMethod -Uri 'http://127.0.0.1:18081/health' -TimeoutSec 2
            if ($health.status -eq 'ok') { return }
        } catch {
            # A loading server can return 503 or may not have bound the port yet.
        }
        Start-Sleep -Seconds 1
    }
    throw 'Candidate model did not become ready within 90 seconds. Inspect server.stderr.log; stop-model.ps1 can stop the owned process.'
}
if (!(Test-Path -LiteralPath $serverExe) -or !(Test-Path -LiteralPath $modelFile)) {
    throw 'Shared llama.cpp runtime or candidate model is missing; see native/docs/model-upgrade.md.'
}
if ((Get-Item -LiteralPath $modelFile).Length -ne 1834426016) {
    throw 'Candidate model size does not match the official pinned artifact; download may be incomplete.'
}
if (Test-Path -LiteralPath $stateFile) {
    $recorded = Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json
    $existing = Get-Process -Id $recorded.pid -ErrorAction SilentlyContinue
    if ($existing -and $recorded.endpoint -eq 'http://127.0.0.1:18081/v1/chat/completions' -and $existing.Path -eq $serverExe -and $existing.StartTime.ToUniversalTime().Ticks -eq ([DateTime]$recorded.startTimeUtc).ToUniversalTime().Ticks) {
        Wait-ModelReady $existing
        Write-Output "Owned candidate server already running: PID $($existing.Id), http://127.0.0.1:18081"
        return
    }
}
$listener = Get-NetTCPConnection -LocalPort 18081 -State Listen -ErrorAction SilentlyContinue
if ($listener) { throw 'Port 18081 is already in use; refusing to replace or stop another process.' }
$arguments = @(
    '-m', ('"' + $modelFile + '"'),
    '--host', '127.0.0.1', '--port', '18081',
    '--threads', '4', '--threads-batch', '4', '--threads-http', '2',
    '--ctx-size', '4096', '--parallel', '2', '--alias', 'local',
    '--jinja', '--chat-template-kwargs', '"{\"enable_thinking\":false}"', '--offline'
) -join ' '
$serverProcess = Start-Process -FilePath $serverExe -ArgumentList $arguments -WorkingDirectory $modelRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $modelRoot 'server.stdout.log') -RedirectStandardError (Join-Path $modelRoot 'server.stderr.log')
@{
    pid = $serverProcess.Id
    executable = $serverExe
    model = $modelFile
    startTimeUtc = $serverProcess.StartTime.ToUniversalTime().ToString('o')
    arguments = $arguments
    endpoint = 'http://127.0.0.1:18081/v1/chat/completions'
} | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8
Wait-ModelReady $serverProcess
Write-Output "Started owned candidate server: PID $($serverProcess.Id), http://127.0.0.1:18081"
Write-Output 'Candidate model health is ok; chat completions are ready.'
