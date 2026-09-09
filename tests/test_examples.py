import subprocess
import sys
import unittest
from pathlib import Path

from examples.company_research_baseline import workflow as baseline
from vora import load
from vora.providers import DemoProvider
from vora.runtime import run

ROOT = Path(__file__).resolve().parents[1]


class ExampleTests(unittest.TestCase):
    def test_python_and_dsl_equivalence(self):
        workflow = load(ROOT / "examples/company_research.vora")
        self.assertEqual(workflow, baseline)
        a = run(workflow, "Example", DemoProvider())
        b = run(baseline, "Example", DemoProvider())
        self.assertEqual(a.outputs, b.outputs)
        self.assertEqual(a.result, b.result)

    def test_cli_demo_is_explicit(self):
        result = subprocess.run([sys.executable, "-m", "vora", "run",
                                 "examples/review.vora", "--input", "Example"],
                                cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("SIMULATED DEMO", result.stdout)
        self.assertIn("no model or web requests", result.stderr)

    def test_cli_invalid_limit_fails(self):
        result = subprocess.run([sys.executable, "-m", "vora", "run",
                                 "examples/review.vora", "--input", "Example", "--max-calls", "1"],
                                cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn("minimum call count", result.stderr)


if __name__ == "__main__":
    unittest.main()
