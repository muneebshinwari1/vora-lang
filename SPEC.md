# Vora Language Specification 0.3

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
statement    = agent | step | returns | validate | require | forbid | repair ;
agent        = "agent", name, "=", string ;
step         = name, "=", name, "(", string, ")" ;
returns      = "return", name ;
validate     = "validate", name, "as", "critique" ;
require      = "require", name, ( "sentences", integer | "contains", string ) ;
forbid       = "forbid", name, "contains", string ;
repair       = "repair", name, "max", integer ;
```

Step prompt placeholders such as `{input}` or `{draft}` reference the workflow
input or another step's output. They define graph dependencies. Execution limits
are CLI/runtime options, not language statements.


## Static rules

A program has exactly one workflow declaration, at least one agent, and exactly
one return target. Names are unique and every referenced agent and placeholder exists. The flow
graph is acyclic, every agent is reachable from a root, and the returned agent is
reachable. Sentence counts are 1..100 and repair counts are 1..5. Invalid programs fail before a provider is called.

## Execution

Agents execute once in topological order. Each prompt substitutes its declared input/output placeholders. Independent steps
can run concurrently within the configured worker limit. The provider receives the agent role and assembled prompt. Transient
provider failures may be retried within `--retries`; all attempts count
toward the step budget and share the call budget. HTTP adapters apply transport timeouts; the runtime
does not guarantee forced cancellation or a hard workflow deadline.

`validate NAME as critique` asks the provider for a critique after `NAME` runs.
Deterministic rules are evaluated locally: `sentences N` requires exactly N
sentences, `contains` requires an exact case-sensitive substring, and `forbid
contains` rejects that substring. If a deterministic rule fails and `repair NAME
max N` exists, the provider receives a bounded repair request and the rules are
checked again. A failed check after the repair budget is a workflow error.

The result is the returned agent's final text. A trace records agent calls,
retries, validation calls, repair calls, and their ordering.

## Providers and portability

The parser, planner, runtime, deterministic validation, and demo provider require
C++17; Linux/macOS HTTP support additionally requires libcurl. They are tested by CI on Windows, Linux, and macOS. The real local-model HTTP provider uses WinHTTP on Windows and libcurl on Linux/macOS.
Provider adapters are implementation extensions and do not change language
semantics.

## Compatibility

The language version is `major.minor`. Implementations may add providers and
diagnostics in a patch release. New syntax or changed execution semantics require
a minor or major version change. Unknown statements must be rejected.
