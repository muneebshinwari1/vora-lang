# Vora research assessment

Reviewed 2026-09-08. This is a documentation-based feasibility assessment, not a market study or executed framework benchmark. Vora's novelty, profitability and user adoption have not been established. No customer pilot or willingness-to-pay evidence is claimed.

## Findings from primary sources

| Existing option | Verified capability / API | Consequence for Vora |
| --- | --- | --- |
| [LangGraph Functional API](https://docs.langchain.com/oss/python/langgraph/functional-api) | `@entrypoint` and `@task` support ordinary Python control flow, asynchronous tasks, persistence and interrupts. | Compare with this concise API as well as ordinary Python; a verbose graph tutorial is an unfair sole baseline. |
| [CrewAI Flows](https://docs.crewai.com/en/concepts/flows) | `@start()`, `@listen(...)` and `@persist`; routing and human-feedback decorators are documented. | Dependencies, persistence and handoffs are established capabilities. |
| [PydanticAI multi-agent applications](https://pydantic.dev/docs/ai/guides/multi-agent-applications/) | Delegation, programmatic handoffs and graph control flow; `SubAgents` exposes delegates and propagates dependencies and usage limits. | Simple chaining already requires little orchestration machinery. |
| [PydanticAI usage controls](https://pydantic.dev/docs/ai/api/pydantic-ai/usage/) | `UsageLimits` includes request, token, tool and cost controls. Token limits are checked after model responses; unavailable cost information is explicitly considered. | A future Vora budget directive needs precise semantics. Tracking accrued usage is different from guaranteeing a hard dollar ceiling. |
| [Oracle Open Agent Spec](https://github.com/oracle/agent-spec) | Declarative agents and flows, JSON/YAML serialization, reference runtime and adapters for LangGraph, AutoGen and CrewAI. | Framework-independent agent specifications already exist; interoperability may be more useful than claiming invention. |

[BAML's workflow announcement](https://boundaryml.com/blog/workflows) is additional language prior art. That historical page labels its implementation a tech preview. It does not establish current production readiness or the current feature set.

## Proposed direction

A narrow language is feasible. The product hypothesis is that a readable workflow file, an inspectable execution plan and useful errors make common orchestration tasks easier to change and debug. This assessment does not claim that competitors lack those features.

Start with explicit input dependencies, sequential and parallel execution, bounded transient retries, trace events and a deterministic fixture backend. Reject undefined references and invalid dependencies before running. Keep arbitrary code execution and implicit network tools outside the grammar. Preserve an adapter boundary for real providers and existing frameworks.

Durable recovery needs a separate contract. LangGraph documents that interrupted nodes replay from their start and unfinished side effects can require idempotency protection. A saved JSON output is not sufficient evidence of crash-safe execution. [Interrupt semantics](https://docs.langchain.com/oss/python/langgraph/interrupts)

## Claims to avoid

- Do not claim "50 lines become 10" until equivalent behavior is measured, including all user-authored configuration and adapters.
- A fixture-backed demonstration validates interpreter behavior; it does not validate live model quality, real web research, cost savings or production reliability.
- A successful local prototype is not a public product launch. A public release still needs packaging, documentation and an explicit statement of supported behavior.
- Paid hosting is a possible business model, not evidence of demand or profit.

See [validation-plan.md](validation-plan.md) for proposed comparative and user-validation work.
