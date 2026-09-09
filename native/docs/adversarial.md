# Native runtime adversarial validation

This review concerns the native C++ runtime, independently of the Python prototype. The native tests use throwing checks so Release builds with `NDEBUG` retain every assertion.

## Test scope

`native/tests/runtime_tests.cpp` contains 13 behavioral tests covering declared data flow, nonrecursive interpolation, concurrent branch overlap, join ordering, transient retries, retry exhaustion, shared parallel call limits, permanent and non-standard exceptions, failed dependencies, empty output, content-free traces and preflight rejection of invalid limits. Parallelism checks use a condition variable with a two-second timeout rather than an unbounded barrier.

The coordinator rebuilt the native executable and reports both CTest targets passing: parser tests and the final 13-case runtime suite. The coordinator additionally reports all seven tests in `native/tests/test_http_cli.py` passing against actual WinHTTP fixtures, covering request/input-file handling, incomplete/truncated/tool/malformed responses, redirects, HTTP retry classification, endpoint restrictions and demo isolation.

This reviewer independently executed `native/dist/vora.exe --help`: the native CLI returned its help text with exit code 0. No model was invoked by that smoke check. The coordinator reports successful real llama.cpp/Qwen model-server verification; the full three-step native workflow was still running when this report was finalized, so no end-to-end real-workflow completion is claimed here.

## Findings and resolution

1. **Resolved:** The first version of `has_text()` treated form-feed and vertical-tab-only output as valid content. The implementation now uses `isspace`; the final runtime suite includes both characters and passed in the coordinator's native build.
2. **Resolved:** The first OpenAI-compatible response reader ignored `choices[0].finish_reason`, allowing incomplete or truncated results to look complete. It now requires `finish_reason` to equal `stop`. The coordinator's HTTP tests cover incomplete and length-truncated responses and passed.

The runtime has a catch-all around provider invocation, and the final suite verifies that a non-standard C++ exception does not leak content, retry, or execute its dependent step.

## Scope limits

The injected runtime-test providers are deterministic C++ functions. These tests do not establish real-model answer quality, remote research quality, provider billing, durable recovery or a public deployment. The runtime schedules ready steps in bounded batches and waits for in-flight siblings; it has no workflow-wide deadline or force cancellation. A thread cannot safely force-stop arbitrary provider code. Returned results deliberately contain content, while normal event logs exclude the tested prompt, role, input, output and exception-message content. Workflow and step identifiers remain visible metadata and should not contain secrets.

External side effects, exactly-once guarantees, workflow wall-clock deadlines and monetary budgets require additional mechanisms and are not established by a native executable alone.

On the reported native test evidence, this is suitable for an explicitly experimental local developer preview. There are no unresolved blocking findings from this bounded review; this is not a production security certification or proof of commercial demand.
