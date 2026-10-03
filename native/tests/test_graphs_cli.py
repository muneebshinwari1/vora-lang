"""Real loopback HTTP contracts for native conditional/review graph execution."""
import json
import os
import subprocess
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('VORA_TEST_EXE', NATIVE / ('dist/vora.exe' if os.name == 'nt' else 'dist/vora')))
KEEP = {'verdict': 'keep', 'issues': [], 'instruction': 'Keep the draft.'}
REVISE = {'verdict': 'revise', 'issues': ['Improve wording'], 'instruction': 'Improve the draft.'}
REVIEW = '''workflow Review(input):
agent writer = "writer"
agent critic = "critic"
answer = writer("Task {input}")
review answer with critic max 2
return answer
'''
ROUTE = '''workflow Route(input):
agent writer = "writer"
agent critic = "critic"
draft = writer("{input}")
critique = critic("Check {draft}")
validate critique as critique
final = writer("Revise {draft}: {critique}") when critique verdict "revise" else draft
return final
'''

class GraphCLI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.folder = Path(self.tmp.name)
        self.source = self.folder / 'graph.vora'
        self.source.write_text(REVIEW)
        self.checkpoint = self.folder / 'checkpoint.json'
        self.trace = self.folder / 'trace.json'
        self.requests = []
        self.verdict = KEEP
        self.fail_critic = False
        self.block_critic = False
        self.critic_entered = threading.Event()
        self.release_critic = threading.Event()
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                owner.requests.append(body)
                critic = body['messages'][0]['content'] == 'critic'
                if critic and owner.block_critic:
                    owner.critic_entered.set()
                    owner.release_critic.wait(5)
                if critic and owner.fail_critic:
                    self.send_response(503)
                    self.end_headers()
                    return
                content = json.dumps(owner.verdict) if critic else 'WORKING DRAFT'
                try:
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/json')
                    self.end_headers()
                    self.wfile.write(json.dumps({'choices': [{'message': {'content': content}, 'finish_reason': 'stop'}]}).encode())
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.release_critic.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(2)
        self.tmp.cleanup()

    def invoke(self, state='--checkpoint', calls=8):
        return subprocess.run([str(EXE), 'run', str(self.source), '--input', 'task',
            '--endpoint', f'http://127.0.0.1:{self.server.server_port}/v1/chat/completions',
            '--max-calls', str(calls), '--trace', str(self.trace), state, str(self.checkpoint)],
            capture_output=True, text=True, timeout=15)

    def test_internal_critic_schema_and_completed_resume(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.requests), 2)
        self.assertIn('response_format', self.requests[1])
        checkpoint = json.loads(self.checkpoint.read_text())
        self.assertEqual(checkpoint['graph_state']['answer']['phase'], 'accepted')
        self.assertEqual(checkpoint['calls'], 2)
        result = self.invoke('--resume')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.requests), 2)
        self.assertIn('WORKING DRAFT', result.stdout)

    def test_failed_critic_resumes_saved_draft_and_keeps_budget(self):
        self.fail_critic = True
        result = self.invoke()
        self.assertEqual(result.returncode, 1)
        saved = json.loads(self.checkpoint.read_text())
        self.assertEqual(saved['graph_state']['answer']['phase'], 'critique')
        self.assertEqual(saved['calls'], 2)
        self.fail_critic = False
        result = self.invoke('--resume')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.requests), 3)
        self.assertEqual([r['messages'][0]['content'] for r in self.requests], ['writer', 'critic', 'critic'])
        self.assertEqual(json.loads(self.checkpoint.read_text())['calls'], 3)

    def test_killed_process_resumes_completed_writer_stage(self):
        self.block_critic = True
        command = [str(EXE), 'run', str(self.source), '--input', 'task',
            '--endpoint', f'http://127.0.0.1:{self.server.server_port}/v1/chat/completions',
            '--max-calls', '8', '--checkpoint', str(self.checkpoint)]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertTrue(self.critic_entered.wait(5), 'Internal critic was not reached')
            process.kill()
            process.communicate(timeout=5)
            self.assertIsNotNone(process.poll())
            saved = json.loads(self.checkpoint.read_text())
            self.assertEqual(saved['graph_state']['answer']['phase'], 'critique')
            self.assertEqual(saved['calls'], 2)
            # Only this dead test process owned the empty lock in our temporary directory.
            lock = Path(str(self.checkpoint) + '.lock').resolve()
            self.assertEqual(lock.parent, self.folder.resolve())
            lock.rmdir()
            self.block_critic = False
            self.release_critic.set()
            result = self.invoke('--resume')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual([r['messages'][0]['content'] for r in self.requests], ['writer', 'critic', 'critic'])
            self.assertEqual(json.loads(self.checkpoint.read_text())['calls'], 3)
        finally:
            self.release_critic.set()
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)

    def test_exhausted_rounds_do_not_restart_on_resume(self):
        self.verdict = REVISE
        result = self.invoke()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 4)
        self.assertIn('Review round limit', result.stderr)
        self.assertIn('Improve wording', self.requests[2]['messages'][1]['content'])
        self.verdict = KEEP
        result = self.invoke('--resume')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 4)

    def test_routing_skips_unselected_call_and_restores_alias(self):
        self.source.write_text(ROUTE)
        result = self.invoke(calls=2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.requests), 2)
        events = json.loads(self.trace.read_text())
        self.assertTrue(any(e['event'] == 'step_skipped' for e in events))
        result = self.invoke('--resume', calls=2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.requests), 2)

    def test_changed_review_limit_rejected_before_http(self):
        self.assertEqual(self.invoke().returncode, 0)
        self.source.write_text(REVIEW.replace('max 2', 'max 3'))
        result = self.invoke('--resume')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 2)

    def test_missing_graph_state_fails_closed(self):
        self.assertEqual(self.invoke().returncode, 0)
        saved = json.loads(self.checkpoint.read_text())
        del saved['graph_state']
        self.checkpoint.write_text(json.dumps(saved))
        result = self.invoke('--resume')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 2)

if __name__ == '__main__':
    unittest.main(verbosity=2)
