"""Reproduce insertion after swapping two windows across the screen."""
import json
import subprocess


def run_insertion_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def spawn(label):
        proc=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#21618c','0'],env=env,stdout=log,stderr=log)
        children.append(proc)
        ident=wait_for(lambda:next((w['id'] for w in status()['windows'] if w['title'].endswith(label)),None))
        wait_for(lambda:get(ident)['tiled']);wait_for(settled)
        return proc,ident
    _,a=spawn('Insertion A');_,b=spawn('Insertion B')
    call('TestWindow',a,'activate');wait_for(lambda:status()['activeWindow']==a)
    report={}
    snapshots=[]
    for round_ in range(4):
        assert len(tiled())==2
        # Keep focus on A while alternating its spatial location. Swapping
        # changes the tile position, not the identity of the focused window.
        for _ in range(3 if round_==0 else 1):
            direction='right' if get(a)['target'][0]<get(b)['target'][0] else 'left'
            assert call('Action','swap-'+direction)=='true'
            wait_for(settled)
            assert status()['activeWindow']==a
        side='left' if get(a)['target'][0]<get(b)['target'][0] else 'right'
        before_a=get(a)['target'];before_b=get(b)['target']
        proc,new=spawn('Insertion C '+str(round_))
        existing=get(a)['target'];added=get(new)['target']
        assert abs(existing[0]-added[0])<1 and abs(existing[2]-added[2])<1,(side,existing,added)
        assert added[1]>existing[1],'New window must occupy lower child of the focused tile'
        assert get(b)['target']==before_b,'Unselected tile changed during third-window insertion'
        assert abs(existing[0]-before_a[0])<1
        snapshots.append({'focused_side':side,'before':before_a,'existing':existing,'new':added})
        report['focused_'+side+'_after_swap_'+str(round_)]='passed'
        proc.terminate();proc.wait(timeout=4)
        wait_for(lambda:len(tiled())==2);wait_for(settled)
        call('TestWindow',a,'activate');wait_for(lambda:status()['activeWindow']==a)
    assert {s['focused_side'] for s in snapshots}=={'left','right'}
    (results/'insertion.json').write_text(json.dumps({'pass':True,'cases':report,'snapshots':snapshots},indent=2)+'\n')
    return report
