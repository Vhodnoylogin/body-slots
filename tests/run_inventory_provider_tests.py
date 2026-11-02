"""Actual public Body Slots/HIGGS inventory-provider protocol; no game/engine SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build import vs_environment
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--higgs-root',type=Path,required=True);parser.add_argument('--build-dir',type=Path,required=True);args=parser.parse_args();out=args.build_dir.resolve();higgs=args.higgs_root.resolve()
    if out==ROOT or ROOT in out.parents:parser.error('External output required')
    out.mkdir(parents=True,exist_ok=True);env=vs_environment();cl=shutil.which('cl.exe',path=env.get('Path',env.get('PATH')))
    cmake=(ROOT/'CMakeLists.txt').read_text();core=[ROOT/p for p in re.search(r'set\(BS_CORE ([^)]+)\)',cmake)[1].split()]
    closure=higgs/'cmake/HiggsServices.cmake';services=[higgs/p for p in re.findall(r'"\$\{HIGGS_SERVICE_ROOT\}/(src/[^"\n]+\.cpp)"',closure.read_text())]
    helper=ROOT/'examples/ordinary_inventory_provider.cpp';test=ROOT/'tests/inventory_provider_tests.cpp'
    service_headers=[higgs/p for p in re.findall(r'"\$\{HIGGS_SERVICE_ROOT\}/(include/[^"\n]+\.h)"',closure.read_text())]
    paths=list(dict.fromkeys(core+services+service_headers+[helper,test,closure,ROOT/'examples/ordinary_inventory_provider.h',ROOT/'CMakeLists.txt',Path(__file__)]+list((ROOT/'include').glob('*.h'))+list((ROOT/'src').glob('*.h'))+list((higgs/'include').glob('higgs*.h'))))
    pins={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths};commands=[]
    def run(name,argv):
        argv=list(map(str,argv));p=subprocess.run(argv,cwd=out,env=env,capture_output=True);(out/(name+'.log')).write_bytes(p.stdout+p.stderr);commands.append({'argv':argv,'exitCode':p.returncode})
        if p.returncode:raise RuntimeError(name+' failed; see '+str(out/(name+'.log')))
        print(name+': passed',flush=True)
    includes=[f'/I{ROOT/"include"}',f'/I{ROOT/"src"}',f'/I{ROOT/"examples"}',f'/I{higgs/"include"}']
    flags=[cl,'/nologo','/EHsc','/O2','/W4','/permissive-','/D_CRT_SECURE_NO_WARNINGS',*includes]
    contracts=out/'public-higgs-contracts';contracts.mkdir(exist_ok=True)
    for name in ['higgshandapi.h','higgshandrouting.h','higgsinventory.h','higgstransactions.h']:
        shutil.copyfile(higgs/'include'/name,contracts/name)
    sdk_flags=[cl,'/nologo','/EHsc','/O2','/W4','/WX','/permissive-',f'/I{ROOT/"include"}',f'/I{ROOT/"examples"}',f'/I{contracts}']
    obj=out/'ordinary_provider.obj';run('sdk-compile',[*sdk_flags,'/std:c++17','/c',helper,f'/Fo{obj}'])
    exe=out/'inventory_provider_tests.exe';run('compile',[*flags,'/std:c++20',*core,*services,test,obj,f'/Fe{exe}','/link','/STACK:8388608']);run('protocol',[exe])
    for p in paths:
        if hashlib.sha256(p.read_bytes()).hexdigest()!=pins[str(p)]:raise RuntimeError('Source changed: '+str(p))
    (out/'inventory-provider-manifest.json').write_text(json.dumps({'commands':commands,'sourceSha256':pins,'gameStarts':0,'qualification':'Actual public tables and Body Slots/HIGGS inventory/transaction coordinators with explicit fake native engine/physical observations. No gameplay, SKSE engine or rendering qualification.'},indent=2)+'\n')
if __name__=='__main__':main()
