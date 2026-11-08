# Human approval milestone verification — 2026-11-06

Status: implemented and validated locally on Windows x64/MSVC 19.42, C++17 Release.
This is native 0.5 development work, not a published or production-qualified release.
No GitHub push, release, package upload or remote configuration change was performed.

## Delivered behavior

The `approval NAME = SOURCE "INSTRUCTION"` gate pauses at a scheduler batch
boundary after atomically saving the exact candidate, request ID and pending state.
`vora approvals CHECKPOINT` inspects requests. `vora decide CHECKPOINT` records an
explicit approve/reject decision with operator label/reason; it never executes
providers. `run --resume` releases approved snapshots or rejects the job before
further calls. Gates use version-2 checkpoints; ordinary workflows retain version 1.
See [operator commands and trust boundary](human-approval.md).

## Evidence

- Final CTest: **13/13 groups passed**, 41.81 seconds. Covers parser, runtime,
  validation, state, tools, limits, graphs, approvals, and HTTP/state/tool/graph/
  approval CLI suites using the compiled development executable.
- The approval CLI group contains **15 process-level tests**: inspect/approve/
  resume, repeated pending resume, terminal reject, stale/duplicate/unknown IDs,
  malformed/forged/missing state, lock contention and failed decision writes,
  missing checkpoint/changed workflow, parallel gates, chained gates, reviewed
  source composition, process kill after approval, metadata limits, no phantom
  role memory, failed initial persistence, and output-validation enforcement.
  One malformed-state case uses a call count greater than 2^32; bounds are checked
  before narrowing to a native integer.
- Native approval tests cover syntax, cycles/undefined sources, literal instructions,
  zero-call plans/release, durable-callback requirement, pause events, identity
  changes, malformed state and programmatically malformed gate dependencies.
- Actual process-kill test waits until the downstream editor has begun, kills only
  its test process, confirms exit, removes only its owned empty temporary lock and
  resumes. The saved approval stays valid, completed writer is not called again,
  and the interrupted editor's reservation remains charged.
- Existing archived-state and bounded-review regressions passed in the full suite.
- CPack produced **vora-0.5.0-dev.2-win64.zip**. Fresh extraction passed CLI/version,
  graph examples/docs, tool grants, ordinary checkpoint roundtrip and the packaged
  human-approval pause → inspect → decide → resume sequence.
- Mermaid plan displays an explicit human-approval diamond between writer/editor.
- `git diff --check` passed.

## Real local model smoke test

Started the existing pinned llama.cpp/Qwen3-0.6B-Q8_0 loopback server, using alias
`local`, 128 output tokens and max-calls 6. The task requested one short sentence
stating that Vora workflows require human review before use.

1. Writer produced `Vora workflows require human review before use.` The run
   exited 2 with one reserved call and a pending request; the editor had not run.
2. The test harness recorded `approve` under **codex-test-operator**, with reason
   explicitly identifying automated acceptance testing. This was not an approval
   by the user or evidence of authenticated human identity.
3. Explicit resume completed with two total calls. The model returned
   `Approved draft: Vora workflows require human review before use.`
4. Completed resume made **zero new provider calls** and preserved the result.

These are functional smoke checks, not a sustained-load benchmark, model-quality
evaluation or proof of correctness of future human/model decisions. Private run
files are under `%LOCALAPPDATA%/Vora/approval-validation-20261106` and are not committed.

## Local artifacts and reproduction

Development executable: `%LOCALAPPDATA%/Vora/build-0.5/bin/vora.exe`

SHA256: `6af1ca03b5c2de9346dda33eaae541a2643d2077cf28fb48639769adb79fb65b`.

Archive: `%LOCALAPPDATA%/Vora/build-0.5/packages/vora-0.5.0-dev.2-win64.zip`

SHA256: `573df1ac996acce57e0146283f5b60bc5ede3bc4daa1758ec78ba7ca488940b0`.

```powershell
cmake -S native -B "$env:LOCALAPPDATA/Vora/build-0.5" -A x64 -DVORA_RUNTIME_OUTPUT_DIRECTORY="$env:LOCALAPPDATA/Vora/build-0.5/bin"
cmake --build "$env:LOCALAPPDATA/Vora/build-0.5" --config Release --parallel 2
ctest --test-dir "$env:LOCALAPPDATA/Vora/build-0.5" -C Release --output-on-failure
cpack --config "$env:LOCALAPPDATA/Vora/build-0.5/CPackConfig.cmake" -C Release -B "$env:LOCALAPPDATA/Vora/build-0.5/packages"
python native/tests/verify_package.py "$env:LOCALAPPDATA/Vora/build-0.5/packages/vora-0.5.0-dev.2-win64.zip"
```

## Limits and promotion gates

The checkpoint is trusted plaintext local state, not a signed security boundary.
Operator labels are self-reported. Local file writers can rewrite state; the CLI
does not prove someone actually read it. Scope excludes remote reviewers,
authentication/RBAC, expiry, signatures, live inboxes and exactly-once side effects.

At the time of this initial report, Linux/macOS, sanitizers and long-running
deployment tests had not run and no independent security review was complete.
See the [November follow-up](local-validation-2026-11.md) for subsequent Linux
and scoped independent agent review evidence. Remaining checks,
plus exact-candidate public CI/package verification, remain promotion gates.
The earlier 0.4 CI and soak results are not validation of this new feature.
