"""Closing/removing a third tile yields side-by-side pairs in real KWin."""
import hashlib
import json
import subprocess
import time


def run_pair_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    report={}
    def tiles():return [w for w in tiled() if w['visible']]
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def activate(ident):
        call('TestWindow',ident,'activate');wait_for(lambda:status()['activeWindow']==ident)
    def apply(values):
        subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps(values)],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
    def spawn(label,kind=''):
        p=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#21618c','0',kind],env=env,stdout=log,stderr=log);children.append(p)
        ident=wait_for(lambda:next((w['id'] for w in tiles() if w['title'].endswith(label)),None));wait_for(settled)
        return p,ident
    def side_by_side():
        pair=tiles()
        if len(pair)!=2:return False
        a,b=sorted(pair,key=lambda w:w['target'][0]);x,y,width,height=a['target'];ox,oy,ow,oh=b['target']
        return abs(y-oy)<1 and abs(height-oh)<1 and ox>x+width and all(not w['redirected'] for w in pair)
    def rows():
        pair=tiles()
        return len(pair)==2 and abs(pair[0]['target'][0]-pair[1]['target'][0])<1 and abs(pair[0]['target'][2]-pair[1]['target'][2])<1
    def close(p,ident):
        p.terminate();p.wait(timeout=5);wait_for(lambda:all(w['id']!=ident for w in status()['windows']));wait_for(settled)
    def triple(round_):
        group={}
        for i in range(3):
            p,ident=spawn('Pair '+str(round_)+' '+str(i));group[ident]=p
        assert len(tiles())==3
        return group
    apply({'AppearanceEnabled':True,'SpringFrequency':12});wait_for(lambda:status()['appearance']['enabled'] and status()['springFrequency']==12)
    for round_,which in enumerate(['main','upper','lower','swapped-main','close-during-resize']):
        group=triple(round_);main=max(tiles(),key=lambda w:w['target'][3])
        stacked=sorted([w for w in tiles() if w['id']!=main['id']],key=lambda w:w['target'][1])
        assert main['target'][3]>stacked[0]['target'][3]*1.5
        if which=='swapped-main':
            activate(main['id']);direction='right' if main['target'][0]<stacked[-1]['target'][0] else 'left'
            call('Action','swap-'+direction);wait_for(settled)
            main=max(tiles(),key=lambda w:w['target'][3]);stacked=sorted([w for w in tiles() if w['id']!=main['id']],key=lambda w:w['target'][1])
        victim=stacked[0] if which=='upper' else stacked[1] if which=='lower' else main
        restore={w['id']:w['restore'] for w in tiles() if w['id']!=victim['id']}
        if which=='close-during-resize':
            w=stacked[0];x,y,width,height=w['actual'];origin=(x+width*.7,y+height*.7)
            assert call('TestPointer','resize',*origin)=='true';call('TestPointer','motion',origin[0]-70,origin[1]+35)
        close(group.pop(victim['id']),victim['id']);wait_for(side_by_side)
        assert not status()['dragging']
        assert all(get(i)['restore']==rect for i,rect in restore.items())
        if which in ['main','swapped-main']:
            pair=sorted(tiles(),key=lambda w:w['target'][0])
            assert [w['id'] for w in pair]==[w['id'] for w in stacked],'Former top/bottom pair lost its relative order'
        report['close_'+which]='passed'
        for ident,p in group.items():close(p,ident)

    group=triple('other-removals')
    for operation in ['minimize','float','desktop']:
        main=max(tiles(),key=lambda w:w['target'][3]);ident=main['id'];activate(ident)
        if operation=='minimize':call('TestWindow',ident,'minimize')
        elif operation=='float':call('Action','float')
        else:call('TestWindow',ident,'desktop-2')
        wait_for(lambda:len(tiles())==2);wait_for(settled);wait_for(side_by_side)
        if operation=='minimize':call('TestWindow',ident,'restore')
        elif operation=='float':activate(ident);call('Action','float')
        else:call('TestWindow',ident,'desktop-1')
        wait_for(lambda:len(tiles())==3);wait_for(settled)
        report[operation+'_leaves_side_by_side_pair']='passed'
    main=max(tiles(),key=lambda w:w['target'][3]);close(group.pop(main['id']),main['id']);wait_for(side_by_side)
    activate(tiles()[0]['id']);assert call('Action','split')=='true';wait_for(settled);assert rows()
    # Ordinary reconciles after a color save must respect an explicit rotation.
    apply({'UseSystemBorderColors':False,'ActiveBorderColor':'#12abef'});time.sleep(.2);wait_for(settled);assert rows()
    activate(tiles()[0]['id']);assert call('Action','split')=='true';wait_for(settled);assert side_by_side()
    report['explicit_rotate_override_survives_config_reconcile']='passed'
    for ident,p in group.items():close(p,ident)

    # Preserve two legal tiles if native minima make columns impossible.
    a,ia=spawn('Pair wide A','wide-minimum');b,ib=spawn('Pair wide B','wide-minimum')
    wait_for(lambda:len(tiles())==2);wait_for(settled)
    assert rows() and not get(ia)['capacityBlocked'] and not get(ib)['capacityBlocked']
    assert all(w['actual'][2]>=950-1 for w in tiles())
    report['native_minimum_width_fallback_keeps_both_tiles']='passed'
    close(a,ia);close(b,ib)
    data={'pass':True,'cases':report,'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
    (results/'pairs.json').write_text(json.dumps(data,indent=2)+'\n')
    return report
