# Security policy

## Supported version

Security fixes target the latest native 0.4.x release and `main`. Earlier
previews and the historical Python prototype do not have a supported stable
release line. Native 0.4.x supports reviewed trusted local workflows under the
[deployment contract](docs/production-readiness.md).

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting feature for this
repository. Do not include secrets, private prompts, model outputs, or personal
data in a public issue.

Include the affected commit, platform, reproduction steps, impact, and any
suggested mitigation. Reports will be acknowledged when the maintainer is
available; no response-time guarantee is offered for this experimental project.

## Scope

Useful reports include parser/runtime vulnerabilities, loopback endpoint
bypasses, unsafe handling of provider responses, sensitive content written to
traces, workspace capability/path-containment bypasses, unsafe memory/checkpoint
handling, and build or dependency integrity issues. Model hallucination and weak
answer quality are important product limitations, but are not by themselves
software security vulnerabilities.

## Trust boundaries and expected controls

The native runtime is a local developer tool, not an internet-facing service.
Workflow source, provider responses and workspace documents may be untrusted.
Tool implementations, host capability grants and local state files are trusted.

- Reject undeclared tools and tools absent from the explicit host allowlist.
- Require workspace grants for filesystem tools; reject traversal, absolute
  paths and symlink components according to the documented tool contract.
- Accept only loopback model endpoints; disable proxies and reject redirects.
- Enforce bounded calls, tool inputs and state/provider response sizes.
- Keep prompt/output content and tool arguments out of standard trace events.
- Bind checkpoint reuse to workflow, input, provider and tool configuration.

These are review expectations, not a claim that a security audit proves them.

## Documented limitations

Memory, results and checkpoints contain plaintext task/output data. Normal OS
permissions apply; Vora does not encrypt them or configure ACLs. Checkpoints are
trusted local artifacts, not authenticated imports. Do not resume someone
else's state. Opt-in storage of this data is documented behavior; unintended
disclosure or authorization bypasses remain reportable.

Workspace checks are not an OS sandbox or race-free against hostile concurrent
filesystem replacement. Untrusted documents may influence model output. Known
limitations do not exclude new concrete bypasses from review. Resume can repeat
unfinished calls; POSIX power-loss durability and exactly-once external effects
are not promised. See [tools](native/docs/tools.md) and the
[native guide](native/README.md) for the full boundaries.
