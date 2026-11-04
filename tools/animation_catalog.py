#!/usr/bin/env python3
"""Compile audited Lua literals to data; never import or execute upstream code.

Regeneration is explicit. --check verifies the checked-in catalog and snapshot
hashes without changing files. See the packaged adaptation manifest.
"""
import argparse
import ast
import copy
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/'package/contents/data/animations'
UPSTREAM=DATA/'upstream'
EVENTS=['open','close','rearrange','drop','float']
NUM=r'[-+]?(?:\d*\.\d+|\d+\.?\d*)'
def bezier(points,duration=250):
    return dict(type='bezier',x1=points[0],y1=points[1],x2=points[2],y2=points[3],duration=duration)
def spring(omega):return dict(type='spring',mass=1,stiffness=omega*omega,damping=omega*2)
def profile(curve,**kwargs):
    return dict(enabled=True,inherit=False,curve=curve,fade=False,style='pop',origin='tile',direction='nearest',size=.65,**kwargs)
def parse(name):
    text=(UPSTREAM/name).read_text()
    text=re.sub(r'--[^\n]*','',text)
    curves={'default':bezier([0,.75,.15,1])}
    for m in re.finditer(r'hl\.curve\(\s*"([^"]+)"\s*,\s*\{(.*?)\}\s*\)',text,re.S):
        key,body=m.groups()
        if '"bezier"' in body:
            points=re.search(r'points\s*=\s*\{\s*\{\s*('+NUM+r')\s*,\s*('+NUM+r')\s*\}\s*,\s*\{\s*('+NUM+r')\s*,\s*('+NUM+r')\s*\}',body)
            if not points:raise ValueError((name,key,'Unparsed bezier'))
            curves[key]=bezier([float(v) for v in points.groups()])
        else:
            values={k:float(v) for k,v in re.findall(r'(mass|stiffness|damping|dampening)\s*=\s*('+NUM+r')',body)}
            if 'dampening' in values:values['damping']=values.pop('dampening')
            assert set(values)=={'mass','stiffness','damping'},(name,key,values)
            curves[key]=dict(type='spring',**values)
    animations={}
    for body in re.findall(r'hl\.animation\(\s*\{(.*?)\}\s*\)',text,re.S):
        values=dict(re.findall(r'(leaf|bezier|spring|style)\s*=\s*"([^"]*)"',body))
        if not values:continue
        speed=re.search(r'speed\s*=\s*(?:prod\()?('+NUM+r')',body)
        enabled=re.search(r'enabled\s*=\s*(true|false)',body)
        animations[values.pop('leaf')]=dict(values,speed=float(speed[1])*100 if speed else None,enabled=not enabled or enabled[1]=='true')
    def resolve(leaf):
        parent={'windows':'global','windowsIn':'windows','windowsOut':'windows','windowsMove':'windows','fade':'global','fadeIn':'fade','fadeOut':'fade'}
        result=dict(speed=800,bezier='default',style='',enabled=True)
        if leaf in parent:result.update(resolve(parent[leaf]))
        own=animations.get(leaf,{})
        if 'spring' in own:result.pop('bezier',None)
        if 'bezier' in own:result.pop('spring',None)
        result.update({k:v for k,v in own.items() if v is not None})
        return result
    def curve(values):
        out=copy.deepcopy(curves[values.get('spring',values.get('bezier','default'))])
        # A spring has no duration; keep the source speed in provenance only.
        if out['type']=='bezier':out['duration']=values['speed']
        return out
    result={}
    for event,leaf in [('open','windowsIn'),('close','windowsOut'),('rearrange','windowsMove')]:
        values=resolve(leaf);style=values['style'];p=profile(curve(values));p['enabled']=values['enabled']
        p['style']='slide' if style.startswith('slide') else 'pop'
        p['size']=float(re.search(r'('+NUM+r')%',style)[1])/100 if '%' in style else 0
        p['direction']=next((s for s in ['top','bottom','left','right'] if s in style),'nearest')
        if event in ['open','close']:
            fade=resolve('fadeIn' if event=='open' else 'fadeOut');p.update(fade=fade['enabled'],opacity=curve(fade))
        result[event]=p
    for event in ['drop','float']:result[event]=dict(copy.deepcopy(result['rearrange']),inherit=True)
    return result,curves

def build():
    profiles={e:profile(spring(32)) for e in EVENTS}
    profiles['open'].update(curve=spring(28),origin='desktop',fade=True,opacity=bezier([.33,1,.68,1],140))
    profiles['close'].update(curve=bezier([.32,0,.67,0],140),size=.9,fade=True,opacity=bezier([0,0,1,1],120))
    profiles['drop']['curve']=spring(36);profiles['float']['curve']=spring(28)
    presets=[dict(id='fluid',name='Fluid — default',description='Responsive, controlled motion. Opens from the desktop center.',source='Fluid Tiles',profiles=profiles)]
    recipes=[('hyprland','Hyprland Example','hyprland-hyprland.lua'),('hyde-fast','HyDE Fast','hyde-fast.lua'),('hyde-optimized','HyDE Optimized','hyde-optimized.lua'),('popin-soft','Popin Soft — HyDE / ML4W','hyde-end4.lua'),('hyde-macos','HyDE macOS','hyde-macos.lua'),('ml4w-dynamic','ML4W Dynamic','ml4w-animations-dynamic.lua')]
    library={}
    for ident,name,file in recipes:
        p,c=parse(file)
        presets.append(dict(id=ident,name=name,description='Adapted for Fluid Tiles. Original window curves and timing; KDE owns workspace and shell effects.',source=file,profiles=p))
        for key,value in c.items():library[ident+'/'+key]=value
    # Literal data only: do not execute the copied Python module.
    tree=ast.parse((UPSTREAM/'hyprmod-bezier_presets.py').read_text())
    literals=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.AnnAssign) and isinstance(n.target,ast.Name) and n.target.id=='BUILTIN_PRESETS')
    assert len(literals)==19
    library.update({name:bezier(values) for name,values in literals.items()})
    classic={e:profile(dict(spring(24),classic=True)) for e in EVENTS}
    classic['open']['origin']='desktop';classic['close']['style']='system'
    for event in ['drop','float']:classic[event]['inherit']=True
    presets.append(dict(id='classic',name='Classic',description='Previous Fluid Tiles spring; KDE handles closing.',source='Fluid Tiles 0.5.4',profiles=classic))
    off=copy.deepcopy(profiles)
    for p in off.values():p.update(enabled=False,fade=False)
    presets.append(dict(id='off',name='Off',description='Immediate managed-window transitions.',source='Fluid Tiles',profiles=off))
    return dict(version=1,presets=presets,curves=library,provenance=json.loads((UPSTREAM/'manifest.json').read_text()))

if __name__=='__main__':
    args=argparse.ArgumentParser();args.add_argument('--check',action='store_true');check=args.parse_args().check
    for source in json.loads((UPSTREAM/'manifest.json').read_text()):
        assert hashlib.sha256((UPSTREAM/source['file']).read_bytes()).hexdigest()==source['sha256'],source['file']
    result=build();path=DATA/'catalog.json'
    if check:assert json.loads(path.read_text())==result,'Catalog differs from pinned source literals'
    else:path.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
    print('Verified 9 presets, 19 HyprMod easings and pinned snapshot hashes')
