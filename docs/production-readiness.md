# Production readiness

Native 0.4.0 is governed by the trusted local deployment contract below and the
[patch compatibility policy](compatibility.md). Promotion requires the recorded
checks below for a specific commit and package. A green test suite alone does
not establish readiness for a new deployment or model.

## First deployment target

One trusted operator or internal team runs reviewed workflows under a dedicated
OS account, against a loopback model server. Filesystem tools read a deliberately
selected workspace, using explicit grants. State lives in private local storage outside cloud-sync folders such as OneDrive.
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
tested commit, environment, workload and any remaining blockers. Release notes bind those results to an immutable release commit.

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

## Tested hardware profile

The release soak exercises a local Qwen3-0.6B-Q8_0 model, llama.cpp with four
CPU threads, and writer/critic/editor CLI jobs. Production admission on this
hardware should allow one active job and use the 120000ms HTTP attempt timeout.
A two-job/30000ms trial hit timeouts and is not an accepted load profile.
Set workers to 1 for independent DAG branches until the chosen model/server has
been benchmarked. Different hardware, models and workflows require their own
load and quality acceptance; this release does not promise model accuracy.

Reproduce the optional developer soak (Python and psutil are test tooling):

```sh
python native/tests/soak.py --exe native/dist/vora --workflow native/quickstart-fast.vora --output-dir native/outputs/unique-soak-run --seconds 900 --concurrency 1 --http-timeout-ms 120000
```

On Windows use native/dist/vora.exe. The output directory must be new. Metrics
include per-job status/calls, nearest-rank p95 latency, sampled engine RSS and
model-server RSS endpoints. Retain logs and state for failed runs.

Windows persistence writes and flushes one handle, then retries only transient
sharing/lock/access-denied failures when replacing the target (maximum 500ms of retry sleeps).
A longer lock fails closed and preserves the prior checkpoint. After other
storage errors, inspect the checkpoint before resuming; a post-rename error can
leave a new file with uncertain directory durability.
Checkpoint errors identify the storage phase and Windows error code without
including prompt contents. Cloud-sync folders are outside the tested storage
contract; copy completed artifacts there only after successful job completion.
