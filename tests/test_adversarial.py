"""Independent failure-oriented tests; no network or real model required."""
import io
import json
import threading
import unittest
from dataclasses import replace
from unittest.mock import Mock, patch

from vora.parser import ParseError, parse
from vora.providers import OllamaProvider
from vora.runtime import ExecutionError, TransientProviderError, run


SINGLE = '''workflow Test(input):
agent worker = "PRIVATE_ROLE"
answer = worker("PRIVATE_PROMPT {input}")
return answer
'''

PARALLEL = '''workflow Test(input):
agent worker = "worker"
left = worker("left {input}")
right = worker("right {input}")
answer = worker("join {left} {right}")
return answer
'''


class RuntimeAdversarialTests(unittest.TestCase):
    def test_invalid_limits_never_invoke_provider(self):
        for option, values in {"max_workers": [0, -1, True, 1.5],
                               "max_calls": [0, -1, True, 1.5],
                               "retries": [-1, True, 1.5]}.items():
            for value in values:
                with self.subTest(option=option, value=value):
                    provider = Mock(return_value="ok")
                    with self.assertRaises(ExecutionError):
                        run(parse(SINGLE), "x", provider, **{option: value})
                    provider.assert_not_called()

    def test_manually_constructed_invalid_graphs_never_invoke_provider(self):
        workflow = parse(SINGLE)
        step = workflow.steps[0]
        variants = [
            replace(workflow, steps=(step, step)),
            replace(workflow, output="missing"),
            replace(workflow, steps=(replace(step, agent="missing"),)),
            replace(workflow, steps=(replace(step, prompt="{missing}", dependencies=("missing",)),)),
            replace(workflow, steps=(replace(step, prompt="{answer}", dependencies=("answer",)),)),
            replace(workflow, steps=(replace(step, prompt="{input}", dependencies=("answer",)),)),
        ]
        for candidate in variants:
            with self.subTest(candidate=candidate):
                provider = Mock(return_value="ok")
                with self.assertRaises(ExecutionError):
                    run(candidate, "x", provider)
                provider.assert_not_called()

    def test_callback_mutation_and_exception_cannot_change_trace(self):
        def observer(event):
            event["event"] = "tampered"
            event["secret"] = "observer secret"
            raise RuntimeError("observer secret")
        result = run(parse(SINGLE), "x", Mock(return_value="ok"), on_event=observer)
        self.assertEqual(result.result, "ok")
        self.assertEqual([event["event"] for event in result.events],
                         ["step_started", "step_completed", "workflow_completed"])
        self.assertNotIn("observer secret", json.dumps(result.events))

    def test_provider_secrets_absent_from_failures_and_traces(self):
        for error in [RuntimeError("PRIVATE_TOKEN"),
                      ExecutionError("PRIVATE_TOKEN"),
                      TransientProviderError("PRIVATE_TOKEN")]:
            with self.subTest(error=type(error).__name__):
                with self.assertRaises(ExecutionError) as caught:
                    run(parse(SINGLE), "PRIVATE_INPUT", Mock(side_effect=error))
                rendered = str(caught.exception) + json.dumps(caught.exception.events)
                for secret in ["PRIVATE_TOKEN", "PRIVATE_INPUT", "PRIVATE_PROMPT", "PRIVATE_ROLE"]:
                    self.assertNotIn(secret, rendered)

    def test_success_trace_excludes_content(self):
        result = run(parse(SINGLE), "PRIVATE_INPUT", Mock(return_value="PRIVATE_OUTPUT"))
        rendered = json.dumps(result.events)
        for secret in ["PRIVATE_INPUT", "PRIVATE_OUTPUT", "PRIVATE_PROMPT", "PRIVATE_ROLE"]:
            self.assertNotIn(secret, rendered)

    def test_only_explicit_transient_error_is_retried(self):
        permanent = Mock(side_effect=ValueError("bad credentials"))
        with self.assertRaises(ExecutionError):
            run(parse(SINGLE), "x", permanent, retries=4)
        self.assertEqual(permanent.call_count, 1)
        transient = Mock(side_effect=[TransientProviderError("again"), "ok"])
        result = run(parse(SINGLE), "x", transient, retries=1)
        self.assertEqual(result.result, "ok")
        self.assertEqual(transient.call_count, 2)

    def test_retry_count_and_call_limit(self):
        provider = Mock(side_effect=TransientProviderError("again"))
        with self.assertRaises(ExecutionError):
            run(parse(SINGLE), "x", provider, retries=2)
        self.assertEqual(provider.call_count, 3)
        provider.reset_mock()
        with self.assertRaises(ExecutionError) as caught:
            run(parse(SINGLE), "x", provider, retries=10, max_calls=2)
        self.assertEqual(provider.call_count, 2)
        self.assertEqual(caught.exception.events[-1]["calls"], 2)

    def test_parallel_retry_budget_never_overshoots(self):
        lock = threading.Lock()
        count = 0
        def provider(**kwargs):
            nonlocal count
            with lock:
                count += 1
            raise TransientProviderError("again")
        with self.assertRaises(ExecutionError) as caught:
            run(parse(PARALLEL), "x", provider, max_workers=2, retries=10, max_calls=3)
        self.assertEqual(count, 3)
        self.assertEqual(caught.exception.events[-1]["calls"], 3)
        self.assertNotIn("answer", [event.get("step") for event in caught.exception.events])

    def test_actual_parallelism_and_join_inputs(self):
        barrier = threading.Barrier(2, timeout=2)
        calls = []
        lock = threading.Lock()
        def provider(*, role, prompt):
            with lock:
                calls.append(prompt)
            if prompt.startswith("join"):
                self.assertEqual(prompt, "join LEFT RIGHT")
                return "done"
            barrier.wait()
            return prompt.split()[0].upper()
        result = run(parse(PARALLEL), "x", provider, max_workers=2)
        self.assertEqual(result.result, "done")
        self.assertEqual(len(calls), 3)

    def test_parallel_failure_never_runs_join(self):
        barrier = threading.Barrier(2, timeout=2)
        calls = []
        lock = threading.Lock()
        def provider(*, role, prompt):
            with lock:
                calls.append(prompt)
            barrier.wait()
            if prompt.startswith("left"):
                raise RuntimeError("failure")
            return "right result"
        with self.assertRaises(ExecutionError) as caught:
            run(parse(PARALLEL), "x", provider, max_workers=2)
        self.assertEqual(len(calls), 2)
        self.assertEqual(caught.exception.events[-1]["event"], "workflow_failed")
        self.assertEqual({event["step"] for event in caught.exception.events
                          if event["event"] in {"step_completed", "step_failed"}}, {"left", "right"})

    def test_invalid_provider_output_stops_downstream(self):
        workflow = parse(SINGLE.replace("return answer", 'next = worker("{answer}")\nreturn next'))
        for response in [None, {}, [], 42, "", " \n"]:
            with self.subTest(response=response):
                provider = Mock(return_value=response)
                with self.assertRaises(ExecutionError):
                    run(workflow, "x", provider, retries=3)
                self.assertEqual(provider.call_count, 1)

    def test_replacement_text_is_inert_and_not_recursively_expanded(self):
        provider = Mock(return_value="ok")
        payload = "{missing} __import__('os').system('echo SHOULD_NOT_RUN')"
        run(parse(SINGLE), payload, provider)
        self.assertEqual(provider.call_args.kwargs["prompt"], "PRIVATE_PROMPT " + payload)


class ParserAdversarialTests(unittest.TestCase):
    def test_rejects_cycles_unknown_names_and_python_statements(self):
        invalid = [
            SINGLE.replace("{input}", "{missing}"),
            SINGLE.replace("{input}", "{answer}"),
            SINGLE.replace("return answer", 'other = worker("{answer}")\nreturn other').replace("{input}", "{other}"),
            SINGLE.replace("return answer", "import os\nreturn answer"),
            SINGLE.replace("return answer", "answer = worker(\"again\")\nreturn answer"),
        ]
        for source in invalid:
            with self.subTest(source=source):
                with self.assertRaises(ParseError):
                    parse(source)


class ProviderAdversarialTests(unittest.TestCase):
    def test_rejects_external_ambiguous_or_credentialed_endpoints(self):
        for endpoint in ["https://localhost/api/chat", "http://example.com/api/chat",
                         "http://localhost.evil.test/api/chat", "http://127.0.0.1@evil.test/api/chat",
                         "http://user:secret@localhost/api/chat", "http://localhost/api/chat?url=evil",
                         "http://localhost/api/chat#fragment", "http://localhost:0/api/chat",
                         "http://localhost:65536/api/chat", "http://localhost/other",
                         "http://2130706433/api/chat", "file:///etc/passwd"]:
            with self.subTest(endpoint=endpoint):
                with self.assertRaises(ValueError):
                    OllamaProvider(endpoint=endpoint)

    def test_localhost_is_canonicalized_to_fixed_loopback(self):
        provider = OllamaProvider(endpoint="http://localhost:12345/")
        self.assertEqual(provider.endpoint, "http://127.0.0.1:12345/api/chat")

    def test_invalid_provider_controls(self):
        for timeout in [0, -1, True, float("nan"), float("inf"), "10"]:
            with self.subTest(timeout=timeout):
                with self.assertRaises(ValueError):
                    OllamaProvider(timeout=timeout)
        for tokens in [0, -1, True, 1.5]:
            with self.subTest(tokens=tokens):
                with self.assertRaises(ValueError):
                    OllamaProvider(max_output_tokens=tokens)

    def test_malformed_incomplete_and_tool_responses_fail(self):
        cases = [b"not json", b"\xff", b"[]", b"{}",
                 json.dumps({"message": {"content": "ok"}, "done": False}).encode(),
                 json.dumps({"message": {"content": "ok", "tool_calls": [{"function": "shell"}]}, "done": True}).encode(),
                 json.dumps({"message": {"content": []}, "done": True}).encode(),
                 b"x" * (4 * 1024 * 1024 + 1)]
        for payload in cases:
            with self.subTest(payload_size=len(payload)):
                with patch("vora.providers.build_opener") as factory:
                    factory.return_value.open.return_value = io.BytesIO(payload)
                    with self.assertRaises(ExecutionError):
                        OllamaProvider()(role="r", prompt="p")

    def test_token_limit_is_sent_and_valid_response_accepted(self):
        payload = json.dumps({"message": {"content": "ok"}, "done": True}).encode()
        with patch("vora.providers.build_opener") as factory:
            factory.return_value.open.return_value = io.BytesIO(payload)
            result = OllamaProvider(max_output_tokens=17)(role="role", prompt="prompt")
            request = factory.return_value.open.call_args.args[0]
            body = json.loads(request.data)
            self.assertEqual(body["options"]["num_predict"], 17)
            self.assertEqual(result, "ok")


if __name__ == "__main__":
    unittest.main()
