# Engineering validation — 2026-09-08

## Recorded test run

Environment: Windows, Python 3.13.1. Command from project root:

```powershell
python -m unittest discover -s tests -v
```

The parent-agent integrated run reported **40 tests passed in 8.028 seconds**. This included the independent adversarial suite present at that point, parser/runtime tests, HTTP adapter fixtures, CLI tests and Python/DSL equivalence. Later additions, if any, are documented separately in the adversarial report.

Verified behaviors:

- Unknown agents/references, duplicate names, cycles and Python statements are rejected.
- The equivalent direct Python graph and DSL graph produce identical deterministic outputs.
- A synchronization barrier proves two independent calls overlap; the join receives both completed results.
- Only explicit transient failures retry; permanent failures block dependents.
- Concurrent attempts respect the shared call limit, including retries.
- Malformed results and incomplete/tool-request HTTP responses fail clearly.
- Prompt/output content and raw provider exception messages are absent from standard traces.
- Observer exceptions and mutations do not alter stored trace records.
- The Ollama HTTP fixture validates request shape, token option, status classification and redirect rejection.
- CLI output explicitly labels simulation; invalid execution limits return failure status.

The CLI syntax check returned `Valid: CompanyResearch, 3 steps`. The Mermaid plan showed `facts` and `risks` feeding `report`. A full CLI demo returned an explicitly labeled simulated report and wrote `docs/demo-trace.json`.

## Limits of this evidence

This is deterministic software/HTTP contract validation, not a real LLM evaluation. The Ollama executable was not found on this machine's PATH; no live model was downloaded or called. No factual research quality, real-world task completion rate, cost savings or productivity improvement has been established.

No comparison against a fully equivalent LangGraph implementation, user pilot, payment test or public launch has been completed. Eight workflow lines describe the sample graph; all supporting runtime/configuration must be included in any future complete-application comparison. See the [proposed validation plan](validation-plan.md).
