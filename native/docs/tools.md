# Controlled tools in Vora 0.4

Tool declarations and calls are part of the native language. A host grants named
builtins explicitly; the model cannot add tools or run arbitrary commands.

```text
workflow WorkspaceAudit(input):
    tool reader = "read_file"
    tool stats = "text_stats"
    agent reviewer = "Review the supplied document. Cite only supplied evidence."
    document = reader({"path": "{input}"})
    counts = stats({"text": "{document}"})
    report = reviewer("Document: {document}\nCounts: {counts}")
    return report
```

From the repository root on Windows:

```powershell
native/dist/vora.exe run native/examples/workspace-audit.vora --input README.md --workspace . --allow-tools read_file,text_stats --memory native/outputs/audit-memory.json --checkpoint native/outputs/audit-checkpoint.json
```

Start the local model first; memory/checkpoint parent folders must exist. For a
fully deterministic run with no model, use `native/examples/file-stats.vora`.
On Linux/macOS use `native/dist/vora` after building. `--provider demo` substitutes
only the model calls; tool operations still read real files and calculate results.

## Builtin contracts

| Tool | Required arguments | Optional arguments | Result |
| --- | --- | --- | --- |
| read_file | path: string | none | UTF-8 text, including empty files |
| list_files | none | path: string, default . | JSON entries and truncated flag |
| search_file | path: string, query: string | max_matches: integer 1..100, default 20 | JSON matching line numbers/text and truncated flag |
| text_stats | text: string | none | JSON bytes, whitespace-delimited words, and lines |
| json_select | text: JSON string, pointer: JSON Pointer string | none | selected JSON value serialized as JSON |

Each declaration binds a workflow alias to a builtin. Aliases share the agent and
step namespace. Calls accept a flat JSON object. Unknown arguments, wrong types,
duplicate keys, unsupported builtins and repair rules on tool steps are rejected.
The older quoted JSON-object string form also works. Placeholder dependencies are
resolved before each tool call; substitution affects string values and cannot
create new JSON keys or syntax. Numeric arguments cannot use placeholders.

Text arguments are limited to 256 KiB; path/query/pointer arguments to 4096 bytes.
Files must be regular UTF-8 text, contain no NUL bytes, and stay within 256 KiB.
Search is a literal case-sensitive substring match, not a regular expression.
Search returns at most 100 matching lines with 1024-byte UTF-8-preserving snippets.
Directory listing is non-recursive and capped at 100 entries; order follows the
filesystem. Text line counts equal newline count plus one for nonempty text,
including the final empty line after a trailing newline. JSON selection allows
at most 64 nesting levels. Missing pointers and malformed JSON fail the step.

## Capabilities and execution

`--allow-tools` is a comma-separated builtin allowlist. Filesystem tools also
require an explicit `--workspace DIR`. Paths are relative to that canonical root,
use forward slashes, and cannot contain parent traversal, absolute paths, NUL,
backslashes, or colons/alternate data streams. Symlink path components are denied;
canonical containment is rechecked. Listing a symlink reports its type and does
not follow it. Root access grants read access to every eligible file below it;
choose a dedicated folder when the project includes private data.

This is a capability boundary for cooperating processes, not an OS sandbox.
Filesystem checks and subsequent opens are not race-free against a hostile
process replacing directories or links concurrently. There are no shell/process,
network-fetch, write/delete, or dynamically model-selected tools. Tool code and
local workspace state remain trusted. Reading an untrusted document can still
influence a later model prompt; labeling data does not prevent prompt injection.

Tools run as graph steps. Independent tool/model steps may execute concurrently.
Every tool attempt consumes the same max-calls budget as model attempts and is
reserved in the checkpoint before execution. A tool failure stops dependent
steps; automatic tool retries and repairs are unsupported. Deterministic output
contracts may validate tool results. Critique contracts are structural checks,
not an additional model call.

Traces include tool_started, tool_completed and tool_failed with step/builtin
names, but omit paths, arguments and content. Results and checkpoint outputs do
contain data. Restored tool steps use their saved output snapshot even when a
file changed or disappeared; begin a fresh run to read current contents. Checkpoint
identity includes tool declarations, step kinds, workspace root, grants and tool
API version. Agent memory ignores tool-step outputs. This release's new identity
shape rejects earlier development checkpoints; no released checkpoint migration
is promised yet.

## Verification

```sh
ctest --test-dir native/build -C Release --output-on-failure
python native/tests/test_tools_cli.py
python native/tests/test_http_cli.py
python native/tests/test_state_cli.py
```

Tests cover grants, path containment, file-size limits, tool-to-model dependencies,
JSON substitution, bounded results, resume and shared budgets. Symlink fixtures
are skipped where the OS does not permit their creation.
