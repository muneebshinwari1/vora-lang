"""Export a native Vora trace as a self-contained offline HTML viewer."""
import argparse
import json
from pathlib import Path

MAX_BYTES = 8 * 1024 * 1024

def render(events):
    if not isinstance(events, list) or len(events) > 50000:
        raise ValueError('Expected an array of at most 50000 events')
    for event in events:
        if not isinstance(event, dict) or not isinstance(event.get('event'), str):
            raise ValueError('Every event must have a string event name')
        elapsed = event.get('elapsed_ms', 0)
        if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or not 0 <= elapsed <= 1e15:
            raise ValueError('Invalid elapsed_ms')
    data = json.dumps(events, ensure_ascii=True, allow_nan=False).replace('<', '\\u003c').replace('>', '\\u003e').replace('&', '\\u0026')
    return TEMPLATE.replace('EVENT_DATA', data)

TEMPLATE = '''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'none'; img-src 'none'">
<title>Vora trace viewer</title>
<style>body{font:16px system-ui;background:#111827;color:#e5e7eb;margin:32px auto;max-width:1100px;padding:0 20px}h1{color:#67e8f9}input,select,button{font:inherit;padding:9px;background:#1f2937;color:white;border:1px solid #64748b;border-radius:6px;margin:4px}table{width:100%;border-collapse:collapse}td,th{text-align:left;padding:10px;border-bottom:1px solid #374151;vertical-align:top}td:last-child{white-space:pre-wrap;overflow-wrap:anywhere}tr.failed{background:#481e28}small{color:#9ca3af}#summary{padding:16px;background:#1f2937;border-radius:8px;margin:20px 0}label{display:inline-block}progress{width:110px;accent-color:#22d3ee}</style>
<h1>Vora execution trace</h1><p>Offline event inspector. Times are relative to each run; overlapping calls may finish out of order.</p>
<div id="summary"></div>
<label>Search <input id="search" type="search" placeholder="Step, event or detail"></label>
<label>Event <select id="kind"><option value="">All events</option></select></label>
<label><input id="errors" type="checkbox">Failures only</label>
<button id="reset">Reset filters</button><p id="count" aria-live="polite"></p>
<table><thead><tr><th>Time</th><th>Event</th><th>Step</th><th>Details</th></tr></thead><tbody id="rows"></tbody></table>
<p><small>Trace files may contain sensitive metadata. This viewer makes no network requests. It displays recorded events, not a live workflow or factual-quality assessment.</small></p>
<script id="data" type="application/json">EVENT_DATA</script>
<script>
const events=JSON.parse(document.getElementById('data').textContent);
const byId=id=>document.getElementById(id), failed=e=>/failed|exhausted/.test(e.event);
const calls=events.filter(e=>e.event==='step_started').length;
const runs=new Set(events.map(e=>e.run_id).filter(Boolean));
byId('summary').textContent=`${runs.size} run(s) · ${events.length} events · ${calls} call attempts · ${events.filter(failed).length} failure events · ${events.filter(e=>e.event==='step_skipped').length} skipped calls`;
for(const kind of [...new Set(events.map(e=>e.event))].sort()){const option=document.createElement('option');option.value=kind;option.textContent=kind;byId('kind').append(option)}
function draw(){const q=byId('search').value.toLowerCase(),kind=byId('kind').value;
const selected=events.filter(e=>(!kind||e.event===kind)&&(!byId('errors').checked||failed(e))&&JSON.stringify(e).toLowerCase().includes(q));
byId('rows').replaceChildren();byId('count').textContent=`${selected.length} matching events; displaying first ${Math.min(selected.length,1000)}. Narrow filters for larger traces.`;
for(const e of selected.slice(0,1000)){const tr=document.createElement('tr');if(failed(e))tr.className='failed';
const detail={...e};delete detail.event;delete detail.step;delete detail.elapsed_ms;
for(const value of [`${e.elapsed_ms||0} ms`,e.event,e.step||'—',JSON.stringify(detail,null,2)]){const td=document.createElement('td');td.textContent=value;tr.append(td)}byId('rows').append(tr)}}
for(const id of ['search','kind','errors'])byId(id).addEventListener('input',draw);
byId('reset').onclick=()=>{byId('search').value='';byId('kind').value='';byId('errors').checked=false;draw()};draw();
</script></html>'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.trace.open('rb') as source:
        raw = source.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        parser.error('Trace exceeds 8 MiB')
    try:
        html = render(json.loads(raw.decode('utf-8-sig')))
    except (ValueError, RecursionError) as error:
        parser.error(str(error))
    # Exclusive creation prevents accidental replacement of source/state/user files.
    with args.output.open('x', encoding='utf-8') as output:
        output.write(html)
    print(args.output.resolve())

if __name__ == '__main__':
    main()
