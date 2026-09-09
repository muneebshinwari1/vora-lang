# Engineering team and reusable assignments

This prototype was developed in one coordinated local task. Three subagents were launched alongside the coordinating agent; these are task-scoped assignments, not permanently running services.

| Agent | Assignment | Delivered evidence |
|---|---|---|
| Coordinator | Define grammar, implement parser/CLI, integrate and test the product | Core package, examples, integrated test run |
| Research | Independently compare official framework APIs and test the differentiation hypothesis | research.md, validation-plan.md |
| Runtime engineer | Implement bounded concurrent graph execution and a documented model adapter | runtime.py, providers.py, HTTP fixture tests |
| Adversarial reviewer | Independently try to break graph validation, limits, failures, traces and endpoint controls | test_adversarial.py, adversarial-review.md |

## Research prompt

Research agent orchestration DSL feasibility against concise LangGraph APIs, CrewAI, PydanticAI and declarative alternatives. Use primary sources and concrete API syntax. Identify overlapping capabilities, unproven novelty claims and one equivalent-semantics benchmark. Do not claim a 50-to-10-line reduction or commercial demand without evidence.

## Runtime engineering prompt

Implement a Python standard-library executor for a validated static DAG. Run independent ready steps concurrently, safely substitute declared inputs, count all attempts atomically, and retry explicit transient failures only. Return nonempty string results and content-free per-run/per-step/per-attempt events. Provide a clearly simulated fixture provider and an explicitly configured Ollama HTTP adapter. No implicit tool execution or credentials. Persist reproducible provider contract tests.

## Adversarial prompt

Independently inspect and test the parser, runtime, provider and CLI. Attempt malformed/hand-built graphs, unknown references, cycles, Python injection, concurrent retry budget overruns, permanent failures, malformed outputs, observer interference, secret leakage and endpoint bypasses. Report concrete reproducible blockers promptly. Do not modify implementation while reviewing it. Distinguish passed tests from untested production guarantees.

## Integration rule

Independent file ownership prevents conflicting edits. The coordinator integrates results, resolves reproducible findings and runs the full relevant suite. Research and review findings are evidence, not blanket approval or proof of commercial readiness.
