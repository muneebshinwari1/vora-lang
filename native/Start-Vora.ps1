param(
    [string]$Task,
    [ValidateSet('fast', 'review')]
    [string]$Mode = 'fast',
    [switch]$NoPause
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
try {
    $taskExe = Join-Path $PSScriptRoot 'dist\vora.exe'
    if (-not (Test-Path -LiteralPath $taskExe)) { throw 'vora.exe is missing. Run build.ps1 first.' }
    Write-Host ''
    Write-Host 'VORA - Native C++ agent workflows' -ForegroundColor Cyan
    Write-Host 'Writer -> Critic -> Editor | Real local AI | No Python required'
    if ($Mode -eq 'review') {
        $taskModelFolder = 'local-model-upgrade'
        $taskEndpoint = 'http://127.0.0.1:18081/v1/chat/completions'
        $taskWorkflowFile = 'review.vora'
        $taskReasoning = 256
        $taskTokens = 768
        Write-Host 'Experimental review: Qwen3 1.7B, structured critique and bounded reasoning.'
        Write-Host 'Check the final draft against your facts; the critic can miss or invent errors.' -ForegroundColor Yellow
    } else {
        $taskModelFolder = 'local-model'
        $taskEndpoint = 'http://127.0.0.1:18080/v1/chat/completions'
        $taskWorkflowFile = 'quickstart-fast.vora'
        $taskReasoning = 0
        $taskTokens = 256
        Write-Host 'Fast demo mode: Qwen3 0.6B, limited review quality.'
    }
    Write-Host ''
    if ([string]::IsNullOrWhiteSpace($Task)) { $Task = Read-Host 'What would you like the agents to write or plan?' }
    if ([string]::IsNullOrWhiteSpace($Task)) { throw 'Please enter a task.' }
    Write-Host 'Preparing the local model...'
    & (Join-Path $PSScriptRoot "$taskModelFolder\start-model.ps1")
    $taskOutputDir = Join-Path $PSScriptRoot 'outputs'
    New-Item -ItemType Directory -Path $taskOutputDir -Force | Out-Null
    $taskRunName = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6)
    $taskInputPath = Join-Path $taskOutputDir "$taskRunName-input.txt"
    $taskTracePath = Join-Path $taskOutputDir "$taskRunName-trace.json"
    $taskResultPath = Join-Path $taskOutputDir "$taskRunName-result.json"
    [IO.File]::WriteAllText($taskInputPath, $Task, (New-Object System.Text.UTF8Encoding($false)))
    if ($Mode -eq 'review') {
        Write-Host 'Running three agents. Review mode can take several minutes on this CPU...' -ForegroundColor Yellow
    } else {
        Write-Host 'Running three agents. The first response can take a little longer...' -ForegroundColor Yellow
    }
    $taskNativeOutput = @(& $taskExe run (Join-Path $PSScriptRoot $taskWorkflowFile) --input-file $taskInputPath --endpoint $taskEndpoint --workers 2 --retries 1 --max-calls 6 --max-tokens $taskTokens --reasoning-budget $taskReasoning --trace $taskTracePath --output $taskResultPath)
    if ($LASTEXITCODE -ne 0) { throw 'Workflow failed. Check the model log and the execution trace.' }
    $taskSavedResult = [IO.File]::ReadAllText($taskResultPath) | ConvertFrom-Json
    Write-Host ''
    Write-Host 'DRAFT' -ForegroundColor Cyan
    Write-Host $taskSavedResult.outputs.draft
    Write-Host ''
    Write-Host 'CRITIC' -ForegroundColor Cyan
    if ($Mode -eq 'review') {
        $taskCritique = $taskSavedResult.outputs.review | ConvertFrom-Json
        Write-Host ("Decision: " + $taskCritique.verdict.ToUpperInvariant())
        foreach ($taskIssue in $taskCritique.issues) { Write-Host ("- " + $taskIssue) }
        Write-Host ("Action: " + $taskCritique.instruction)
    } else { Write-Host $taskSavedResult.outputs.review }
    Write-Host ''
    Write-Host 'FINAL DRAFT' -ForegroundColor Green
    Write-Host $taskSavedResult.result
    Write-Host ''
    Write-Host "Saved result: $taskResultPath" -ForegroundColor Green
    Write-Host "Saved trace:  $taskTracePath"
    Write-Host "The local model stays available. Stop it with $taskModelFolder\stop-model.ps1."
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    if ($NoPause) { exit 1 }
} finally {
    if (-not $NoPause) { Read-Host 'Press Enter to close' | Out-Null }
}
