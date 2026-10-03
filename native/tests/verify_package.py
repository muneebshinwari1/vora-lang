"""Verify a generated archive in a fresh installation directory."""
import argparse,glob,json,os,pathlib,subprocess,tarfile,tempfile,zipfile
p=argparse.ArgumentParser();p.add_argument('pattern');a=p.parse_args()
archives=glob.glob(a.pattern)
if len(archives)!=1:raise RuntimeError('Expected exactly one generated archive')
with tempfile.TemporaryDirectory(prefix='vora-package-') as tmp:
    root=pathlib.Path(tmp)
    archive=archives[0]
    if archive.endswith('.zip'):
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if not (root/name).resolve().is_relative_to(root):raise RuntimeError('Unsafe archive path')
            z.extractall(root)
    else:
        with tarfile.open(archive) as t:t.extractall(root,filter='data')
    binaries=list(root.rglob('vora.exe' if os.name=='nt' else 'vora'))
    binaries=[b for b in binaries if b.is_file() and b.parent.name=='bin']
    if len(binaries)!=1:raise RuntimeError('Expected one installed binary')
    exe=binaries[0];installed=exe.parent.parent
    def cli(*args):
        r=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,timeout=30,cwd=tmp)
        if r.returncode:raise RuntimeError(r.stderr)
        return r
    if 'Vora 0.4.0 -' not in cli('--help').stdout:raise RuntimeError('Wrong package version')
    example=installed/'share/vora/examples/quickstart-fast.vora'
    cli('check',example);cli('plan',example)
    checkpoint=root/'state.json';output=root/'output.json';trace=root/'trace.json'
    cli('run',example,'--provider','demo','--input','package smoke','--checkpoint',checkpoint,'--output',output)
    first=json.loads(output.read_text())['result']
    cli('run',example,'--provider','demo','--input','package smoke','--resume',checkpoint,'--output',output,'--trace',trace)
    if json.loads(output.read_text())['result']!=first:raise RuntimeError('Resume changed result')
    if any(e['event']=='step_started' for e in json.loads(trace.read_text())):raise RuntimeError('Completed checkpoint made a new call')
    cli('run',installed/'share/vora/examples/file-stats.vora','--input','share/vora/SPEC.md','--workspace',installed,'--allow-tools','read_file,text_stats')
    if not (installed/'share/vora/licenses/nlohmann-json-LICENSE.MIT').is_file():raise RuntimeError('Missing third-party license')
print('Clean archive install, CLI, tool grants and checkpoint roundtrip passed')
