"""Real-model sustained load test. Developer tooling requires psutil.
Never runs in CI by default. Outputs logs/metrics under the selected directory.
"""
import argparse, concurrent.futures, json, pathlib, subprocess, time
import psutil
p=argparse.ArgumentParser()
p.add_argument('--exe',required=True)
p.add_argument('--workflow',required=True)
p.add_argument('--output-dir',required=True)
p.add_argument('--seconds',type=int,default=900)
p.add_argument('--concurrency',type=int,default=1)
p.add_argument('--http-timeout-ms',type=int,default=120000)
a=p.parse_args()
if not 1<=a.concurrency<=8 or a.seconds<1 or not 1<=a.http_timeout_ms<=120000: p.error('invalid load limits')
out=pathlib.Path(a.output_dir);out.mkdir(parents=True,exist_ok=False)
start=time.monotonic();rows=[]
servers=[]
for conn in psutil.net_connections(kind='tcp'):
    if conn.status=='LISTEN' and conn.laddr.port==18080 and conn.pid:
        proc=psutil.Process(conn.pid)
        servers.append(dict(pid=conn.pid,exe=proc.exe(),initial_rss=proc.memory_info().rss))
def worker(slot):
    count=0;done=[]
    while time.monotonic()-start<a.seconds:
        name=f'{slot}-{count}';count+=1
        cmd=[a.exe,'run',a.workflow,'--input',f'Write a short notice: Vora workflows need review before use. Job {name}.','--max-tokens','128','--max-calls','3','--http-timeout-ms',str(a.http_timeout_ms),'--checkpoint',str(out/(name+'-state.json')),'--trace',str(out/(name+'-trace.json')),'--output',str(out/(name+'-result.json'))]
        started=time.monotonic();peak=0;timed_out=False
        with open(out/(name+'.log'),'w') as log:
            proc=subprocess.Popen(cmd,stdout=log,stderr=log)
            while proc.poll() is None:
                try: peak=max(peak,psutil.Process(proc.pid).memory_info().rss)
                except psutil.Error: pass
                if time.monotonic()-started>400:
                    timed_out=True;proc.kill();proc.wait();break
                time.sleep(.05)
        valid=False;calls=None
        if proc.returncode==0:
            try:
                state=json.loads((out/(name+'-state.json')).read_text())
                trace=json.loads((out/(name+'-trace.json')).read_text())
                result=json.loads((out/(name+'-result.json')).read_text())
                calls=state['calls']
                valid=calls==3 and len(state['outputs'])==3 and trace[-1]['event']=='workflow_completed' and isinstance(result.get('result'),str) and bool(result['result'].strip())
            except (ValueError,KeyError,OSError): pass
        row=dict(completed_at=round(time.monotonic()-start,3),job=name,slot=slot,exit_code=proc.returncode,seconds=round(time.monotonic()-started,3),peak_engine_rss=peak,valid=valid,calls=calls,timed_out=timed_out)
        done.append(row)
        with open(out/('progress-'+str(slot)+'.jsonl'),'a') as f:f.write(json.dumps(row)+'\n')
    return done
with concurrent.futures.ThreadPoolExecutor(max_workers=a.concurrency) as pool:
    for done in pool.map(worker,range(a.concurrency)):rows.extend(done)
for server in servers:
    try:server['final_rss']=psutil.Process(server['pid']).memory_info().rss
    except psutil.Error:server['final_rss']=None
rows.sort(key=lambda r:r['completed_at'])
lat=sorted(r['seconds'] for r in rows)
summary=dict(duration_seconds=round(time.monotonic()-start,3),requested_seconds=a.seconds,concurrency=a.concurrency,http_timeout_ms=a.http_timeout_ms,jobs=len(rows),failures=sum(not r['valid'] for r in rows),p95_seconds=lat[max(0,(95*len(lat)+99)//100-1)],max_engine_rss=max(r['peak_engine_rss'] for r in rows),servers=servers,first_quarter_mean_rss=sum(r['peak_engine_rss'] for r in rows[:max(1,len(rows)//4)])/max(1,len(rows)//4),last_quarter_mean_rss=sum(r['peak_engine_rss'] for r in rows[-max(1,len(rows)//4):])/max(1,len(rows)//4),limitations='Fresh CLI process per job; engine RSS sampled at 50ms; model-server RSS endpoints only; factual quality not graded',rows=rows)
(out/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='rows'}),flush=True)
raise SystemExit(bool(summary['failures']))
