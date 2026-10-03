"""Persistence integration tests; Python is developer tooling only."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('VORA_TEST_EXE', NATIVE / ('dist/vora.exe' if os.name == 'nt' else 'dist/vora')))

class StateCLI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.folder = Path(self.tmp.name)
        self.workflow = self.folder / 'fixture.vora'
        self.workflow.write_text('workflow Memory(input):\nagent a = "role"\nx = a("{input}")\nreturn x\n', encoding='utf-8')
        self.memory = self.folder / 'memory.json'
        self.checkpoint = self.folder / 'checkpoint.json'
        self.trace = self.folder / 'trace.json'

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *options, input_text='task'):
        return subprocess.run([str(EXE), 'run', str(self.workflow), '--provider', 'demo',
            '--input', input_text, *map(str, options)], capture_output=True, text=True, timeout=15)

    def test_archived_preview_memory_v1_contract(self):
        self.workflow.write_text('workflow Archived(input):\nagent a = "role"\nx = a("{input}")\nreturn x\n', encoding='utf-8')
        self.memory.write_bytes((NATIVE / 'tests/fixtures/memory-v1.json').read_bytes())
        result = self.run_cli('--memory', self.memory)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Prior task: baseline', result.stdout)
        entries = json.loads(self.memory.read_text())['agents']['a']['entries']
        self.assertEqual(entries[0]['input'], 'baseline')
        self.assertEqual(entries[-1]['input'], 'task')

    def test_archived_checkpoint_v1_contract(self):
        self.workflow.write_text('workflow Archived(input):\nagent a = "role"\nx = a("{input}")\nreturn x\n', encoding='utf-8')
        fixture = json.loads((NATIVE / 'tests/fixtures/checkpoint-v1.json').read_text())
        fixture['identity']['workspace'] = self.folder.resolve().as_posix()
        self.checkpoint.write_text(json.dumps(fixture))
        result = self.run_cli('--workspace', self.folder, '--resume', self.checkpoint, '--trace', self.trace)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('archived result', result.stdout)
        events = json.loads(self.trace.read_text())
        self.assertFalse(any(e['event'] == 'step_started' for e in events))
        self.assertTrue(any(e['event'] == 'step_restored' for e in events))
        # Earlier previews without the timeout identity are deliberately rejected.
        del fixture['identity']['http_timeout_ms']
        self.checkpoint.write_text(json.dumps(fixture))
        before = self.checkpoint.read_bytes()
        rejected = self.run_cli('--workspace', self.folder, '--resume', self.checkpoint)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual(self.checkpoint.read_bytes(), before)

    def test_memory_is_persistent_and_role_scoped(self):
        first = self.run_cli('--memory', self.memory)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_cli('--memory', self.memory, input_text='next')
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn('VORA PRIOR RUN CONTEXT', second.stdout)
        self.assertEqual(len(json.loads(self.memory.read_text())['agents']['a']['entries']), 2)
        self.workflow.write_text('workflow Memory(input):\nagent a = "new role"\nx = a("{input}")\nreturn x\n', encoding='utf-8')
        third = self.run_cli('--memory', self.memory)
        self.assertEqual(third.returncode, 0, third.stderr)
        self.assertNotIn('VORA PRIOR RUN CONTEXT', third.stdout)

    def test_completed_resume_is_call_free_and_memory_idempotent(self):
        first = self.run_cli('--memory', self.memory, '--checkpoint', self.checkpoint)
        self.assertEqual(first.returncode, 0, first.stderr)
        before = self.memory.read_bytes()
        second = self.run_cli('--memory', self.memory, '--resume', self.checkpoint, '--trace', self.trace)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(self.memory.read_bytes(), before)
        events = json.loads(self.trace.read_text())
        self.assertTrue(any(e['event'] == 'step_restored' for e in events))
        self.assertFalse(any(e['event'] == 'step_started' for e in events))

    def test_changed_input_and_provider_settings_rejected(self):
        self.assertEqual(self.run_cli('--checkpoint', self.checkpoint).returncode, 0)
        self.assertNotEqual(self.run_cli('--resume', self.checkpoint, input_text='changed').returncode, 0)
        self.assertNotEqual(self.run_cli('--resume', self.checkpoint, '--model', 'different').returncode, 0)
        before = self.checkpoint.read_bytes()
        self.assertNotEqual(self.run_cli('--checkpoint', self.checkpoint).returncode, 0)
        self.assertEqual(before, self.checkpoint.read_bytes())

    def test_locks_and_path_aliases_fail_without_overwrites(self):
        self.memory.with_name('memory.json.lock').mkdir()
        self.assertNotEqual(self.run_cli('--memory', self.memory).returncode, 0)
        self.assertFalse(self.memory.exists())
        before = self.workflow.read_bytes()
        self.assertNotEqual(self.run_cli('--checkpoint', self.workflow).returncode, 0)
        self.assertEqual(before, self.workflow.read_bytes())
        self.assertNotEqual(self.run_cli('--checkpoint', self.checkpoint, '--trace', self.checkpoint).returncode, 0)
        self.assertFalse(self.checkpoint.exists())
        self.assertNotEqual(self.run_cli('--checkpoint', self.checkpoint, '--output', str(self.checkpoint) + '.tmp').returncode, 0)
        self.assertFalse(self.checkpoint.exists())

    def test_corrupt_state_is_rejected(self):
        self.memory.write_text('{broken', encoding='utf-8')
        self.assertNotEqual(self.run_cli('--memory', self.memory).returncode, 0)
        self.assertEqual(self.memory.read_text(), '{broken')

    def test_result_and_trace_cannot_overwrite_source_or_each_other(self):
        before = self.workflow.read_bytes()
        for option in ('--trace', '--output'):
            with self.subTest(option=option):
                result = self.run_cli(option, self.workflow)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.workflow.read_bytes(), before)
        self.assertNotEqual(self.run_cli('--trace', self.trace, '--output', self.trace).returncode, 0)
        self.assertFalse(self.trace.exists())

    def test_locked_output_is_preserved(self):
        result_path = self.folder / 'result.json'
        result_path.write_text('previous result', encoding='utf-8')
        Path(str(result_path) + '.lock').mkdir()
        result = self.run_cli('--output', result_path)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result_path.read_text(), 'previous result')

    def test_oversized_input_and_source_are_rejected(self):
        input_file = self.folder / 'input.txt'
        input_file.write_bytes(b'x' * (1024 * 1024 + 1))
        self.assertNotEqual(self.run_cli('--input-file', input_file).returncode, 0)
        self.workflow.write_bytes(b'#' * (1024 * 1024 + 1))
        self.assertNotEqual(self.run_cli().returncode, 0)

    def test_deep_state_is_rejected_without_modification(self):
        raw = '[' * 100 + '0' + ']' * 100
        self.memory.write_text(raw, encoding='utf-8')
        self.assertNotEqual(self.run_cli('--memory', self.memory).returncode, 0)
        self.assertEqual(self.memory.read_text(), raw)

if __name__ == '__main__':
    unittest.main(verbosity=2)
