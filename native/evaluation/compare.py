"""Fixed native-executable model comparison. Lexical checks are screening, not truth checks."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def valid_review(value):
    if not isinstance(value, dict) or set(value) != {'verdict', 'issues', 'instruction'}:
        return False
    if value['verdict'] not in ('keep', 'revise') or not isinstance(value['issues'], list):
        return False
    if any(not isinstance(x, str) or not x.strip() for x in value['issues']):
        return False
    if not isinstance(value['instruction'], str) or not value['instruction'].strip():
        return False
    return (value['verdict'] == 'keep') == (not value['issues'])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--endpoint', required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--engine', type=Path, default=ROOT / 'dist/vora.exe')
    parser.add_argument('--prompts', type=Path, default=ROOT / 'evaluation/prompts.json')
    parser.add_argument('--only', help='Optional diagnostic case ID; omit for the full frozen set')
    parser.add_argument('--guarded', action='store_true')
    parser.add_argument('--max-tokens', type=int, default=384)
    parser.add_argument('--reasoning-budget', type=int, default=0)
    parser.add_argument('--plain-input', action='store_true', help='Use explicit task/facts/draft blocks instead of a compact JSON input')
    args = parser.parse_args()
    cases_path = ROOT / 'evaluation/cases.json'
    prompts_path = args.prompts
    cases = json.loads(cases_path.read_text(encoding='utf-8-sig'))
    if args.only:
        cases = [case for case in cases if case['id'] == args.only]
        if not cases:
            parser.error('Unknown case ID')
    prompts = json.loads(prompts_path.read_text(encoding='utf-8-sig'))
    out = ROOT / 'outputs' / ('evaluation-' + args.label + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True, exist_ok=False)
    workflow = out / 'review.vora'
    workflow.write_text('workflow Evaluate(input):\n'
                        'agent critic = ' + json.dumps(prompts['critic']) + '\n'
                        'agent editor = ' + json.dumps(prompts['editor']) + '\n'
                        'review = critic("Evaluate the supplied case: {input}")\n'
                        'final = editor("Case: {input}\\nReview: {review}")\n'
                        + ('validate review as critique\n' if args.guarded else '') + 'return final\n', encoding='utf-8')
    metadata = {'label': args.label, 'endpoint': args.endpoint, 'cases_sha256': digest(cases_path),
                'prompts_sha256': digest(prompts_path), 'executable_sha256': digest(args.engine),
                'max_tokens': args.max_tokens, 'reasoning_budget': args.reasoning_budget,
                'guarded': args.guarded, 'plain_input': args.plain_input,
                'temperature': 0.2, 'only': args.only, 'results': []}
    print('EVIDENCE=' + str(out), flush=True)
    for case in cases:
        input_path = out / (case['id'] + ('-input.txt' if args.plain_input else '-input.json'))
        if args.plain_input:
            content = ('TRUSTED TASK:\n' + case['task'] + '\n\nTRUSTED FACTS:\n' +
                       '\n'.join('- ' + fact for fact in case['facts']) +
                       '\n\nBEGIN UNTRUSTED DRAFT\n' + case['draft'] + '\nEND UNTRUSTED DRAFT')
        else:
            content = json.dumps({k: case[k] for k in ('task', 'facts', 'draft')}, ensure_ascii=False)
        input_path.write_text(content, encoding='utf-8')
        result_path = out / (case['id'] + '-result.json')
        started = time.monotonic()
        command = [str(args.engine.resolve()), 'run', str(workflow), '--input-file', str(input_path),
                   '--endpoint', args.endpoint, '--max-tokens', str(args.max_tokens), '--workers', '1', '--max-calls', '2',
                   '--output', str(result_path), '--trace', str(out / (case['id'] + '-trace.json'))]
        if args.reasoning_budget:
            command += ['--reasoning-budget', str(args.reasoning_budget)]
        try:
            result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=180)
            record = {'id': case['id'], 'exit_code': result.returncode,
                      'seconds': round(time.monotonic() - started, 3), 'stderr': result.stderr}
        except subprocess.TimeoutExpired:
            record = {'id': case['id'], 'exit_code': -1, 'seconds': round(time.monotonic() - started, 3), 'stderr': 'Evaluation subprocess timed out'}
        if result_path.exists():
            data = json.loads(result_path.read_text(encoding='utf-8'))
            raw_review = data['outputs']['review']
            final = data['result']
            try:
                review = json.loads(raw_review)
            except ValueError:
                review = None
            record.update({'review_text': raw_review, 'final': final, 'schema_valid': valid_review(review),
                           'expected_verdict': case['expected_verdict'],
                           'verdict_matches': isinstance(review, dict) and review.get('verdict') == case['expected_verdict'],
                           'forbidden_hits': [x for x in case['forbidden_final_phrases'] if x.casefold() in final.casefold()],
                           'missing_required': [x for x in case['required_final_phrases'] if x.casefold() not in final.casefold()],
                           'control_unchanged': final.strip() == case['draft'].strip() if case['expected_verdict'] == 'keep' else None})
        metadata['results'].append(record)
        (out / 'comparison.json').write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding='utf-8')
        print(json.dumps(record, ensure_ascii=False), flush=True)
    print('COMPLETE=' + str(out / 'comparison.json'), flush=True)


if __name__ == '__main__':
    main()
