# Durable human approval — native 0.5 development

An approval gate pauses execution for a local operator to review a saved step
output. It is independent of model verdicts. This feature is local development;
published 0.4 packages do not implement it.

```text
workflow HumanApproval(input):
    agent writer = "Write one short notice."
    agent editor = "Polish the approved notice without adding facts."
    draft = writer("{input}")
    approval approved = draft "Review the draft before editing."
    final = editor("{approved}")
    return final
```

`approval NAME = SOURCE "INSTRUCTION"` declares a zero-call gate. SOURCE must be
a defined step; its exact text becomes the approved output. INSTRUCTION is literal,
including braces. The source can be an agent, tool, completed review loop or prior
gate. Names/dependencies must remain unique and acyclic. Approval gates can have
output validations but cannot have automatic repair or writer–critic review rules.

## Operator workflow

Use the development executable, not the installed stable version. Choose an
existing private local directory for mutable state, outside OneDrive/cloud sync.
For a model-free walkthrough add `--provider demo` consistently to run/resume.

```sh
vora run native/examples/human-approval.vora --provider demo --input "Write a welcome notice" --checkpoint /PRIVATE_DIR/job.json --trace /PRIVATE_DIR/pause.json
vora approvals /PRIVATE_DIR/job.json
vora decide /PRIVATE_DIR/job.json --step approved --request REQUEST_ID --decision approve --by "local-operator" --reason "Reviewed exact draft"
vora run native/examples/human-approval.vora --provider demo --input "Write a welcome notice" --resume /PRIVATE_DIR/job.json --trace /PRIVATE_DIR/resume.json
```

The first command exits **2** after durably saving pending approval state.
`approvals` prints JSON containing the instruction, exact candidate text, request
ID, source, status and decision metadata. Review this output; copy the specific
request ID into `decide`. For rejection use `--decision reject` with a reason.
Successful inspection/decision commands exit 0. Decisions never run providers or
tools: explicit `run --resume` is a separate operation with matching job settings.
Success exits 0; invalid state, rejected jobs and execution errors exit 1.

No final result or role memory is committed while the run is paused. Trace output
is per invocation; use distinct filenames to retain pause/resume histories.
Existing result files are not deleted on pause, so use distinct job output paths.

## Execution and durability

The scheduler processes ready approval gates at batch boundaries, with no worker
still running. It saves all newly ready requests, then pauses the entire workflow
if any is pending. Previously running independent steps may have completed; no
new batch starts once a ready pending gate is observed. Independent nodes whose
dependencies do not pass through a gate are not retroactively approval-protected.
To gate an operation, make it depend on the approved output rather than the raw
source output. A gate also protects the workflow's return if it is the return node.

Each request has a 32-character random hexadecimal ID and a snapshot of the exact
source output. Resume/decision validation checks the snapshot against the saved
source. Repeated pending resumes preserve ID and call budget. An approved gate
returns its snapshot without a model call. Multiple parallel gates require all
ready decisions; chained gates pause independently. A rejection terminates the
job on resume before any provider/tool call, including independent work.

Decision commands take the same exclusive state lock as execution, re-read and
validate the record while holding it, and use the existing atomic write/flush
path. Stale IDs, unknown gates, duplicate decisions and changes to final decisions
are rejected without overwriting state. `--by` is 1..128 bytes and `--reason` is
1..2048 bytes; both reject ASCII control characters. Request/decision timestamps are
local wall-clock epoch milliseconds, recorded as decimal strings. Clock skew is
possible; these timestamps are not a trusted time authority.

If a process is interrupted after approval, the decision stays saved. Already
completed steps restore without calls; an unfinished provider call may repeat
while its previous reservation stays consumed. Approval does not supply
exactly-once external effects. After a crash, verify the writer is no longer
running before removing only its stale empty `.lock` directory. Never remove
locks owned by active processes. Disk-full/lock/write failures fail closed.

## State format and embedding

Workflows with approval gates use `vora-checkpoint` **version 2** with an
`approvals` object. Ordinary workflows retain version 1 and prior identity
semantics. Missing/inconsistent approvals, an output without a matching approved
record, or changed workflow/input/provider/budget identity are rejected.
Gate source and instruction are included in workflow identity. The 8 MiB total
checkpoint limit remains; snapshots consume space in addition to normal outputs.
Version 2 is a development contract and is not readable by stable 0.4. Preserve
the matching binary, workflow and state backup; start a new job to reconsider a
rejected decision. Do not edit checkpoint identities or approval fields manually.

C++ callers must provide `RunOptions.durable_checkpoint(outputs, calls,
graph_state, approvals)`, restore `resume_approvals` along with graph/output/call
state, and handle `WorkflowPaused` separately from `ExecutionError`. The four-argument
callback supersedes older callbacks. Missing durable storage is rejected before
any calls. Callback implementations must atomically save all supplied fields.

Trace events `approval_requested`, `approval_released`, `approval_rejected` and
`workflow_paused` contain step/request identifiers and timing, not candidate text
or operator reasons. Full decision metadata remains in the checkpoint. The offline
trace inspector can search these events; it does not grant decisions.

## Trust boundary and remaining work

This is a workflow coordination mechanism for a trusted local operator. `--by`
is an operator-supplied audit label, not verified identity. Anyone who can modify
the checkpoint or run the decision command has that operator's authority. Records
are plaintext and unsigned; they do not prevent a malicious local user from
rewriting both source and snapshot. Use filesystem permissions to protect them.
The CLI cannot prove a human actually read the candidate. It introduces no network
service, model-controlled approval tool, shell tool, or external publishing action.

Authenticated remote reviewers, cryptographic signatures, role-based permissions,
expiry/delegation, rejection-to-revision branches and a live approval inbox are
future work. Cross-platform and deployment validation must precede public promotion.
