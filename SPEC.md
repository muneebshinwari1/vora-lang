# Vora Language Specification 0.5 (local development)

Vora is a small declarative language for bounded agent workflows. A program names
agents, connects them as an acyclic graph, and declares output checks. The native
implementation is the reference implementation.

## Source format

- Source is UTF-8 text.
- Blank lines and lines whose first non-space character is `#` are ignored.
- Keywords are case-sensitive. One statement occupies one line.
- Names use letters, digits, or `_` and must begin with a letter or `_`.
- Quoted values use double quotes. `\\`, `\"`, `\n`, `\r`, and `\t` are escapes.

```ebnf
program      = header, newline, { statement, newline } ;
header       = "workflow", name, "(", name, "):" ;
statement    = agent | tool | step | conditional | review | approval | returns | validate | require | forbid | repair ;
agent        = "agent", name, "=", string ;
tool         = "tool", name, "=", string ;
step         = name, "=", name, "(", ( string | json_object ), ")" ;
returns      = "return", name ;
validate     = "validate", name, "as", "critique" ;
require      = "require", name, ( "sentences", integer | "contains", string ) ;
forbid       = "forbid", name, "contains", string ;
repair       = "repair", name, "max", integer ;
conditional  = name, "=", name, "(", string, ")", "when", name,
               "verdict", ( '"keep"' | '"revise"' ), "else", name ;
review       = "review", name, "with", name, "max", integer ;
approval     = "approval", name, "=", name, string ;
```

Step prompt placeholders such as `{input}` or `{draft}` reference the workflow
input or another step's output. They define graph dependencies. Execution limits
are CLI/runtime options, not language statements.


## Static rules

A program has exactly one workflow declaration, at least one executable step, and exactly
one return target. Names are unique and every referenced agent/tool and placeholder exists. The step
graph is acyclic and the return target is a defined step. Agents are optional
for tool-only workflows; unused declarations are permitted. Sentence counts are 1..100 and repair counts are 1..5. Invalid programs fail before a provider is called.

## Execution

Steps execute in topological order; retries and repairs can add attempts. Each prompt substitutes its declared input/output placeholders. Independent steps
can run concurrently within the configured worker limit. The provider receives the agent role and assembled prompt. Transient
provider failures may be retried within `--retries`; all attempts consume
the shared call budget. HTTP adapters apply transport timeouts; the runtime
does not guarantee forced cancellation or a hard workflow deadline.

`validate NAME as critique` structurally checks the existing output of step `NAME`
as critique JSON; it does not create another provider call. The local HTTP
provider requests the critique schema for that step. The object has exactly
`verdict`, `issues`, and `instruction`; `keep` requires no issues, `revise` requires
at least one nonempty issue, and the instruction is nonempty. This does not
validate factual accuracy.
Deterministic rules are evaluated locally: `sentences N` requires exactly N
sentences, `contains` requires an exact case-sensitive substring, and `forbid
contains` rejects that substring. If an agent output check fails and `repair NAME
max N` exists, the provider receives a bounded repair request and the rules are
checked again. A failed check after the repair budget is a workflow error.

The result is the returned step's final text. A trace records agent calls,
retries, validation events, repair calls, tool events, and their ordering.

## Native graph extensions (0.5 development)

A conditional call requires an agent, a critique-validated condition step and a
fallback step. Condition/fallback and prompt references define ordinary acyclic
dependencies. If the verdict matches, call the agent; otherwise return the exact
fallback with zero additional calls. Both paths undergo declared output checks.
Upstream dependencies execute normally; no arbitrary expressions are evaluated.

`review STEP with CRITIC max N` requires an unconditional agent step, a declared
critic agent, one review rule per target and N=1..10. Each round runs the writer
then a schema-validated reviewer. Keep releases the draft; revise supplies the
previous draft/critique to the original writer prompt for the next round. Exhaustion
fails closed. Calls, retries and repairs share the global budget. Completed stages
are durable under the CLI graph checkpoint contract; unfinished calls can repeat
with previously reserved budget retained. See [full graph semantics and recovery](docs/agent-graphs.md).
These are native-only development extensions; the stable 0.4 grammar excludes them.

An approval node names a source step and literal operator instruction. It costs
zero calls and returns the exact saved source only after explicit local approval.
Ready pending gates pause the whole scheduler at a batch boundary; rejection is
terminal for the job. Gates participate in cycle/name validation and may have
output checks, but cannot repair or review. The CLI requires durable version-2
state for approval workflows; ordinary workflows retain version 1. Gate decisions
are operator state, never model outputs. See [approval semantics](docs/human-approval.md).

## Providers and portability

The parser, planner, runtime, deterministic validation, and demo provider require
C++17; Linux/macOS HTTP support additionally requires libcurl. They are tested by CI on Windows, Linux, and macOS. The real local-model HTTP provider uses WinHTTP on Windows and libcurl on Linux/macOS.
Provider adapters are implementation extensions and do not change language
semantics.

## Compatibility

The language version is `major.minor`. Implementations may add providers and
diagnostics in a patch release. New syntax or changed execution semantics require
a minor or major version change. Unknown statements must be rejected.

## Tool and persistence extensions (0.4)

Tool aliases select an implementation builtin. Their calls accept flat JSON
objects with builtin-specific argument schemas, unique keys and string/numeric
values. Agent calls continue to accept quoted prompt strings. Tool string-value
placeholders declare dependencies; substitution is serialized as data. Tool steps
require explicit host capabilities, share execution budgets and cannot repair
failed outputs automatically. See [tool contracts](native/docs/tools.md).

CLI opt-in agent memory supplies bounded prior successful context. Checkpoints
restore validated outputs and consumed calls, bind workflow/input/provider/tool
configuration, and may replay an interrupted unfinished call. These features
extend runtime behavior rather than add persistence statements to source syntax.

## Native resource limits (0.4 development line)

Workflow source and run input are limited to 1 MiB each. A source line is at most
8192 bytes, and a workflow has at most 1024 non-comment statements including its
header. The embedded runtime accepts at most 1024 steps, 1..64 workers, 0..100
transient retries and 1..10000 total calls. A step output and expanded prompt are
at most 4 MiB; the HTTP adapter separately caps the serialized request/response
at 4 MiB. State and result/trace JSON are capped at 8 MiB, with state and HTTP
JSON limited to nesting depth 64. These are separate limits: a workflow that
passes parsing can still exceed an execution or persistence limit.
