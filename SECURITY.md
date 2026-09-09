# Security policy

## Supported version

Vora is an experimental local developer preview. Security fixes apply to the
latest commit on `main`; no stable release line is supported yet.

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
traces, and build or dependency integrity issues. Model hallucination and weak
answer quality are important product limitations, but are not by themselves
software security vulnerabilities.
