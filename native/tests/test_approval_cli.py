"""Process-boundary acceptance tests for durable human approval; no real AI used."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

NATIVE = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('VORA_TEST_EXE', NATIVE / ('dist/vora.exe' if os.name == 'nt' else 'dist/vora')))
SOURCE = '''workflow Approval(input):
agent writer = "writer"
agent editor = "editor"
draft = writer("{input}")
approval approved = draft "Review exact draft before editor runs."
final = editor("Use {approved}")
return final
'''

class ApprovalCLI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.folder = Path(self.tmp.name)
        self.source = self.folder / 'job.vora'
        self.source.write_text(SOURCE)
        self.state = self.folder / 'state.json'
        self.trace = self.folder / 'trace.json'
        self.output = self.folder / 'result.json'
        self.requests = []
        self.block = False
        self.entered = threading.Event()
        self.release = threading.Event()
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                owner.requests.append(body)
                role = body['messages'][0]['content']
                if owner.block and role == 'editor':
                    owner.entered.set()
                    owner.release.wait(10)
                text = 'DRAFT for approval' if role == 'writer' else 'FINAL approved content'
                if role == 'critic':
                    text = json.dumps({'verdict': 'keep', 'issues': [], 'instruction': 'Keep.'})
                try:
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/json')
                    self.end_headers()
                    self.wfile.write(json.dumps({'choices': [{'message': {'content': text}, 'finish_reason': 'stop'}]}).encode())
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
            def log_message(self, *args):
                pass
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.release.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(2)
        self.tmp.cleanup()

    def cli(self, *args):
        return subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True, timeout=20)

    def command(self, resume=False):
        return ['run', str(self.source), '--input', 'task', '--endpoint',
                f'http://127.0.0.1:{self.server.server_port}/v1/chat/completions',
                '--max-calls', '8', '--trace', str(self.trace), '--output', str(self.output),
                '--resume' if resume else '--checkpoint', str(self.state)]

    def run_job(self, resume=False):
        return self.cli(*self.command(resume))

    def state_json(self):
        return json.loads(self.state.read_text())

    def pause(self):
        result = self.run_job()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(self.output.exists())
        return self.state_json()['approvals']['approved']['request_id']

    def decide(self, request, decision='approve', step='approved', **kwargs):
        return self.cli('decide', self.state, '--step', step, '--request', request,
                        '--decision', decision, '--by', kwargs.get('by', 'test-operator'),
                        '--reason', kwargs.get('reason', 'Reviewed exact draft'))

    def test_pause_inspect_decide_resume_and_completed_resume(self):
        request = self.pause()
        saved = self.state_json()
        self.assertEqual(saved['version'], 2)
        self.assertEqual(saved['calls'], 1)
        self.assertEqual(len(self.requests), 1)
        self.assertNotIn('approved', saved['outputs'])
        events = json.loads(self.trace.read_text())
        self.assertEqual(events[-1]['event'], 'workflow_paused')
        self.assertFalse(any(e['event'] == 'workflow_failed' for e in events))
        inspected = self.cli('approvals', self.state)
        self.assertEqual(inspected.returncode, 0, inspected.stderr)
        record = json.loads(inspected.stdout)['requests'][0]
        self.assertEqual(record['value'], 'DRAFT for approval')
        self.assertEqual(record['request_id'], request)
        self.assertEqual(self.decide(request).returncode, 0)
        self.assertEqual(len(self.requests), 1, 'Decision executed a provider')
        decided = self.state_json()['approvals']['approved']
        self.assertEqual(decided['status'], 'approved')
        self.assertEqual(decided['by'], 'test-operator')
        self.assertTrue(decided['decided_at'])
        self.assertEqual(self.run_job(True).returncode, 0)
        self.assertEqual(len(self.requests), 2)
        self.assertEqual(self.requests[1]['messages'][1]['content'], 'Use DRAFT for approval')
        self.assertEqual(self.state_json()['calls'], 2)
        self.assertEqual(self.run_job(True).returncode, 0)
        self.assertEqual(len(self.requests), 2)

    def test_pending_resume_preserves_request_and_budget(self):
        request = self.pause()
        before = self.state_json()
        for _ in range(2):
            result = self.run_job(True)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertEqual(self.state_json(), before)
        self.assertEqual(self.state_json()['approvals']['approved']['request_id'], request)
        self.assertEqual(len(self.requests), 1)

    def test_rejection_is_terminal_and_cannot_be_overwritten(self):
        request = self.pause()
        self.assertEqual(self.decide(request, 'reject').returncode, 0)
        before = self.state.read_bytes()
        self.assertEqual(self.run_job(True).returncode, 1)
        self.assertEqual(self.decide(request).returncode, 1)
        self.assertEqual(self.state.read_bytes(), before)
        self.assertEqual(len(self.requests), 1)

    def test_stale_duplicate_and_unknown_decisions_do_not_write(self):
        request = self.pause()
        before = self.state.read_bytes()
        for req, step in [('0' * 32, 'approved'), (request, 'missing')]:
            self.assertEqual(self.decide(req, step=step).returncode, 1)
            self.assertEqual(self.state.read_bytes(), before)
        self.assertEqual(self.decide(request).returncode, 0)
        before = self.state.read_bytes()
        self.assertEqual(self.decide(request).returncode, 1)
        self.assertEqual(self.state.read_bytes(), before)

    def test_malformed_and_forged_state_fail_before_calls(self):
        self.pause()
        original = self.state_json()
        cases = []
        state = json.loads(json.dumps(original)); del state['approvals']; cases.append(state)
        state = json.loads(json.dumps(original)); state['approvals']['approved']['value'] = 'changed'; cases.append(state)
        state = json.loads(json.dumps(original)); state['outputs']['approved'] = 'DRAFT for approval'; cases.append(state)
        state = json.loads(json.dumps(original)); state['approvals']['approved']['status'] = 'approved'; cases.append(state)
        state = json.loads(json.dumps(original)); state['version'] = 1; cases.append(state)
        state = json.loads(json.dumps(original)); state['calls'] = 2**32 + 1; cases.append(state)
        for state in cases:
            with self.subTest(state=state):
                self.state.write_text(json.dumps(state))
                before = self.state.read_bytes()
                self.assertEqual(self.run_job(True).returncode, 1)
                self.assertEqual(self.state.read_bytes(), before)
                self.assertEqual(len(self.requests), 1)

    def test_decision_lock_and_failed_write_preserve_pending(self):
        request = self.pause()
        before = self.state.read_bytes()
        lock = Path(str(self.state) + '.lock'); lock.mkdir()
        try:
            self.assertEqual(self.decide(request).returncode, 1)
        finally:
            lock.rmdir()
        temporary = Path(str(self.state) + '.tmp'); temporary.mkdir()
        (temporary / 'blocker').write_text('owned fixture')
        try:
            self.assertEqual(self.decide(request).returncode, 1)
            self.assertEqual(self.state.read_bytes(), before)
        finally:
            (temporary / 'blocker').unlink(); temporary.rmdir()
        self.assertEqual(self.decide(request).returncode, 0)

    def test_no_checkpoint_or_changed_workflow_rejected(self):
        command = self.command()[:-2]
        result = self.cli(*command)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 0)
        self.pause()
        self.source.write_text(SOURCE.replace('Review exact draft', 'Different review'))
        self.assertEqual(self.run_job(True).returncode, 1)
        self.assertEqual(len(self.requests), 1)

    def test_parallel_gates_pause_together_and_join_after_all_decisions(self):
        self.source.write_text('''workflow Parallel(input):
agent writer = "writer"
agent editor = "editor"
left = writer("left {input}")
right = writer("right {input}")
approval approved = left "Approve left"
approval second = right "Approve right"
final = editor("{approved} {second}")
return final
''')
        request = self.pause()
        state = self.state_json()
        self.assertEqual(len(state['approvals']), 2)
        self.assertEqual(state['calls'], 2)
        self.assertEqual(self.decide(request).returncode, 0)
        self.assertEqual(self.run_job(True).returncode, 2)
        self.assertEqual(len(self.requests), 2)
        self.assertEqual(self.decide(state['approvals']['second']['request_id'], step='second').returncode, 0)
        self.assertEqual(self.run_job(True).returncode, 0)
        self.assertEqual(len(self.requests), 3)

    def test_review_then_human_gate_retains_graph_stage(self):
        self.source.write_text(SOURCE.replace('draft = writer', 'agent critic = "critic"\ndraft = writer').replace('approval approved', 'review draft with critic max 2\napproval approved'))
        request = self.pause()
        self.assertEqual(self.state_json()['graph_state']['draft']['phase'], 'accepted')
        self.assertEqual(len(self.requests), 2)
        self.assertEqual(self.decide(request).returncode, 0)
        self.assertEqual(self.run_job(True).returncode, 0)
        self.assertEqual(len(self.requests), 3)

    def test_chained_gates_issue_separate_requests_without_provider_calls(self):
        self.source.write_text(SOURCE.replace('final = editor("Use {approved}")',
            'approval second = approved "Second operator review"\nfinal = editor("Use {second}")'))
        request = self.pause()
        self.assertEqual(len(self.state_json()['approvals']), 1)
        self.assertEqual(self.decide(request).returncode, 0)
        self.assertEqual(self.run_job(True).returncode, 2)
        state = self.state_json()
        self.assertEqual(len(state['approvals']), 2)
        second = state['approvals']['second']['request_id']
        self.assertNotEqual(request, second)
        self.assertEqual(len(self.requests), 1)
        self.assertEqual(self.decide(second, step='second').returncode, 0)
        self.assertEqual(self.run_job(True).returncode, 0)
        self.assertEqual(len(self.requests), 2)

    def test_pause_write_failure_does_not_report_successful_pause(self):
        # Force persistence failure before any provider call; no pending claim may escape.
        temporary = Path(str(self.state) + '.tmp'); temporary.mkdir()
        (temporary / 'blocker').write_text('owned fixture')
        try:
            result = self.run_job()
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(len(self.requests), 0)
            self.assertFalse(self.state.exists())
            self.assertNotIn('Paused:', result.stderr)
        finally:
            (temporary / 'blocker').unlink(); temporary.rmdir()

    def test_approval_does_not_bypass_output_validation(self):
        self.source.write_text(SOURCE.replace('return final', 'require approved contains "NOT_PRESENT"\nreturn final'))
        request = self.pause()
        self.assertEqual(self.decide(request).returncode, 0)
        result = self.run_job(True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.requests), 1)

    def test_killed_after_approval_resumes_without_new_decision(self):
        request = self.pause()
        self.assertEqual(self.decide(request).returncode, 0)
        self.block = True
        process = subprocess.Popen([str(EXE), *self.command(True)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertTrue(self.entered.wait(5))
            process.kill(); process.communicate(timeout=5)
            self.assertIsNotNone(process.poll())
            state = self.state_json()
            self.assertEqual(state['calls'], 2)
            self.assertEqual(state['approvals']['approved']['status'], 'approved')
            self.assertIn('approved', state['outputs'])
            lock = Path(str(self.state) + '.lock').resolve()
            self.assertEqual(lock.parent, self.folder.resolve()); lock.rmdir()
            self.block = False; self.release.set()
            self.assertEqual(self.run_job(True).returncode, 0)
            self.assertEqual(len(self.requests), 3)
            self.assertEqual(self.state_json()['calls'], 3)
        finally:
            self.release.set()
            if process.poll() is None:
                process.kill(); process.communicate(timeout=5)

    def test_decision_metadata_limits_and_control_characters(self):
        request = self.pause(); before = self.state.read_bytes()
        for by, reason in [('', 'valid'), ('x' * 129, 'valid'), ('operator', ''), ('operator', 'x' * 2049), ('operator', 'line\nbreak')]:
            self.assertEqual(self.decide(request, by=by, reason=reason).returncode, 1)
            self.assertEqual(self.state.read_bytes(), before)

    def test_gate_only_final_and_memory_no_phantom_agent(self):
        self.source.write_text(SOURCE.replace('return final', 'return approved').replace('final = editor("Use {approved}")\n', ''))
        memory = self.folder / 'memory.json'
        result = self.cli(*self.command(), '--memory', memory)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(memory.exists())
        request = self.state_json()['approvals']['approved']['request_id']
        self.assertEqual(self.decide(request).returncode, 0)
        result = self.cli(*self.command(True), '--memory', memory)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(json.loads(memory.read_text())['agents']), {'writer'})
        self.assertEqual(len(self.requests), 1)

if __name__ == '__main__':
    unittest.main(verbosity=2)
