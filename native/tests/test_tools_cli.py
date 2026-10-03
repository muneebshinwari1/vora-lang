"""End-to-end controlled tool workflow tests, no model required."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
NATIVE = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('VORA_TEST_EXE', NATIVE / ('dist/vora.exe' if os.name == 'nt' else 'dist/vora')))

def step(name, tool, args):
    return f'{name} = {tool}({json.dumps(args)})\n'

class ToolCLI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.workspace = self.root / 'work'
        self.workspace.mkdir()
        (self.workspace / 'notes.txt').write_text('alpha beta\nalpha gamma\n', encoding='utf-8')
        self.workflow = self.root / 'audit.vora'
        self.trace = self.root / 'trace.json'
        self.checkpoint = self.root / 'checkpoint.json'
        self.workflow.write_text('workflow Audit(input):\ntool reader = "read_file"\ntool stats = "text_stats"\n' +
            step('source', 'reader', {'path':'{input}'}) + step('counts', 'stats', {'text':'{source}'}) + 'return counts\n', encoding='utf-8')

    def tearDown(self):
        self.tmp.cleanup()

    def invoke(self, *options, input_text='notes.txt'):
        return subprocess.run([str(EXE), 'run', str(self.workflow), '--input', input_text, *map(str, options)],
            capture_output=True, text=True, timeout=15)

    def test_real_tools_and_trace_without_ai(self):
        result = self.invoke('--allow-tools','read_file,text_stats','--workspace',self.workspace,'--trace',self.trace)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['words'], 4)
        self.assertIn('no AI inference', result.stderr)
        events = json.loads(self.trace.read_text())
        self.assertEqual(sum(e['event']=='tool_completed' for e in events), 2)
        self.assertFalse(any('notes.txt' in json.dumps(e) for e in events))

    def test_denied_grant_and_missing_root(self):
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertNotEqual(self.invoke('--allow-tools','text_stats','--workspace',self.workspace).returncode, 0)
        self.assertNotEqual(self.invoke('--allow-tools','read_file,text_stats').returncode, 0)

    def test_traversal_and_absolute_input_rejected(self):
        options = ['--allow-tools','read_file,text_stats','--workspace',self.workspace]
        self.assertNotEqual(self.invoke(*options,input_text='../audit.vora').returncode, 0)
        self.assertNotEqual(self.invoke(*options,input_text=str(self.workflow)).returncode, 0)

    def test_resumed_tools_use_snapshot_and_bind_grants(self):
        options = ['--allow-tools','read_file,text_stats','--workspace',self.workspace]
        result = self.invoke(*options,'--checkpoint',self.checkpoint)
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.workspace / 'notes.txt').unlink()
        resumed = self.invoke(*options,'--resume',self.checkpoint,'--trace',self.trace)
        self.assertEqual(resumed.returncode, 0, resumed.stderr)
        self.assertEqual(resumed.stdout,result.stdout)
        self.assertFalse(any(e['event']=='tool_started' for e in json.loads(self.trace.read_text())))
        self.assertNotEqual(self.invoke('--allow-tools','read_file,text_stats,list_files','--workspace',self.workspace,'--resume',self.checkpoint).returncode, 0)

    def test_tools_memory_and_resume_together(self):
        source = self.workflow.read_text().replace('return counts\n',
            'agent reviewer = "summarize"\nreport = reviewer("Counts: {counts}")\nreturn report\n')
        self.workflow.write_text(source, encoding='utf-8')
        memory = self.root / 'memory.json'
        options = ['--provider','demo','--allow-tools','read_file,text_stats','--workspace',self.workspace,'--memory',memory]
        first = self.invoke(*options,'--checkpoint',self.checkpoint)
        self.assertEqual(first.returncode,0,first.stderr)
        saved_memory = memory.read_bytes()
        self.assertEqual(set(json.loads(saved_memory)['agents']), {'reviewer'})
        (self.workspace / 'notes.txt').unlink()
        second = self.invoke(*options,'--resume',self.checkpoint,'--trace',self.trace)
        self.assertEqual(second.returncode,0,second.stderr)
        self.assertEqual(second.stdout,first.stdout)
        self.assertEqual(memory.read_bytes(),saved_memory)
        self.assertFalse(any(e['event']=='step_started' for e in json.loads(self.trace.read_text())))

    def test_shared_budget_blocks_too_small_run(self):
        result = self.invoke('--allow-tools','read_file,text_stats','--workspace',self.workspace,'--max-calls','1')
        self.assertNotEqual(result.returncode, 0)

    def test_quoted_input_stays_data(self):
        self.workflow.write_text('workflow Pure(input):\ntool stats = "text_stats"\n'+step('counts','stats',{'text':'{input}'})+'return counts\n',encoding='utf-8')
        value = 'quote ", "path":"../secret" {missing}'
        result = self.invoke('--allow-tools','text_stats',input_text=value)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['bytes'],len(value.encode()))

if __name__ == '__main__':
    unittest.main(verbosity=2)
