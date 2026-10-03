# Native 0.4 compatibility contract

This contract applies to native engine 0.4.x, not the historical Python prototype.
The reviewed grammar is SPEC.md; the public CLI is the documented check, plan and
run interface. Patch releases preserve valid reviewed 0.4.0 workflows, flags,
JSON result fields and version-1 memory/checkpoint semantics. Security fixes may
reject unsafe inputs previously accepted; release notes must identify these.
New minor versions may break compatibility only with migration/rollback notes.
Trace event ordering across independent workers is not a stable ordering promise.

## State and upgrades

Memory format vora-memory version 1 is supported. Checkpoint format
vora-checkpoint version 1 requires the full configuration identity, including
http_timeout_ms and tool_api. Resume validates workflow, input, provider, model,
generation configuration, budgets, tool grants/workspace and memory mode.
Changing any of these requires a new job. Do not rewrite identity to bypass checks.
Unsupported versions are rejected; no automatic lossy conversion is provided.
The archived checkpoint regression relocates only a test workspace placeholder;
this is fixture setup, not a production state migration procedure.

Earlier 0.4.0-dev.1 checkpoints without http_timeout_ms are intentionally
incompatible. Keep their backup, finish them with their original pinned binary,
or start a new job with the new binary. Their completed responses can be retained
as ordinary job records; do not copy calls/results into a new checkpoint.
Role-scoped version-1 memory is retained if its schema and roles remain valid.
Back up memory and checkpoints before upgrading; new jobs use distinct paths.

## Rollback

Keep the old binary/package, model configuration and untouched state backups.
Stop the job supervisor and confirm active writers exited. Switch the pinned
binary to the previous version and restore only its own compatible state copy.
Do not overwrite an in-flight writer or edit a persisted call budget. Check the
workflow with the selected version, then resume only a matching checkpoint.
Atomic write does not imply exactly-once model calls or power-loss guarantees.
