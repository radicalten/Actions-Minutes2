#!/usr/bin/env python3
# Official Docker Hub image; extract only opt/ after SHA-256 verification.
import requests, os, hashlib, tarfile, json
from pathlib import Path
root=Path('/home/user/dkp-work'); stage=root/'stage'; stage.mkdir(exist_ok=True)
s=requests.Session()
t=s.get('https://auth.docker.io/token',params={'service':'registry.docker.io','scope':'repository:devkitpro/devkitppc:pull'}); t.raise_for_status()
s.headers['Authorization']='Bearer '+t.json()['token']
base='https://registry-1.docker.io/v2/devkitpro/devkitppc/'
h={'Accept':'application/vnd.docker.distribution.manifest.list.v2+json, application/vnd.oci.image.index.v1+json, application/vnd.docker.distribution.manifest.v2+json'}
r=s.get(base+'manifests/latest',headers=h); r.raise_for_status(); m=r.json()
if 'manifests' in m:
 d=next(x['digest'] for x in m['manifests'] if x['platform']['architecture']=='amd64' and x['platform']['os']=='linux')
 r=s.get(base+'manifests/'+d,headers=h); r.raise_for_status(); m=r.json()
(root/'manifest.json').write_text(json.dumps(m,indent=2))
for layer in m['layers']:
 digest=layer['digest'].split(':')[1]; p=root/(digest+'.tar.gz')
 print('Layer',digest,layer['size'],flush=True)
 if not p.exists():
  r=s.get(base+'blobs/'+layer['digest'],stream=True); r.raise_for_status()
  with p.open('wb') as f:
   for chunk in r.iter_content(1024*1024): f.write(chunk)
 hsh=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''): hsh.update(chunk)
 if hsh.hexdigest()!=digest: raise RuntimeError('digest mismatch')
 with tarfile.open(p) as tar:
  members=[x for x in tar if x.name.lstrip('./').startswith('opt/')]
  tar.extractall(stage,members=members)
 p.unlink()
print('EXTRACT_OK',flush=True)
