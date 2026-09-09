# Local model setup

The native demo uses a real local Qwen3-0.6B model through the portable Windows x64 CPU build of llama.cpp. No Python interpreter, paid API, credentials or global installation is required. Model inference stays on this machine after setup.

## Pinned downloads

The official Qwen repository supplied only Q8_0 when inspected on 2026-09-08. Its 639,446,688-byte model was accepted instead of the initially proposed smaller quantization. Downloaded archives/model plus the extracted runtime remain below 1 GB.

| Artifact | Pin | SHA256 |
| --- | --- | --- |
| [llama.cpp Windows CPU zip](https://github.com/ggml-org/llama.cpp/releases/download/b10859/llama-b10859-bin-win-cpu-x64.zip) | `b10859`, source commit `ca86fb222e0080d4d102f4390d5c7279dffb3277`; 18,426,061 bytes | `861b8ecbea40d0b097a2029fda689e769fda5b8f1cfaad02b2b044fe143101b5` |
| [Qwen GGUF](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/23749fefcc72300e3a2ad315e1317431b06b590a/Qwen3-0.6B-Q8_0.gguf) | Official `Qwen/Qwen3-0.6B-GGUF`, revision `23749fefcc72300e3a2ad315e1317431b06b590a`; Q8_0 | `9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031` |

The release asset digest came from the [GitHub release API](https://api.github.com/repos/ggml-org/llama.cpp/releases/tags/b10859). The model hash came from the LFS metadata in the [official Hugging Face API](https://huggingface.co/api/models/Qwen/Qwen3-0.6B-GGUF?blobs=true). Local provenance is saved in `native/local-model/manifest.json`. License copies are alongside it; preserve these when distributing their respective artifacts.

## Start and stop

Run from the `vora-lang` directory in PowerShell:

```powershell
& ./native/local-model/start-model.ps1
Invoke-RestMethod http://127.0.0.1:18080/health
& ./native/local-model/stop-model.ps1
```

Start checks process liveness and polls health until ready, with a 90-second deadline; it also checks readiness when an owned server already exists. The launch script binds exclusively to `127.0.0.1:18080`, limits generation and batch processing to four CPU threads, uses context size 4096 and two parallel slots, and sets model alias `local`. It launches a hidden window and records the PID, executable, start time and exact arguments in `server-process.json`. Standard output and error go to local log files. Stop acts only if the recorded PID, executable path and process start time all match; it never kills a process merely because it uses the port.

The executable arguments are:

```text
-m "<local-model>/Qwen3-0.6B-Q8_0.gguf" --host 127.0.0.1 --port 18080 --threads 4 --threads-batch 4 --threads-http 2 --ctx-size 4096 --parallel 2 --alias local --jinja --chat-template-kwargs "{\"enable_thinking\":false}" --offline
```

`start-model.ps1` supplies the correct Windows quoting and absolute paths. The pinned executable's `--help` confirms these flags. Its startup log confirms 2048 context tokens per slot with this 4096-total, two-slot configuration; use short prompts and modest response limits for this demo.

## Completion request

The endpoint accepts an OpenAI-compatible chat-completion JSON request. Include the per-request template setting to keep Qwen's thinking disabled explicitly:

```powershell
$request = @{
    model = 'local'
    messages = @(@{ role = 'user'; content = 'What is 2 plus 2? Answer briefly.' })
    max_tokens = 64
    temperature = 0
    chat_template_kwargs = @{ enable_thinking = $false }
} | ConvertTo-Json -Depth 5
$response = Invoke-RestMethod -Uri 'http://127.0.0.1:18080/v1/chat/completions' -Method Post -ContentType 'application/json' -Body $request
$response.choices[0].message.content
```

This small model demonstrates actual inference and orchestration. It can produce incorrect or low-quality answers and has no live web knowledge or tools merely because a workflow calls an agent "researcher." CPU latency varies with prompt length and concurrent work. An API usage object reports token counts, not a paid invoice.

## Verification performed

Both downloaded SHA256 values matched their official metadata. On 2026-09-08 the local health endpoint returned `{"status":"ok"}` and the request above, with a one-sentence instruction, returned `2 plus 2 equals 4.` with 26 prompt tokens and 9 completion tokens. Full response metadata is preserved in `native/local-model/verified-completion.json`. Total local artifacts at initial setup were approximately 704.6 MB, including the archive and extracted runtime. This proves local inference works for the tested request; it is not a model-quality benchmark.

The stop, cold-start, repeated-start and post-restart completion paths were verified with Windows `powershell.exe` 5.1. The server was left running after verification; the current PID is in `server-process.json` rather than hard-coded into scripts.
