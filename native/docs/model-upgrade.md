# Candidate local-model comparison

This candidate uses official Qwen3-1.7B Q8_0 for a controlled comparison with the existing Qwen3-0.6B model. A larger parameter count alone does not establish better critic reliability. Promote the candidate only after comparing identical evaluation prompts and expected verdicts, including correct answers, false claims and adversarial text. Compare latency as well as accuracy.

## Official provenance

- [Official model repository](https://huggingface.co/Qwen/Qwen3-1.7B-GGUF): Qwen3-1.7B, Q8_0, Apache 2.0. Chosen as the modest CPU candidate for the i7-8650U / 16 GB machine.
- Pinned repository revision: `90862c4b9d2787eaed51d12237eafdfe7c5f6077`.
- [Exact model download](https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/resolve/90862c4b9d2787eaed51d12237eafdfe7c5f6077/Qwen3-1.7B-Q8_0.gguf): `1,834,426,016` bytes.
- Expected SHA256: `061b54daade076b5d3362dac252678d17da8c68f07560be70818cace6590cb1a`, obtained from the LFS metadata in the [official model API](https://huggingface.co/api/models/Qwen/Qwen3-1.7B-GGUF?blobs=true).
- The candidate reuses `native/local-model/llama-b10859/llama-server.exe`. No runtime duplicate or global installation is needed. The runtime pin and license are documented in [model-setup.md](model-setup.md).

`native/local-model-upgrade/manifest.json` records provenance. A model-license copy is in the same directory. `download-model.ps1` verifies size and SHA256 and preserves an existing file on mismatch.

## Independent candidate endpoint

From the `vora-lang` directory, using Windows PowerShell 5.1 or later:

```powershell
& ./native/local-model-upgrade/download-model.ps1
& ./native/local-model-upgrade/start-model.ps1
Invoke-RestMethod http://127.0.0.1:18081/health
```

Stop only this candidate:

```powershell
& ./native/local-model-upgrade/stop-model.ps1
```

The baseline remains at `127.0.0.1:18080`; the candidate uses `127.0.0.1:18081`. Both expose `/v1/chat/completions` and model alias `local`. The candidate is launched hidden, uses four CPU threads, two parallel slots, context size 4096 and non-thinking template settings. It waits for health readiness with a 90-second deadline and checks process liveness. Stop validates recorded model, endpoint, executable, PID and process start time to avoid stopping another process.

Exact native arguments (the script supplies the full model path and Windows quoting):

```text
-m "<local-model-upgrade>/Qwen3-1.7B-Q8_0.gguf" --host 127.0.0.1 --port 18081 --threads 4 --threads-batch 4 --threads-http 2 --ctx-size 4096 --parallel 2 --alias local --jinja --chat-template-kwargs "{\"enable_thinking\":false}" --offline
```

Example request:

```powershell
$request = @{
    model = 'local'
    messages = @(@{role = 'user'; content = 'What is 2 plus 2? Answer in one short sentence.'})
    temperature = 0
    max_tokens = 64
    chat_template_kwargs = @{enable_thinking = $false}
} | ConvertTo-Json -Depth 5
$reply = Invoke-RestMethod -Uri 'http://127.0.0.1:18081/v1/chat/completions' -Method Post -ContentType 'application/json' -Body $request -TimeoutSec 120
$reply.choices[0].message.content
```

Keep prompt text, generation parameters, template kwargs and expected verdicts identical across models. Run model comparisons sequentially so two active CPU workloads do not distort latency. Record truncations and invalid verdicts as failures rather than treating them as approvals. Basic health and arithmetic checks prove inference wiring, not critic reliability or safety.

## Setup verification

On 2026-09-08 the complete downloaded model matched the official SHA256 above. Cold start and repeated start succeeded under Windows PowerShell 5.1, and `/health` returned `{"status":"ok"}`. Startup logs confirmed two slots with 2048 context tokens each. The baseline service was left untouched for the comparison. The candidate's current PID and exact launch arguments are recorded in `native/local-model-upgrade/server-process.json`; logs and verification metadata are in that directory. Critic evaluation and any default-model selection are separate from this setup result.
