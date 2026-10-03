"""Persistence integration tests; Python is developer tooling only."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
EXE = NATIVE / ('dist/vora.exe' if os.name == 'nt' else 'dist/vora')

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

if __name__ == '__main__':
    unittest.main(verbosity=2)
