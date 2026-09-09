$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$modelFile = Join-Path $PSScriptRoot 'Qwen3-1.7B-Q8_0.gguf'
$source = 'https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/resolve/90862c4b9d2787eaed51d12237eafdfe7c5f6077/Qwen3-1.7B-Q8_0.gguf'
$expectedHash = '061b54daade076b5d3362dac252678d17da8c68f07560be70818cace6590cb1a'
if (!(Test-Path -LiteralPath $modelFile)) {
    Invoke-WebRequest -UseBasicParsing -Uri $source -OutFile $modelFile
}
if ((Get-Item -LiteralPath $modelFile).Length -ne 1834426016) {
    throw 'Candidate file size mismatch. Existing file was preserved; inspect an incomplete download before retrying.'
}
$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $modelFile).Hash
if ($actualHash -ne $expectedHash) { throw 'Candidate SHA256 mismatch. File preserved; do not start this model.' }
Write-Output 'Official pinned Qwen3-1.7B Q8_0 model verified.'
