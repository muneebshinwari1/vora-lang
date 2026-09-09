# Vora 0.3 — agent workflow language

**The working C++ version is in [`native/`](native/README.md). Double-click [`native/Run Vora.cmd`](native/Run%20Vora.cmd) to enter a task and run the writer → critic → editor workflow using a bundled local AI model.** The native engine is `native/dist/vora.exe`; it does not require Python.

An [experimental review launcher](native/Run%20Vora%20Review.cmd) adds a larger local model, structured critique and native validation. Draft, feedback and final copy are displayed separately. It still makes factual mistakes; see the [independent quality results](native/docs/quality-comparison.md).

The documentation below describes the original Python prototype. Native build, live-model evidence and current limitations are documented in the [native README](native/README.md).

Vora 0.3 adds native deterministic output contracts and bounded automatic repair. A workflow can now require an exact sentence count or phrase, forbid text, and retry a failed step without writing an orchestration loop:

```text
require final sentences 2
require final contains "admission is free"
forbid final contains "$20"
repair final max 2
```

See the [native 0.3 documentation](native/README.md#deterministic-output-rules-and-repair) and [runnable guarded example](native/examples/guarded-notice.vora).

## Original Python prototype

A small language for describing a fixed graph of agent calls. Vora validates the graph before execution, runs independent steps concurrently, and emits structured traces. This is a local engineering prototype, not a published package or production service. The name is provisional; availability and trademarks have not been checked.

```text
workflow CompanyResearch(company):
    agent researcher = "Summarize what is known. Label uncertainty; you have no web tool."
    agent critic = "Identify risks and assumptions without inventing evidence."
    agent writer = "Write a concise proposal from the supplied analysis."
    facts = researcher("Analyze this company: {company}")
    risks = critic("Assess this company: {company}")
    report = writer("Facts: {facts}\nRisks: {risks}")
    return report
```

This is eight nonblank lines of workflow source. The parser, runtime, provider configuration and underlying model are additional machinery. No “50 lines become 8” performance or productivity claim has been validated.

## Run locally

Python 3.11+; no third-party runtime dependencies. From this directory:

```powershell
python -m vora check examples/company_research.vora
python -m vora plan examples/company_research.vora
python -m vora plan examples/company_research.vora --mermaid
python -m vora run examples/company_research.vora --input "An example software company" --trace demo-trace.json
python -m unittest discover -s tests -v
```

The default provider is a deterministic **simulation**. It validates orchestration plumbing, not research accuracy, reasoning quality or real model inference. It makes no network requests. `review.vora` demonstrates a builder → adversary → judge workflow; its output is also simulated unless you select a live provider.

If an Ollama server and a suitable model are already available:

```powershell
python -m vora run examples/review.vora --input "Design a support triage workflow" --provider ollama --model YOUR_INSTALLED_MODEL --retries 1 --max-calls 6
```

The Python CLI calls the local Ollama `/api/chat` endpoint. The adapter uses a 120-second socket timeout and requests at most 512 output tokens per call by default. Its protocol was tested against a local HTTP fixture, not a real Ollama model. The separate native build now includes a downloaded local Qwen model served by llama.cpp. [Ollama API](https://docs.ollama.com/api/chat)

## Execution contract

- `agent name = "role"` declares a role, not a separate persistent process or autonomous worker.
- `step = agent("prompt {input} {previous}")` declares one provider call. Identifier placeholders determine dependencies. Forward references are allowed; cycles and undefined references fail before execution. Names are ASCII identifiers and globally unique within the file.
- The header names the input variable. Prompts are double-quoted JSON strings; `\n` embeds a newline. Full-line `#` comments and blank lines are allowed. Indentation is cosmetic. Inline comments and multiline strings are not supported.
- Every `{identifier}` in a prompt is a reference. Other braces, such as JSON object braces, are ordinary text. There is currently no escape syntax for a literal `{identifier}`. Substituted input/output text is never interpolated again.
- All declared steps execute, including steps not needed by `return`. A dependency is satisfied only after its step completes. Ready steps are scheduled up to `--workers` (default 4); the executor need not wait for an entire displayed plan layer.
- `return` selects the final step's text; it must be the last statement. Outputs are plain nonempty strings, without schema enforcement or factual validation.
- `--retries N` permits at most N additional attempts per step, only for explicit transient provider errors. Attempts consume the shared atomic `--max-calls` limit. A graph whose minimum calls exceed that limit is rejected before any calls.
- Permanent failures stop further dependent work. Already-running independent calls may finish; they cannot be forcibly cancelled. Custom provider callables must be thread-safe and enforce their own I/O timeouts. There is no hard total workflow timeout or dollar budget.
- Traces contain run IDs, step/attempt identifiers, status and timing. They omit prompt/output text and provider exception messages. Workflow and step names are present, so do not put secrets in identifiers. Traces are not checkpoints: process restart does not resume a run.
- The runtime never executes DSL text as Python, runs shell commands, sends email, browses the web or performs tool calls. Agent names such as “researcher” do not grant those abilities. Ollama tool-call responses are rejected.

## Python integration

```python
from vora import load
from vora.runtime import run

def my_provider(*, role: str, prompt: str) -> str:
    # Wire an explicitly configured model client here.
    return "A nonempty response"

result = run(load("examples/review.vora"), "Your task", my_provider)
print(result.result)
```

`examples/company_research_baseline.py` declares the same graph in Python using the same runtime. Tests compare its graph and deterministic outputs with the DSL. This isolates DSL authoring; it is not a completed comparison with LangGraph or other frameworks.

## What has and has not been validated

Automated tests cover grammar rejection, graph dependencies, actual parallel overlap, retry classification, call limits, redaction and the local HTTP adapter. See [validation results](docs/validation-results.md) for the recorded run and [adversarial review](docs/adversarial-review.md) for independent findings.

The Python prototype tests did not establish real model quality, user productivity, willingness to pay or production reliability. The native build has separate live-inference evidence. There is no public deployment or paid service. Research shows substantial existing alternatives; see [research](docs/research.md) and the proposed [validation plan](docs/validation-plan.md).

Next investment decision: run the same representative workflow against concise Python and an existing agent framework, then conduct a small developer pilot. Add tools, durable execution or a hosted product only when their concrete requirements are established.
