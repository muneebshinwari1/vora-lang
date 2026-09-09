"""Developer-only protocol fixtures for the native exe. Python is not used by Vora."""
import json
import subprocess
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]


class NativeHTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.requests = []
        cls.status = 200
        cls.payload = {}
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                cls.requests.append(json.loads(self.rfile.read(int(self.headers['Content-Length']))))
                self.send_response(cls.status)
                self.send_header('Content-Type', 'application/json')
                if cls.status == 302:
                    self.send_header('Location', '/redirected')
                self.end_headers()
                self.wfile.write(json.dumps(cls.payload).encode())
            def log_message(self, *args):
                pass
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(2)

    def setUp(self):
        type(self).requests = []
        type(self).status = 200
        type(self).payload = {'choices': [{'message': {'content': 'REAL FIXTURE OUTPUT'}, 'finish_reason': 'stop'}]}

    def invoke(self, *args, source=None):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            workflow = folder / 'fixture.vora'
            workflow.write_text(source or 'workflow Test(input):\nagent a = "role"\nx = a("{input}")\nreturn x\n', encoding='utf-8')
            input_file = folder / 'input.txt'
            input_file.write_text('Quoted "text"\nsecond line {x}', encoding='utf-8-sig', newline='')
            trace = folder / 'trace.json'
            result = subprocess.run([str(NATIVE / 'dist/vora.exe'), 'run', str(workflow),
                                     '--input-file', str(input_file), '--trace', str(trace),
                                     '--endpoint', f'http://127.0.0.1:{self.server.server_port}/v1/chat/completions',
                                     *args], capture_output=True, text=True, timeout=15)
            events = json.loads(trace.read_text()) if trace.exists() else []
            return result, events

    def test_real_provider_request_contract_and_input_file(self):
        result, events = self.invoke('--max-tokens', '100')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('REAL FIXTURE OUTPUT', result.stdout)
        self.assertEqual(self.requests[0]['messages'][1]['content'], 'Quoted "text"\nsecond line {x}')
        self.assertEqual(self.requests[0]['max_tokens'], 100)
        self.assertFalse(self.requests[0]['stream'])
        self.assertFalse(self.requests[0]['chat_template_kwargs']['enable_thinking'])
        self.assertEqual(events[-1]['event'], 'workflow_completed')

    def test_incomplete_and_truncated_results_rejected(self):
        for finish in (None, 'length', 'tool_calls'):
            with self.subTest(finish=finish):
                type(self).payload['choices'][0]['finish_reason'] = finish
                result, events = self.invoke()
                self.assertEqual(result.returncode, 1)
                self.assertEqual(events[-1]['event'], 'workflow_failed')

    def test_redirect_not_followed(self):
        type(self).status = 302
        result, _ = self.invoke('--retries', '2')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 1)

    def test_retry_classification(self):
        for code, attempts in ((401, 1), (429, 3), (503, 3)):
            with self.subTest(code=code):
                type(self).requests = []
                type(self).status = code
                result, _ = self.invoke('--retries', '2')
                self.assertEqual(result.returncode, 1)
                self.assertEqual(len(self.requests), attempts)

    def test_external_endpoint_rejected(self):
        result, _ = self.invoke('--endpoint', 'http://example.com:80/v1/chat/completions')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 0)

    def test_malformed_and_tool_output_rejected(self):
        for message in ({'content': None}, {'content': '  \f\v'}, {'content': 'text', 'tool_calls': [{}]}):
            with self.subTest(message=message):
                type(self).payload['choices'][0]['message'] = message
                result, _ = self.invoke()
                self.assertEqual(result.returncode, 1)

    def test_explicit_demo_does_not_call_server(self):
        result, _ = self.invoke('--provider', 'demo')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('SIMULATED DEMO', result.stdout)
        self.assertEqual(len(self.requests), 0)

    def test_critique_validation_blocks_downstream_http_call(self):
        source = ('workflow Test(input):\nagent a = "role"\nreview = a("{input}")\n'
                  'final = a("{review}")\nvalidate review as critique\nreturn final\n')
        result, events = self.invoke('--retries', '2', source=source)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 1)
        self.assertIn('critique validation', result.stderr)
        self.assertEqual(events[1]['error'], 'validation_failed')
        self.assertNotIn('REAL FIXTURE OUTPUT', json.dumps(events))

    def test_valid_critique_reaches_editor_http_call(self):
        review = json.dumps({'verdict': 'revise', 'issues': ['Unsupported claim'], 'instruction': 'Remove the claim.'})
        type(self).payload['choices'][0]['message']['content'] = review
        source = ('workflow Test(input):\nagent a = "role"\nreview = a("{input}")\n'
                  'final = a("{review}")\nvalidate review as critique\nreturn final\n')
        result, events = self.invoke(source=source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.requests), 2)
        self.assertEqual(self.requests[1]['messages'][1]['content'], review)
        self.assertEqual(events[-1]['calls'], 2)
        schema = self.requests[0]['response_format']['json_schema']['schema']
        self.assertEqual(schema['properties']['issues']['items']['type'], 'string')
        self.assertFalse(schema['additionalProperties'])
        self.assertNotIn('response_format', self.requests[1])

    def test_bounded_reasoning_request_and_content_isolation(self):
        type(self).payload['choices'][0]['message']['reasoning_content'] = 'INTERNAL_REASONING_FIXTURE'
        result, events = self.invoke('--reasoning-budget', '128', '--max-tokens', '512')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests[0]['reasoning_budget_tokens'], 128)
        self.assertEqual(self.requests[0]['reasoning_format'], 'deepseek')
        self.assertTrue(self.requests[0]['chat_template_kwargs']['enable_thinking'])
        self.assertNotIn('INTERNAL_REASONING_FIXTURE', result.stdout + json.dumps(events))

    def test_reasoning_budget_must_leave_output_capacity(self):
        result, _ = self.invoke('--reasoning-budget', '256', '--max-tokens', '256')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
