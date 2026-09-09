# Proposed Vora validation plan

All numerical targets below are proposed decision thresholds, not achieved results. No framework comparison or five-user pilot is claimed by this plan. Keep deterministic runtime checks, live-provider checks and commercial evidence separate.

## Equivalent-behavior benchmark

Implement the same workflow in Vora, ordinary Python and LangGraph Functional API: two independent research fixtures run concurrently, a synthesis step consumes both outputs, a critic reviews the synthesis and the workflow returns a final result. Inject one transient failure into one branch. Use identical fixture data, prompts, inputs, dependency ordering, retry eligibility and maximum attempts.

Use a maintained, readable Python baseline rather than deliberately expanded boilerplate. Pin Python/framework versions and publish all benchmark sources. Framework setup must not count against only one implementation. If a runtime lacks a feature, report that gap rather than silently removing it from the comparison.

Report line counts separately:

| Category | Counting rule |
| --- | --- |
| Workflow source | Nonblank, noncomment physical lines of user-authored orchestration. |
| Configuration and prompts | All required agent definitions, provider settings, prompt files and schemas. |
| User-authored adapters | Any custom code needed to make the example execute with equivalent behavior. |
| Runner/setup | Invocation commands and initialization source; report separately from workflow code. |
| Framework/runtime | Identify versions and dependencies; do not count installed framework implementation as user code. |

Also publish combined user-authored totals and file counts. Avoid compressed semicolon lines and hidden defaults that change semantics. A ten-line workflow plus extensive configuration is not a ten-line complete application.

## Correctness and failure checks

Proposed release gate: every applicable deterministic check passes on the supported environment before making comparative claims.

- Same outputs for the same fixtures and inputs.
- Both independent branches overlap; synthesis starts only after both succeed.
- A transient failure receives exactly the configured attempts; permanent failures stop without retries.
- A failed prerequisite prevents its downstream steps from executing.
- Undefined agents, duplicate names and invalid input references fail before backend calls.
- Trace events distinguish attempts, completion and failure without asserting fabricated model usage.
- If persistence, cancellation, budgets or approval gates are later advertised, add behavior-specific tests before including those claims.

Report wall time across at least 20 deterministic runs, with median and range. Fixture timings measure orchestration overhead, not LLM performance. A separate live-provider experiment must hold model, prompts, parameters and inputs constant and record actual calls, usage and failures; do not infer live quality from fixture results.

Proposed concision target: at least 30% fewer combined user-authored source/configuration/adapter lines than ordinary Python for this workflow without failing any semantic checks. Publish the LangGraph result even if it is shorter than Vora. Missing the target should prompt investigation rather than selective reporting.

## Five-user developer pilot

Recruit five developers who have recently built an AI workflow. Participation and outreach require actual users; this document does not authorize sending invitations. Use the same starter material and a fixed 10-minute introduction. Counterbalance which implementation each participant tries first.

Ask each participant to run the example, add a third independent researcher, diagnose one broken reference, and explain what happens when a branch exhausts its retries. Record completion time, hints, wrong assumptions and defects. Ask for candid reasons to keep or abandon the tool; interest alone is not a sale.

Proposed thresholds for continuing the product experiment:

- At least 4 of 5 complete the change and debugging tasks within 30 minutes without the facilitator editing code.
- Median combined change/debugging time improves by at least 20% against each participant's baseline implementation.
- At least 3 of 5 independently use Vora for a second task within seven days.
- At least 2 identify a specific workflow they would trial; test willingness to pay separately through a concrete, deliverable offer.

Five users provide directional qualitative evidence, not a statistically reliable market estimate. Report individual results and negative feedback. A paid pilot would demonstrate one transaction, not profitability; estimate provider, hosting and support costs before projecting margins.
