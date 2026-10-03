"""Release containers may publish only after exact main-commit CI succeeds."""
import json,os,re,urllib.request
revision=os.environ['REVISION'];tag=os.environ['RELEASE_TAG']
if not re.fullmatch(r'v0\.4\.\d+',tag) or not re.fullmatch(r'[0-9a-f]{40}',revision):
    raise SystemExit('Unexpected stable tag or revision')
url='https://api.github.com/repos/'+os.environ['GITHUB_REPOSITORY']+'/actions/runs?head_sha='+revision+'&per_page=100'
req=urllib.request.Request(url,headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Accept':'application/vnd.github+json','User-Agent':'vora-release-gate'})
with urllib.request.urlopen(req,timeout=30) as r:data=json.load(r)
if not any(run['name']=='CI' and run['head_sha']==revision and run['head_branch']=='main' and run['event']=='push' and run['conclusion']=='success' for run in data['workflow_runs']):
    raise SystemExit('Exact main-commit CI has not succeeded; publication refused')
print('Exact-commit release CI gate passed')
