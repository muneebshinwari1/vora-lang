# Native 0.5 development: reliable agent graphs

This is a local development milestone. Published packages and the supported stable
release remain 0.4.0. These extensions apply to the native engine; the historical
Python prototype does not implement them. Use separate state paths for development.

## Independent agents and joins

Placeholders define dependencies. Two steps referencing only the input run in the
same ready batch with `--workers 2` or more; a joining step waits for both outputs.
See [parallel research](../native/examples/parallel-research.vora). Existing 0.4
parallel scheduling is retained, including its batch barrier. The runtime does
not execute unbounded threads or promise completion-order scheduling. The model
server needs enough capacity for concurrent calls.

## Conditional agent calls

```text
workflow ConditionalEdit(input):
    agent writer = "Write a brief notice."
    agent critic = "Review the notice."
    agent editor = "Correct the notice using the critique."
    draft = writer("{input}")
    critique = critic("Task: {input}\nDraft: {draft}")
    validate critique as critique
    final = editor("{draft}\n{critique}") when critique verdict "revise" else draft
    return final
```

`when TARGET verdict "keep"|"revise" else FALLBACK` applies to an agent call.
TARGET must have `validate TARGET as critique`; FALLBACK must be a step. Both are
dependencies, together with all prompt references. The agent is called only when
the verdict matches. Otherwise its output is the exact fallback text and the
step consumes zero additional calls. Output validations apply to either result.
The fallback producer and condition producer are ordinary graph nodes and run
normally; this syntax does not skip an entire upstream subgraph. No string is
evaluated as code. Invalid or contradictory critique JSON fails closed.

## Bounded writer–critic loops

```text
workflow ReviewedNotice(input):
    agent writer = "Write a brief notice and apply any supplied critique."
    agent critic = "Check the notice against the task."
    final = writer("Task: {input}")
    review final with critic max 3
    return final
```

`review STEP with CRITIC max N` attaches a loop to one unconditional agent step.
N is 1..10; CRITIC is a declared agent. A round consists of a writer call followed
by a structurally validated critic call. `keep` releases the draft to dependent
steps. `revise` adds the previous draft and critique to the original writer
request for the next round. Rejection at the maximum round count is an execution
failure, never an implicitly accepted answer. Feedback, role memory and repair
requests use the existing pre-allocation prompt bounds. Writer validations are
checked on every draft. Automatic deterministic repair is separate from review;
each repair, retry and reviewer call consumes the shared `--max-calls` budget.

The native local HTTP provider requests the critique schema for internal reviewer
calls. Reviewer stage names in traces use `STEP.review`. The demo provider emits
plain illustrative text and cannot satisfy critique validation; use a real local
model or protocol fixtures for these examples. A critic verdict is model judgement,
not proof of factual correctness or permission for side effects.

## Checkpoints and recovery

The CLI saves completed drafts and critiques as `graph_state` in its version-1
checkpoint envelope, together with ordinary outputs and reserved call counts.
Review progress records the latest round, phase (`critique`, `draft`, `accepted`),
draft and critique. The full workflow identity includes routing and review limits.
Changing a branch, critic, limit, budget or other identity setting requires a new job.

After a completed writer stage, resume calls only the unfinished critic. After a
completed revise verdict, resume starts the next writer round. After acceptance,
resume returns the saved draft without repeating either call. An exhausted loop
stays exhausted. A provider invocation interrupted before its response is saved
can repeat; its previous reserved call is retained. These are at-least-once
unfinished-call semantics, not exactly-once external effects. Concurrent independent
branches serialize checkpoint mutations under the call/state mutex.

Missing, malformed, orphaned or inconsistent stage state is rejected before new
calls. Plain 0.4 workflows keep their original identity and archived version-1
fixtures remain testable. 0.4 cannot read workflows using the new syntax; retain
the development binary and state backups for rollback. State is private plaintext,
limited to 8 MiB including all stages/outputs; large graphs can fail this bound.
Keep mutable state on private local storage outside cloud-sync directories.

C++ embedding callers requiring durable review recovery must supply
`RunOptions.graph_checkpoint(outputs, calls, graph_state)` and restore
`resume_graph`, `resume_outputs` and `resume_calls`. This callback supersedes the
older two-argument checkpoint callback. The old callback alone saves ordinary
outputs/call reservations and cannot preserve intermediate review stages.

## Run locally

Build the development source using the normal CMake instructions. With its native
binary (on Windows, use `native/dist/vora.exe` after a default build):

On Windows, `native/Run-Graph-Demo.ps1` runs a parallel model-free example. Add
`-RealModel` for a bounded review with private state under `%LOCALAPPDATA%/Vora`.
Pass `-Executable PATH` when using a separate development build directory.

```sh
vora plan native/examples/parallel-research.vora
vora run native/examples/parallel-research.vora --provider demo --input "Plan a small documentation project" --workers 2
vora run native/examples/bounded-review.vora --input "Write one short sentence saying Vora workflows require review before use." --max-tokens 256 --max-calls 10 --checkpoint /PRIVATE_LOCAL_DIR/review.json --trace /PRIVATE_LOCAL_DIR/review-trace.json
vora run native/examples/bounded-review.vora --input "Write one short sentence saying Vora workflows require review before use." --max-tokens 256 --max-calls 10 --resume /PRIVATE_LOCAL_DIR/review.json
```

Replace `/PRIVATE_LOCAL_DIR` with an existing private directory. The loopback model
must already be running and available under the configured model alias. Run
`ctest --test-dir BUILD -C Release --output-on-failure` for graph/runtime/protocol
and archived-state regressions. `plan` reports a conservative minimum call count:
ordinary unconditional steps cost one, new review steps cost at least two, and
conditional calls cost zero or one. Repairs/retries/additional rounds increase it.

`route_selected`, `step_skipped`, `review_round`, `review_verdict`, `review_accepted`
and `review_exhausted` augment existing timed call events. Prompts/responses stay
out of trace events, but result/checkpoint files contain plaintext model data.
HTTP attempt timeouts still apply. There is no hard whole-workflow deadline or
forced cancellation of arbitrary embedded providers.

## Further milestones

### Offline trace inspector (local development)

Run `python scripts/trace_viewer.py TRACE.json VIEW.html`, then open VIEW.html in
a browser. Python is export tooling only. The generated file is self-contained,
makes no network requests and supports event/search/failure filters. It displays
up to 1000 matching rows at a time; narrow filters for large traces. Inputs are
limited to 8 MiB/50000 events. Output creation refuses existing files. Event data
is escaped and rendered as text, including provider-derived strings. The viewer
does not execute workflows or supply approvals. Keep exported metadata private.

[Human approval gates](human-approval.md) now provide durable pending/approved/rejected
states and operator CLI decisions. They are separate from critic verdicts.
An authenticated remote review service and live trace viewer remain future work.
Before public 0.5 promotion, validate the exact candidate on Linux/macOS and under
sanitizers, run an independent review and longer real-model deployment tests,
and update distribution URLs/hashes only after publishing immutable tested assets.
This local milestone does not automatically schedule or authorize a GitHub push.
