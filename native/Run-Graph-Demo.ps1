param(
    [string]$Executable = "$PSScriptRoot/dist/vora.exe",
    [switch]$RealModel,
    [string]$Task = 'Write exactly one short sentence saying Vora workflows require review before use.'
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Build the 0.5 development engine first, or pass -Executable with its path.' }
$version = & $Executable --help
if ($LASTEXITCODE -ne 0 -or $version[0] -notmatch '^Vora 0\.5\.0-dev') { throw 'This demo requires the separate 0.5 development engine.' }
if (!$RealModel) {
    $workflow = Join-Path $PSScriptRoot 'examples/parallel-research.vora'
    & $Executable plan $workflow
    if ($LASTEXITCODE -ne 0) { throw 'Graph planning failed.' }
    & $Executable run $workflow --provider demo --input 'Plan a small documentation project' --workers 2
    if ($LASTEXITCODE -ne 0) { throw 'Parallel demo failed.' }
    return
}
$folder = Join-Path $env:LOCALAPPDATA ('Vora/graph-runs/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$workflow = Join-Path $PSScriptRoot 'examples/bounded-review.vora'
& $Executable run $workflow --input $Task --max-tokens 256 --max-calls 10 --checkpoint "$folder/checkpoint.json" --trace "$folder/trace.json" --output "$folder/result.json"
Write-Host "Private run files: $folder"
if ($LASTEXITCODE -ne 0) { throw 'The workflow failed. Inspect the trace; invalid/exhausted reviews are not accepted.' }
