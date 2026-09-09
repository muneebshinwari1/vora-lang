# Native validation — 2026-09-08

## Original built executable (before critique validation)

- Windows x64, MSVC 19.42.34433, C++17, Release, static MSVC runtime.
- Executable: `native/dist/vora.exe`, 560,640 bytes.
- SHA256: `D7E077B326ACF2E0784494BEE25BD054E64C2E1181942F59F8827C0BF847AF9C`.
- `dumpbin /DEPENDENTS` lists only `WINHTTP.dll` and `KERNEL32.dll`. No Python or Python DLL is required by the workflow engine.
- Bundled local inference is a separate llama.cpp C/C++ process. It is not embedded in the 560 KB engine executable.

## Original automated checks

`native/build.ps1` configured, built and ran CTest successfully: parser and runtime targets passed, zero failures. Parser tests include JSON escapes, forward references, graph plans and 32 invalid programs. The final runtime target contains 13 behavioral cases, including actual parallel overlap, dependency joins, retry classification/limits, blank-output rejection, exception sanitization and permanent failure handling.

`python native/tests/test_http_cli.py` passed all 7 tests in 2.458 seconds. This development-only fixture calls the compiled native exe over real loopback HTTP. It checks request shape, input-file preservation, complete vs truncated responses, malformed/tool output, redirects, retryable/permanent statuses, endpoint restrictions and explicit demo isolation. A first fixture run found a test expectation mismatch from Windows newline translation; the fixture now writes its intended LF bytes explicitly. No runtime newline rewriting was introduced.

The adversarial reviewer found whitespace-only output acceptance and silent acceptance of incomplete/truncated completions. Both were corrected before the final native build and are covered by regression checks. See [review](adversarial.md).

## Real end-to-end run

The Windows PowerShell 5.1 launcher was invoked with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File native\Start-Vora.ps1 -Task 'Write two short sentences advertising a reusable steel water bottle. Mention less single-use plastic. Do not invent certifications or numeric claims.' -NoPause
```

It returned exit code 0. The compiled C++ engine called the actual local Qwen3-0.6B Q8_0 model through llama.cpp for all three steps. Execution trace:

| Step | Started at | Completed at |
|---|---:|---:|
| draft | 1 ms | 2,989 ms |
| review | 2,989 ms | 5,188 ms |
| final | 5,189 ms | 7,744 ms |

All three steps completed on their first attempt. The workflow recorded 3 model calls and completed in 7.744 seconds for this particular warm-server run. This is an observation, not a general latency guarantee.

Final model output:

> A reusable steel water bottle is a stylish and durable option for eco-conscious consumers, reducing plastic waste while supporting sustainable practices.

Saved local evidence (under the gitignored `native/outputs/` directory):

- `20260908-214251-51f19b-result.json` — full step outputs and events.
- `20260908-214251-51f19b-trace.json` — metadata-only trace.

The small model returned one sentence despite the two-sentence request, and its critic output was a rewrite rather than a clearly labeled critique. Thus this verifies real model execution and data flow, not reliable instruction adherence or reasoning quality. No simulation fallback was used.

## Model lifecycle

The model agent verified Windows PowerShell 5.1 stop, cold start with health readiness, repeated start reusing the owned server, and a real completion after restart. The server remains available on `127.0.0.1:18080`. Source versions, hashes, licenses and model setup are recorded in [model-setup.md](model-setup.md) and `native/local-model/manifest.json`.

## Unvalidated claims

No broad model-quality benchmark, customer trial, productivity comparison, hosted deployment or commercial validation has been performed. Neither traces nor passing tests establish durable resume, hard workflow deadlines, exactly-once side effects or production readiness.

## Critique-validation update, 2026-09-08

The current Release executable is 586,240 bytes, SHA256 `49B17AFE883C15C39B8F5857126FC28792516884DA72A6D819F42CEC78FBAE3B`. The original build measurements above remain historical evidence.

The updated build passed all three CTest targets (parser, runtime, validation; 4.07 seconds) and all 11 development HTTP/CLI tests (3.671 seconds). Added checks cover validation declarations, duplicate JSON keys, empty/contradictory critique fields, malformed review blocking the editor without retry, schema requests only for validated steps, bounded-reasoning request parameters, separation of reasoning from returned content, and provider state surviving transient retries.

`review.vora` is a nine-line, three-agent workflow and passes the compiled parser's `check` command. The experimental review profile requests Qwen3 1.7B at port 18081, with a 256-token reasoning budget and 768-token total generation allowance per call. The fast profile remains available at port 18080. JSON structure is checked by native C++; factual judgment remains a model-quality limitation.

The [independent quality comparison](quality-comparison.md) records all six fixed cases, original comparison results and separately labeled tuned remediation rounds. This is a small regression exercise, not commercial or broad quality validation. The default fast demo has not been replaced by the experimental candidate because the documented factual-quality gate was not met.

The final v4 round completed four workflows: invoice, workshop and correct-control cases passed; the product description added unsupported facts. The remaining two cases failed during review after approximately 124 seconds, with no critic/final content to grade. Overall: 3/6 end-to-end passes. A runtime success must not be counted as factual correctness, and missing output must not be counted as injection resistance.

During the late v4 run, an OS snapshot reported roughly 431 MiB free physical memory and 100% CPU load; server generation logs showed a substantial speed drop. These observations accompany the timeouts but do not establish a single cause or a controlled latency comparison. After the frozen run, the owned idle 0.6B server was stopped before launcher verification. The fast launcher can start it again.

## Updated launcher verification

Windows PowerShell 5.1 ran `Start-Vora.ps1 -Mode review -NoPause` with the task: “Write one short sentence using only these facts: the coding class starts Monday at 4 PM.” Exit code was 0. The writer, structured critic and editor each completed on their first attempt, with three real local model calls taking 102.670 seconds in total. The critic returned `keep` with an empty issues array; the final preserved the writer's correct sentence: “The coding class begins on Monday at 4 PM.”

The console displayed DRAFT, CRITIC with decision/action, and FINAL DRAFT separately. Original result and trace files are saved locally as `native/outputs/20260908-225435-85aafb-result.json` and `native/outputs/20260908-225435-85aafb-trace.json`. This simple successful launch verifies integration, not a replacement for the six-case failures. The 1.7B server remains available; its owned stop script is `local-model-upgrade/stop-model.ps1`.

The read-only engineering review also verified the exact native stdout capture under Windows PowerShell 5.1.26100.9278 with explicit demo mode: status stderr did not abort the launcher. Do not merge stderr using an inner `2>&1` under `ErrorActionPreference = 'Stop'`, which reproduced a PowerShell `NativeCommandError` in that separate diagnostic.
