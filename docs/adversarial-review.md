# Independent adversarial review

Date: 2026-09-08. Scope: `vora/parser.py`, `vora/runtime.py`, `vora/providers.py`, and `vora/cli.py`. This is a bounded engineering review, not a production security certification.

## Result

No blocking defect was reproduced in the tested parser, runtime, or provider controls. The first 18 tests in `tests/test_adversarial.py` passed independently using `python -m unittest discover -s tests -p test_adversarial.py -v` (1.800 seconds; process exit 0 confirmed). The coordinator separately reported an integrated 40-test pass; see `validation-results.md` for that evidence.

The final adversarial file retains those 18 validated tests. Additional unrun CLI and HTTP tests were removed to avoid duplicating existing coverage. The coordinator reports CLI demo and limit-failure coverage in `tests/test_examples.py` as part of the integrated 40-test pass. HTTP integration belongs to the provider test suite. Later ordinary shell reads stalled as well as commands; no additional verification is claimed from those stalled calls.

## What was challenged

- Parser rejects unknown references, self and multi-step cycles, duplicate steps and Python statements.
- Runtime validates manually constructed invalid graphs before making a provider call; invalid integer controls and boolean values are rejected.
- A barrier demonstrates actual concurrent branch execution. The join receives both completed branch outputs. When a branch fails, no join runs and sibling outcomes are recorded.
- Only explicit transient provider failures retry. Two retries mean at most three attempts; the total call limit is shared across concurrent attempts and does not overshoot in the test.
- Empty, whitespace-only and non-text results fail before dependent steps run.
- Observer mutation and ordinary observer exceptions do not alter the stored execution trace or successful result.
- Input containing braces and Python-like text remains inert and is not recursively expanded.
- Provider exception messages, prompts, roles, inputs and outputs are absent from the tested default runtime traces and sanitized runtime failure messages. Names/identifiers remain trace metadata; do not put secrets in workflow or step identifiers.
- Ollama endpoints reject external hosts, credential-bearing URLs, query strings, fragments, invalid ports and unexpected paths. `localhost` becomes the fixed address `127.0.0.1`.
- Invalid timeout/token controls, malformed JSON, incomplete responses, tool requests and oversized bodies fail. The requested output-token limit is present in the request payload.

## Material limits and follow-up work

1. **This is an orchestration prototype, not an autonomous agent product.** The demo is deterministic simulated text; mocked provider tests do not establish real-model quality or a successful Ollama installation. Tool execution, research quality and business demand remain unvalidated.
2. **There is no workflow wall-clock deadline or force cancellation.** The executor waits for in-flight provider calls. A custom provider or slow observer can block execution. The Ollama socket timeout is not an overall execution deadline. The implementation documents these limits; retain that disclosure.
3. **There is no durable resume, exactly-once execution or dollar budget.** Do not describe traces as checkpoints or the call cap as a monetary cap. External side effects require additional idempotency and approval semantics before adding them.
4. **This is not a sandbox for hostile Python plugins.** Custom providers and callbacks are trusted Python code. The DSL itself is parsed with a closed grammar and does not execute Python expressions.
5. **Returned content is intentionally visible.** Runtime traces exclude content; CLI output and `RunResult.outputs` contain results, and the demo may echo input. Do not claim system-wide secret redaction or privacy protection.
6. **Line reduction is not yet a customer or performance result.** Benchmark equivalent semantics and count required configuration. The existing ecosystem already provides durable execution, persistence and human intervention; Vora's authoring/debugging advantage needs a fair comparison.

Relevant official background: [LangGraph overview](https://docs.langchain.com/oss/python/langgraph/overview) documents existing orchestration features. [LangGraph interrupts](https://docs.langchain.com/oss/python/langgraph/interrupts) describes re-execution and the need for idempotent side effects.

## Suggested release decision

Suitable for an explicitly experimental local developer preview on the reported tested scope. Not evidence for production reliability, real-model effectiveness, product-market fit or income. Keep release claims limited to the capabilities and verification above.
