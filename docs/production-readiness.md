# Production readiness

Vora remains a development preview. This document defines the first production
target and the evidence needed to promote a specific commit and package. A green
test suite is necessary, but does not establish production readiness on its own.

## First deployment target

One trusted operator or internal team runs reviewed workflows under a dedicated
OS account, against a loopback model server. Filesystem tools read a deliberately
selected workspace, using explicit grants. State lives in private local storage.
An application or process supervisor owns scheduling, deadlines, retries across
process restarts and result consumption. Vora is a CLI engine, not a public API.

Public multi-tenant hosting, hostile workflows, autonomous external writes and
security-critical decisions are outside this target. They require authentication,
tenant isolation, sandboxing and application-level authorization. Model output
validation checks structure and specified text, not factual truth.

## Enforced runtime safeguards

- Source/input reads are bounded while reading, rather than after allocation.
- Source, prompt, output, calls and workers have [explicit limits](../SPEC.md).
- Model redirects and external endpoints are rejected. Proxy use is disabled.
- Malformed, deeply nested, oversized or truncated model responses fail closed.
- Local HTTP attempts have a configurable `--http-timeout-ms` limit. Retries and
  repairs share a persisted total-call budget when checkpoints are enabled.
- Results, traces and state use atomic file replacement. JSON persistence is
  capped at 8 MiB. Output paths and their sidecars cannot overwrite source/input.
- Cooperating writers use per-file locks. File and Unix directory flush errors
  fail the operation. Storage hardware still determines power-loss durability.
- Completed checkpoints restore validated ancestors without making new calls.
  An unfinished call can replay after a crash; there is no exactly-once promise.

## Operator runbook

1. Pin a tested release and verify its checksum. Pin the model file/version,
   inference server version and model configuration as well.
2. Run `vora check FILE` and inspect `vora plan FILE`. Grant only needed tools.
3. Use a private local directory for state/results. Do not put secrets into
   prompts unless you accept plaintext storage of them in outputs/checkpoints.
   Tool workspaces must not be writable by untrusted users during execution.
4. Set workers, max calls, HTTP timeout and an outer process deadline appropriate
   to the workload. Test peak concurrent runs against your actual model server.
5. Consume a result only after exit code zero. A failed run may retain a previous
   result. Use a distinct result path per job and correlate with its trace run ID.
6. On model/network failure, inspect the trace and resume the matching checkpoint
   only after resolving the fault. Resume retains consumed calls. Exhausted or
   incompatible checkpoints need a new job; never edit saved budgets.
7. On a stale `.lock`, first confirm its writer has exited, then remove the lock.
   On disk/flush failure, inspect the checkpoint before resuming. An error after
   rename can leave the new file in place with uncertain directory durability.
8. Back up private state according to your retention policy. Roll back by using
   the prior tested package and compatible state. This preview introduces an HTTP
   timeout identity field, so old preview checkpoints must start a new run.

Example from the repository root, with a running local server:

```sh
vora run native/quickstart-fast.vora --input "Draft a notice" --workers 2 --max-calls 6 --http-timeout-ms 30000 --checkpoint /private/jobs/job-001.json --trace /private/jobs/job-001-trace.json --output /private/jobs/job-001-result.json
```

The directory must exist and its permissions must be set by the operator.

## Stable release gates

| Gate | Required evidence |
| --- | --- |
| Portability | Native and CLI tests pass on the exact release commit on Windows, Linux and macOS. |
| Memory safety | ASan/UBSan tests pass on the exact commit; investigate all sanitizer reports. |
| Failure recovery | Model timeout, unavailable server, invalid responses, validation failure, writer exclusion, corrupt state and resume cases pass. |
| Resource bounds | Oversized inputs/outputs and prompt expansion fail without downstream calls; repeat parallel runs without state leakage. |
| Storage failure | Exercise denied writes, disk-full and forced interruption on supported local filesystems; document recovery and any ambiguous commit. |
| Sustained deployment load | Run representative workflows against the chosen model for an agreed duration and peak concurrency; record p95 latency, RSS, failure rate and recovery results. The 200 mock graph runs are regression coverage, not this gate. |
| Security review | Complete a native repository review with reproducible findings and verify all relevant fixes. Existing prototype reviews do not satisfy this gate. |
| Compatibility | Freeze grammar, CLI and state formats; document migration and rollback; verify them against prior supported releases. |
| Distribution | Publish immutable versioned packages from the gated commit, verify checksums and clean installs; keep moving development tags out of production pinning. |
| Operational acceptance | Operator accepts model-quality evaluation, monitoring, retention, backup and incident/recovery procedures for the actual workload. |

CI runs the portable native suite, optional Python protocol fixtures and a Linux
Clang ASan/UBSan build. Developer checks:

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
```

For sanitizer checks on Unix, use a separate Debug build directory with
`-DVORA_SANITIZERS=ON -DCMAKE_CXX_COMPILER=clang++`. Fixture tests use the exact
CMake-built executable through `VORA_TEST_EXE`. Python is test tooling only.

Release promotion must include a dated report stating which gates passed, the
tested commit, environment, workload and any remaining blockers. No stable
production release is declared by this document.

## Hardening review, 2026-10-03

The native security review of `da8c8c91aceb861546b9c0629e18d9f5767c1573`
covered 55 of 61 native tracked files. Vendored JSON implementation and third-party
licenses/historical evaluation records were excluded; external model-server and
system HTTP implementations were outside scope. One low-severity resource
exhaustion finding was validated: repeated placeholders allocated an oversized
prompt before checking its limit. The subsequent fix checks every append before
allocation, bounds interpolated tool fields to their schema limits, and applies
the same checks to memory and validation-repair prompts. A regression uses an
allocation guard so the old failure is detected without allocating a gigabyte.

This is a scoped review, not a guarantee for public multi-tenant deployment.
Exact-commit CI results and deployment evidence must accompany release promotion.
