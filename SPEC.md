# Vora Language Specification 0.3

Vora is a small declarative language for bounded agent workflows. A program names
agents, connects them as an acyclic graph, and declares output checks. The native
implementation is the reference implementation.

## Source format

- Source is UTF-8 text.
- Blank lines and lines whose first non-space character is `#` are ignored.
- Keywords are case-sensitive. One statement occupies one line.
- Names use letters, digits, `_`, or `-` and must begin with a letter or `_`.
- Quoted values use double quotes. `\\`, `\"`, `\n`, `\r`, and `\t` are escapes.

```ebnf
program      = { statement, newline } ;
statement    = workflow | agent | flow | returns | validate
             | require | forbid | repair | limits ;
workflow     = "workflow", name ;
agent        = "agent", name, "role", string ;
flow         = "flow", name, "->", name ;
returns      = "return", name ;
validate     = "validate", name, "as", "critique" ;
require      = "require", name, ( "sentences", integer
             | "contains", string ) ;
forbid       = "forbid", name, "contains", string ;
repair       = "repair", name, "max", integer ;
limits       = "limits", "steps", integer, "retries", integer,
               "timeout", integer, "seconds" ;
```

## Static rules

A program has exactly one workflow declaration, at least one agent, and exactly
one return target. Names are unique and every referenced agent exists. The flow
graph is acyclic, every agent is reachable from a root, and the returned agent is
reachable. Limits and repair counts are positive and bounded by the reference
implementation. Invalid programs fail before a provider is called.

## Execution

Agents execute once in topological order. A root receives the workflow input.
Other agents receive the completed outputs of their direct predecessors in source
flow order. The provider receives the agent role and assembled prompt. Transient
provider failures may be retried within `limits retries`; all attempts count
toward the step budget and share the workflow timeout.

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
only C++17 and are supported by CI on Windows, Linux, and macOS. The built-in real
local-model HTTP provider uses WinHTTP and is currently available on Windows.
Provider adapters are implementation extensions and do not change language
semantics.

## Compatibility

The language version is `major.minor`. Implementations may add providers and
diagnostics in a patch release. New syntax or changed execution semantics require
a minor or major version change. Unknown statements must be rejected.
