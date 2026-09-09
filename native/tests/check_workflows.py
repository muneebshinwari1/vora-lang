"""Real-model acceptance checks for the native executable; Python is test tooling only."""
import datetime
import json
from pathlib import Path
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'outputs' / ('checking-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
OUT.mkdir(parents=True, exist_ok=False)
EXE = ROOT / 'dist/vora.exe'
records = {}


def invoke(name, source, task, *extra):
    workflow = OUT / (name + '.vora')
    workflow.write_text(source, encoding='utf-8', newline='')
    result_path = OUT / (name + '-result.json')
    trace_path = OUT / (name + '-trace.json')
    process = subprocess.run([str(EXE), 'run', str(workflow), '--input', task,
                              '--max-tokens', '256', '--workers', '2',
                              '--output', str(result_path), '--trace', str(trace_path), *extra],
                             capture_output=True, text=True, encoding='utf-8', timeout=120)
    record = {'exit_code': process.returncode, 'task': task, 'stdout': process.stdout,
              'stderr': process.stderr, 'workflow': workflow.name}
    if result_path.exists():
        record['data'] = json.loads(result_path.read_text(encoding='utf-8'))
    records[name] = record
    print(json.dumps({'check': name, **record}, ensure_ascii=False), flush=True)
    (OUT / 'checks.json').write_text(json.dumps(records, indent=2, ensure_ascii=False), encoding='utf-8')
    return record


print('EVIDENCE_FOLDER=' + str(OUT), flush=True)
invoke('new-task', (ROOT / 'quickstart.vora').read_text(encoding='utf-8'),
       'Write a two-sentence welcome message for a beginner coding class. Mention that mistakes are part of learning.')

role_template = '''workflow RoleCheck(input):
agent writer = ROLE
draft = writer("Describe this item: {input}")
return draft
'''
for name, role in (
    ('role-factual', 'You are a technical writer. Respond with exactly two bullet points describing practical features. No metaphors or slogans.'),
    ('role-poetic', 'You are a poet. Write exactly two short rhyming lines about the item. Do not use bullet points.'),
):
    invoke(name, role_template.replace('ROLE', json.dumps(role)), 'A reusable steel water bottle')

parallel = invoke('parallel', '''workflow ParallelCheck(input):
agent describer = "Describe the item in two short sentences."
agent reviewer = "Give two practical limitations to consider, in two short sentences."
agent writer = "Combine the description and limitations in one balanced short paragraph."
description = describer("Item: {input}")
limitations = reviewer("Item: {input}")
summary = writer("Description: {description}\\nLimitations: {limitations}")
return summary
''', 'A reusable steel water bottle')
if parallel.get('data'):
    events = parallel['data']['events']
    started = {e['step']: e['elapsed_ms'] for e in events if e['event'] == 'step_started'}
    finished = {e['step']: e['elapsed_ms'] for e in events if e['event'] == 'step_completed'}
    parallel['overlap_ms'] = min(finished['description'], finished['limitations']) - max(started['description'], started['limitations'])
    parallel['join_after_both'] = started['summary'] >= max(finished['description'], finished['limitations'])

requests = []
class Spy(BaseHTTPRequestHandler):
    def do_POST(self):
        requests.append(self.path)
        self.send_response(500)
        self.end_headers()
    def log_message(self, *args):
        pass

server = ThreadingHTTPServer(('127.0.0.1', 0), Spy)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    invalid = invoke('invalid-reference', '''workflow Invalid(input):
agent writer = "Write a response"
draft = writer("Process {missing}")
return draft
''', 'Test', '--endpoint', f'http://127.0.0.1:{server.server_port}/v1/chat/completions')
    invalid['observed_provider_requests'] = len(requests)
finally:
    server.shutdown()
    server.server_close()
    thread.join(2)

invoke('critic-quality', '''workflow CriticCheck(input):
agent critic = "Review the draft. Give one concrete improvement in one short sentence."
agent editor = "Apply the critique. Return only the improved text, at most 3 short sentences."
review = critic("Task: Remove unsupported absolute promises from this product description.\\nDraft: {input}")
final = editor("Task: Remove unsupported absolute promises.\\nDraft: {input}\\nCritique: {review}")
return final
''', 'This reusable steel bottle keeps water cold forever and is guaranteed never to leak.')

(OUT / 'checks.json').write_text(json.dumps(records, indent=2, ensure_ascii=False), encoding='utf-8')
print('CHECKS_COMPLETE=' + str(OUT / 'checks.json'), flush=True)
