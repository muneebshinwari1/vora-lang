"""Offline provider contract tests using a temporary loopback HTTP server."""
import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from vora.providers import DemoProvider, OllamaProvider
from vora.runtime import ExecutionError, TransientProviderError


class ProviderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_POST(self):
                length = int(self.headers["Content-Length"])
                cls.requests.append((self.path, json.loads(self.rfile.read(length))))
                self.send_response(cls.status)
                for key, value in cls.headers.items():
                    self.send_header(key, value)
                self.send_header("Content-Length", str(len(cls.body)))
                self.end_headers()
                self.wfile.write(cls.body)

        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.endpoint = f"http://localhost:{cls.server.server_port}/api/chat"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def setUp(self):
        type(self).requests = []
        type(self).status = 200
        type(self).headers = {}
        self.set_payload({"message": {"content": "local response"}, "done": True})

    def set_payload(self, payload):
        type(self).body = json.dumps(payload).encode("utf-8")

    def call(self, **options):
        return OllamaProvider("test-model", endpoint=self.endpoint, **options)(
            role="Be helpful", prompt="Test input")

    def test_success_sends_expected_nonstreaming_chat_contract(self):
        self.assertEqual(self.call(max_output_tokens=17), "local response")
        self.assertEqual(self.requests, [("/api/chat", {
            "model": "test-model", "stream": False,
            "options": {"num_predict": 17},
            "messages": [{"role": "system", "content": "Be helpful"},
                         {"role": "user", "content": "Test input"}],
        })])

    def test_transient_http_statuses(self):
        for status in (429, 500, 503, 599):
            with self.subTest(status=status):
                type(self).status = status
                with self.assertRaises(TransientProviderError):
                    self.call()
        # Provider does not retry secretly; the runtime owns that policy.
        self.assertEqual(len(self.requests), 4)

    def test_permanent_http_error_does_not_leak_response_body(self):
        type(self).status = 404
        type(self).body = b"secret server response"
        with self.assertRaises(ExecutionError) as caught:
            self.call()
        self.assertNotIsInstance(caught.exception, TransientProviderError)
        self.assertNotIn("secret", str(caught.exception))

    def test_redirects_are_blocked(self):
        for status in (301, 302, 303, 307, 308):
            with self.subTest(status=status):
                type(self).status = status
                type(self).headers = {"Location": self.endpoint + "?redirected=true"}
                with self.assertRaises(ExecutionError) as caught:
                    self.call()
                self.assertNotIsInstance(caught.exception, TransientProviderError)
        self.assertEqual(len(self.requests), 5)
        self.assertTrue(all(path == "/api/chat" for path, _ in self.requests))

    def test_malformed_and_incomplete_payloads_are_rejected(self):
        for payload in (None, [], {}, {"message": []},
                        {"message": {"content": 3}, "done": True},
                        {"message": {"content": " "}, "done": True},
                        {"message": {"content": "partial"}, "done": False},
                        {"message": {"content": "text", "tool_calls": [{}]}, "done": True}):
            with self.subTest(payload=payload):
                self.set_payload(payload)
                with self.assertRaises(ExecutionError) as caught:
                    self.call()
                self.assertNotIsInstance(caught.exception, TransientProviderError)
        type(self).body = b"not JSON"
        with self.assertRaises(ExecutionError):
            self.call()

    def test_endpoint_restrictions(self):
        for endpoint in ("https://127.0.0.1:11434", "http://example.com",
                         "http://127.0.0.1:0", "http://user:pass@localhost",
                         "http://localhost/other", "http://localhost?x=1",
                         "http://localhost#fragment"):
            with self.subTest(endpoint=endpoint):
                with self.assertRaises(ValueError):
                    OllamaProvider(endpoint=endpoint)

    def test_provider_configuration_validation(self):
        for options in ({"model": ""}, {"timeout": 0}, {"timeout": float("nan")},
                        {"timeout": True}, {"max_output_tokens": 0},
                        {"max_output_tokens": True}):
            with self.subTest(options=options):
                with self.assertRaises(ValueError):
                    OllamaProvider(**options)

    def test_demo_is_deterministic_and_explicitly_simulated(self):
        demo = DemoProvider()
        first = demo(role="analyst", prompt="example")
        self.assertEqual(first, demo(role="analyst", prompt="example"))
        self.assertIn("SIMULATED DEMO", first)
        self.assertIn("no AI inference", first)


if __name__ == "__main__":
    unittest.main()
