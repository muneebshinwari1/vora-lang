import json
import threading
import unittest

from vora.parser import parse
from vora.runtime import ExecutionError, TransientProviderError, run


def chain():
    return parse('''workflow Example(input):
agent a = "role"
x = a("first {input}")
y = a("second {x}")
return y''')


class RuntimeTests(unittest.TestCase):
    def test_data_flow_and_nonrecursive_interpolation(self):
        seen = []
        def provider(*, role, prompt):
            seen.append(prompt)
            return "result {input}" if len(seen) == 1 else "done"
        result = run(chain(), "SECRET {x}", provider)
        self.assertEqual(seen, ["first SECRET {x}", "second result {input}"])
        self.assertEqual(result.result, "done")
        self.assertNotIn("SECRET", json.dumps(result.events))

    def test_parallel_overlap_and_join(self):
        workflow = parse('''workflow Example(input):
agent a = "role"
x = a("left")
y = a("right")
z = a("join {x} {y}")
return z''')
        barrier = threading.Barrier(2)
        seen = []
        def provider(*, role, prompt):
            if prompt in ("left", "right"):
                barrier.wait(timeout=2)
            seen.append(prompt)
            return prompt.upper()
        result = run(workflow, "", provider, max_workers=2)
        self.assertEqual(seen[-1], "join LEFT RIGHT")
        self.assertEqual(result.result, "JOIN LEFT RIGHT")

    def test_only_transient_errors_retry(self):
        calls = []
        def provider(*, role, prompt):
            calls.append(prompt)
            if len(calls) == 1:
                raise TransientProviderError("temporary")
            return "ok"
        result = run(chain(), "", provider, retries=1)
        self.assertEqual(len(calls), 3)
        self.assertIn("step_retry", [e["event"] for e in result.events])

    def test_permanent_failure_blocks_dependents_and_redacts(self):
        calls = []
        def provider(*, role, prompt):
            calls.append(prompt)
            raise ValueError("SECRET credential")
        with self.assertRaises(ExecutionError) as ctx:
            run(chain(), "", provider, retries=4)
        self.assertEqual(len(calls), 1)
        self.assertNotIn("SECRET", json.dumps(ctx.exception.events))
        self.assertNotIn("SECRET", str(ctx.exception))

    def test_retry_exhaustion_and_call_limit(self):
        calls = []
        def provider(*, role, prompt):
            calls.append(prompt)
            raise TransientProviderError("retry")
        with self.assertRaises(ExecutionError):
            run(chain(), "", provider, retries=5, max_calls=2)
        self.assertLessEqual(len(calls), 2)

    def test_parallel_call_limit_is_atomic(self):
        workflow = parse('''workflow Example(input):
agent a = "role"
x = a("one")
y = a("two")
z = a("three")
return z''')
        calls = []
        def provider(*, role, prompt):
            calls.append(prompt)
            return "ok"
        with self.assertRaises(ExecutionError):
            run(workflow, "", provider, max_calls=2)
        self.assertLessEqual(len(calls), 2)

    def test_malformed_provider_result(self):
        for bad in (None, {}, "", "   "):
            with self.subTest(bad=bad), self.assertRaises(ExecutionError):
                run(chain(), "", lambda **kw: bad)

    def test_bad_limits(self):
        for options in ({"max_workers": 0}, {"retries": -1}, {"max_calls": 0}):
            with self.subTest(options=options), self.assertRaises((ValueError, ExecutionError)):
                run(chain(), "", lambda **kw: "ok", **options)


if __name__ == "__main__":
    unittest.main()
