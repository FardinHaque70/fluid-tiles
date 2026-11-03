"""Window admission regressions from the user's real desktop reports."""
import hashlib
import json
import subprocess
import time


def run_popup_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    before={w['id']:w['target'] for w in tiled()}
    window=min(tiled(),key=lambda w:w['target'][0])
    x,y,w,h=window['actual']
    pointer=subprocess.Popen([str(root/'build/fluidtiles-input'),str(x+w-14),str(y+14),'hover-close'],
        env=env,stdout=log,stderr=log)
    try:
        popup=wait_for(lambda:next((w for w in status()['windows'] if w.get('popup') and w.get('internal')),None),3)
        assert not popup['tiled'] and not popup['eligible'],popup
        assert {w['id']:w['target'] for w in tiled()}==before,'Close tooltip changed the layout'
        (results/'close-tooltip.json').write_text(json.dumps(status(),indent=2))
        # Closing an unrelated internal popup must not cancel a split resize.
        x,y,w,h=window['target']
        assert call('TestPointer','resize',x+w*.75,y+h*.5)=='true'
        wait_for(lambda:all(w['id']!=popup['id'] for w in status()['windows']),5)
        assert status()['dragging'],'Closing tooltip cancelled unrelated resize'
        # The hover helper moves away to dismiss the tooltip. That is real
        # pointer motion during this synthetic resize, so cancel it to restore
        # the starting ratios before comparing the layout.
        assert call('TestPointer','cancel',0,0)=='true'
        assert pointer.wait(timeout=5)==0
        wait_for(settled)
        assert {w['id']:w['target'] for w in tiled()}==before
    finally:
        if pointer.poll() is None:pointer.terminate();pointer.wait(timeout=3)
        if status()['dragging']:call('TestPointer','release',0,0)
    return {'real_close_button_tooltip':'passed','popup_close_during_resize':'passed'}


def run_fifth_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    snapshots=[]
    for label,kind in [('Files 1','fifth-dolphin'),('Settings','fifth-settings'),
                       ('Files 2','fifth-dolphin'),('Chat','fifth-chat'),('Files 3','fifth-dolphin')]:
        if tiled():
            latest=max(tiled(),key=lambda w:w['order'])
            assert call('TestWindow',latest['id'],'activate')=='true'
        p=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#21618c','0',kind],
            env=env,stdout=log,stderr=log)
        children.append(p)
        wait_for(lambda:len(tiled())==len(children))
        wait_for(settled)
        snapshots.append(status())
    assert len(tiled())==5
    for w in tiled():
        assert not w['capacityBlocked'] and not w['manualFloating']
        x,y,width,height=w['target']
        assert width+1>=w['minimum'][0] and height+1>=w['minimum'][1]
        for other in tiled():
            if w['id']>=other['id']:continue
            ox,oy,ow,oh=other['target']
            assert min(x+width,ox+ow)<=max(x,ox) or min(y+height,oy+oh)<=max(y,oy)
    assert not any(w['redirected'] for w in tiled())
    if status().get('appearance',{}).get('enabled'):
        assert all(w['appearance']['applied'] and w['appearance']['border']>0 for w in tiled())
        subprocess.run([str(root/'build/fluidtiles-capture'),str(results/'fifth-appearance.png')],
            env={**env,'QT_QPA_PLATFORM':'offscreen'},check=True,stdout=log,stderr=log,timeout=12)
    report={'pass':True,'five_constrained_clients':'passed','snapshots':snapshots,
        'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
    (results/'fifth.json').write_text(json.dumps(report,indent=2))
    return report
