# Vora Native 0.3 — working C++ engine

## Start here

**Double-click `Run Vora.cmd` in this folder.** Enter a writing or planning task. It starts the bundled local model if necessary, then runs three agents: writer → critic → editor. The final answer appears in the window; results and traces are saved under `outputs/`.

The workflow engine is compiled C++17 in `dist/vora.exe`. The model is served by the bundled C/C++ llama.cpp executable. **Python is not required to run either the engine or the launcher.** The launcher uses Windows PowerShell.

The default fast demo uses Qwen3 0.6B. **Double-click `Run Vora Review.cmd` for the experimental Qwen3 1.7B review mode.** Both display the draft, critic feedback and final draft separately. Review mode requests structured critique and stops dependent steps if that critique is malformed. It uses more CPU time and can still miss errors or invent claims; it has not met the quality promotion gate. See the [six-case comparison and remediation results](docs/quality-comparison.md).

There is no web search or external tool execution. Running either bundled model uses local CPU/RAM, with no API key or paid API call. Python is used only for development tests.

## Your eight-line workflow

The fast launcher runs `quickstart-fast.vora` (the original example also remains in `quickstart.vora`). Edit it to change agent roles and how results flow. The experimental review launcher runs `review.vora`, with one additional validation line:

```text
workflow WriteAndReview(input):
    agent writer = "Write a useful draft in at most 3 short sentences. Follow the user's request."
    agent critic = "Review the draft. Give one concrete improvement in one short sentence."
    agent editor = "Apply the critique. Return only the improved text, at most 3 short sentences."
    draft = writer("Task: {input}")
    review = critic("Task: {input}\nDraft: {draft}")
    final = editor("Task: {input}\nDraft: {draft}\nCritique: {review}")
    return final
```

Independent steps run concurrently, up to the configured worker count. The native executor schedules batches of ready steps and waits for that batch before scheduling another. Placeholders define dependencies. Invalid references, duplicate names and cycles are rejected before a model call.

### Optional critic response validation

Add `validate review as critique` before `return` to require a structured review before dependent steps execute. The response must be a JSON object with exactly `verdict`, `issues`, and `instruction`:

```json
{"verdict":"revise","issues":["The warranty claim is unsupported."],"instruction":"Remove the warranty claim."}
```

`verdict` must be `keep` or `revise`; `issues` must contain nonempty strings; `instruction` must be nonempty. `keep` requires no issues, while `revise` requires at least one. Duplicate JSON keys, markdown-wrapped JSON, replacement prose and contradictory verdict/issue combinations fail. A failure emits `validation_failed`, is not retried, and blocks the editor that depends on that review. Already-running independent steps can finish. For local inference, the engine requests a JSON schema only for the validated step, then independently validates the returned text.

This validates structure and verdict consistency, **not factual accuracy or whether the review found every error**. The original unvalidated DSL remains supported; the Python prototype does not implement this native-only directive.

### Deterministic output rules and repair

Vora 0.3 can enforce small, explicit output contracts in native C++ and ask the same agent to repair a failed response a bounded number of times:

```text
workflow GuardedNotice(input):
    agent writer = "Write exactly two sentences. Include the exact phrase 'admission is free'."
    final = writer("Facts and task: {input}")
    require final sentences 2
    require final contains "admission is free"
    forbid final contains "$20"
    repair final max 2
    return final
```

`contains` checks are exact and case-sensitive. `sentences` counts runs of `.`, `!`, or `?` terminal punctuation; it is deliberately a predictable format check rather than a linguistic sentence parser. Counts must be 1–100. A step may have several rules. `repair STEP max N` accepts 1–5 repair attempts and requires at least one validation on that step.

On failure, the engine emits `step_repair`, supplies only the original prompt plus the failed constraints, and calls the same step provider again. Every repair consumes the shared `--max-calls` budget. Transport retries remain a separate mechanism controlled by `--retries`. When the repair allowance ends, `validation_failed` blocks dependent steps. These checks can guarantee their narrow string/format conditions; they do not prove factual accuracy.

The complete runnable source is [`examples/guarded-notice.vora`](examples/guarded-notice.vora).

## Terminal commands

From this folder, after starting the local model:

```powershell
.\local-model\start-model.ps1
.\dist\vora.exe check quickstart.vora
.\dist\vora.exe plan quickstart.vora --mermaid
.\dist\vora.exe run quickstart.vora --input "Write a short welcome message for a coding class."
.\dist\vora.exe run examples\guarded-notice.vora --input "The community event is at Cedar Hall and admission is free."
```

To run the full launcher without interactive prompts:

```powershell
.\Start-Vora.ps1 -Task "Write a short welcome message for a coding class." -NoPause
.\Start-Vora.ps1 -Mode review -Task "Write one sentence. Facts: a coding class starts Monday at 4 PM." -NoPause
```

Use `--input-file task.txt` for multiline content. `--output result.json` saves all step outputs and the final result. `--trace trace.json` saves event metadata. Saved task/result files contain your text; standard trace events omit it.

Default provider is **real local inference** at `http://127.0.0.1:18080/v1/chat/completions`, model alias `local`. Simulation is available only with explicit `--provider demo` and is visibly labeled. `--endpoint`, `--model`, `--max-tokens`, `--workers`, `--retries` and `--max-calls` are configurable; see `dist/vora.exe --help`.

Only loopback endpoints are accepted. `/v1/chat/completions` requires a complete response with `finish_reason: stop`; truncated or tool-call responses fail. An existing local Ollama `/api/chat` endpoint is also supported, but the bundled/validated inference path uses llama.cpp.

Review mode uses port `18081`, a 256-token reasoning budget and a 768-token combined generation limit per call. `--reasoning-budget` defaults to zero in the CLI. The bundled llama.cpp server separates reasoning from final content; the engine passes only final content downstream. Reasoning budget is per thinking block, not a total workflow deadline. Nonzero reasoning budgets are not supported by this client's Ollama path. These settings improve the available reasoning allowance; they do not establish factual correctness.

## Stop the model

```powershell
.\local-model\stop-model.ps1
.\local-model-upgrade\stop-model.ps1
```

The model stays running between tasks for convenience. The stop script verifies the recorded process ID, executable path and process start time before stopping the process it owns. It does not stop another application using the same port. No machine startup service or recurring automation was installed.

## Build and verify

Visual Studio 2022 C++ Build Tools, Windows SDK and CMake are required to rebuild; they are not required to use the already-built executable on this machine.

```powershell
.\build.ps1
```

The engine uses the static MSVC runtime (`/MT`) and Windows WinHTTP. CMake builds native parser/runtime test executables and runs them with CTest. Development-only HTTP/CLI fixture tests can additionally be run with `python tests/test_http_cli.py`; Python is test tooling here, not a runtime dependency.

See [validation evidence](docs/validation.md), [independent review](docs/adversarial.md) and [model source/setup](docs/model-setup.md).

## Current boundaries

This build executes a static graph of model calls. It has no autonomous tool access, persistent agent memory, durable resume, external actions or hosted account system. Retries apply only to transient failures and count toward the shared call limit. In-flight HTTP requests have socket-operation timeouts but no hard total workflow deadline or forced cancellation. Model output accuracy and commercial demand have not been established by these engineering tests.
