"""Reference-derived user workflows for the isolated compositor only."""
import subprocess
import time
import os
import json
import sys


def run_audit(api, env, children, log):
    call,status,wait_for,tiled,settled,root=api
    report={}
    def windows():return {w['id']:w for w in status()['windows']}
    def targets():return {w['id']:w['target'] for w in tiled()}
    def get(ident):return windows()[ident]
    def activate(ident):
        assert call('TestWindow',ident,'activate')=='true'
        time.sleep(.08)
    def spawn(label,kind=''):
        proc=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#9c640c','0',kind],
            env=env,stdout=log,stderr=log)
        children.append(proc)
        found=wait_for(lambda:next((w for w in status()['windows'] if w.get('title','').endswith(label)),None),15)
        return proc,found['id']
    def close(proc,ident):
        proc.terminate();proc.wait(timeout=5)
        wait_for(lambda:ident not in windows());wait_for(settled)
    def unchanged_restore(original):
        for ident,geometry in original.items():
            assert get(ident)['restore']==geometry,('floating geometry was overwritten',ident,geometry,get(ident))

    original={w['id']:w['restore'] for w in tiled()}
    # Re-enabling is a batch of existing windows, never UUID sort order.
    layouts=[]
    for _ in range(3):
        call('SetEnabled',False);wait_for(lambda:not tiled())
        call('SetEnabled',True);wait_for(lambda:len(tiled())==3);wait_for(settled)
        layouts.append(targets())
    assert layouts[0]==layouts[1]==layouts[2],'enable produced different layouts for the same windows'
    report['repeat_activation']='passed'

    # Opening a fourth window splits the selected existing tile. Other tiles
    # keep their rectangles. Test a non-last-created selection deliberately.
    selected=min(tiled(),key=lambda w:w['order'])
    activate(selected['id']);before=targets()
    proc,new=spawn('Audit focused insertion')
    wait_for(lambda:len(tiled())==4);wait_for(settled)
    for ident,geometry in before.items():
        if ident!=selected['id']:assert get(ident)['target']==geometry,'new window split the wrong existing tile'
    assert get(selected['id'])['target']!=before[selected['id']]
    close(proc,new)
    report['focused_insertion']='passed'

    # Resize cancellation must restore ratios, not merely stop pointer capture.
    before=targets();selected=max(tiled(),key=lambda w:w['target'][3])
    x,y,w,h=selected['target'];point=(x+w*.75,y+h*.5)
    assert call('TestPointer','resize',*point)=='true'
    call('TestPointer','motion',point[0]-100,point[1]+75)
    assert targets()!=before
    assert call('TestPointer','cancel',0,0)=='true'
    wait_for(settled)
    assert targets()==before,'cancelled resize retained changed split ratios'
    report['resize_cancel']='passed'

    # Native states must not replace the saved pre-tiling floating rectangle.
    ident=selected['id']
    for operation,reverse in [('minimize','restore'),('fullscreen','unfullscreen'),('maximize','unmaximize')]:
        call('TestWindow',ident,operation);wait_for(lambda:not get(ident)['tiled'])
        call('TestWindow',ident,reverse);wait_for(lambda:get(ident)['tiled']);wait_for(settled)
        unchanged_restore(original)
    report['native_states_restore']='passed'

    slow=next(w['id'] for w in tiled() if 'Slow C' in w['title'])
    activate(slow)
    call('Action','float');call('Action','float')
    wait_for(lambda:len(tiled())==3);wait_for(settled)
    original[slow]=get(slow)['restore'] # explicit float now recenters its saved rectangle
    unchanged_restore(original)
    report['rapid_float_toggle_slow_client']='passed'

    # Persistent floating remains floating through other layout changes, and
    # the user's latest floating rectangle becomes the next restore geometry.
    activate(ident);call('Action','float');wait_for(lambda:not get(ident)['tiled'])
    assert get(ident)['manualFloating']
    call('TestWindow',ident,'move-floating')
    wait_for(lambda:max(abs(a-b) for a,b in zip(get(ident)['actual'],[120,180,500,330]))<2)
    call('TestWindow',next(w['id'] for w in tiled()),'minimize')
    assert not get(ident)['tiled']
    for w in list(windows().values()):
        if w['id'] in original and w['id']!=ident:call('TestWindow',w['id'],'restore')
    activate(ident);call('Action','float');wait_for(lambda:len(tiled())==3);wait_for(settled)
    original[ident]=get(ident)['restore']
    assert max(abs(a-b) for a,b in zip(original[ident],[120,180,500,330]))<2
    report['persistent_floating']='passed'

    # A window moved to another desktop must not resize the visible desktop's
    # tiles after it settles. Sticky windows follow the current desktop group.
    call('TestWindow',ident,'desktop-2')
    wait_for(lambda:not get(ident)['visible']);wait_for(settled)
    other={w['id']:w['target'] for w in tiled() if w['visible']}
    assert len(other)==2
    group=get(ident)['group']
    call('TestWindow',ident,'switch-desktop-2')
    wait_for(lambda:get(ident)['visible']);wait_for(settled)
    assert get(ident)['group']==group
    assert all(get(i)['target']==r for i,r in other.items())
    call('TestWindow',ident,'sticky');wait_for(settled)
    call('TestWindow',ident,'switch-desktop-1')
    wait_for(lambda:get(ident)['visible'] and get(ident)['group']!=group);wait_for(settled)
    call('TestWindow',ident,'desktop-1');wait_for(settled)
    unchanged_restore(original)
    report['desktop_isolation_and_sticky']='passed'

    # Fixed-size, dialog and bounded-size applications must remain native.
    for kind in ['fixed','dialog','bounded']:
        proc,new=spawn('Audit '+kind,kind)
        time.sleep(.2)
        assert not get(new)['tiled'],(kind,get(new))
        assert len(tiled())==(4 if kind=='dialog' else 3)
        if kind=='fixed':
            x,y,w,h=get(new)['actual'];point=(x+w/2,y+h/2)
            assert call('TestPointer','move',*point)=='false','Meta drag intercepted a floating window'
            assert call('TestPointer','resize',*point)=='false','resize intercepted a floating window'
            divider=(status()['outputs'][0]['geometry'][2]/2,point[1])
            assert x<divider[0]<x+w
            assert call('TestPointer','divider',*divider)=='false','divider drag intercepted a floating window'
        close(proc,new)
        wait_for(lambda:len(tiled())==3)
    report['excluded_windows']='passed'

    # Closing a native-dragged client and changing desktops during a custom
    # resize must cancel interaction safely and release the input filter.
    proc,new=spawn('Audit close during drag')
    wait_for(lambda:get(new)['tiled']);wait_for(settled)
    x,y,w,h=get(new)['actual']
    destination=next(t for t in tiled() if t['id']!=new)['target']
    pointer=subprocess.Popen([str(root/'build/fluidtiles-input'),str(x+w/2),str(y+12),'titlebar',
        str(destination[0]+destination[2]/2),str(destination[1]+destination[3]/2)],env=env,stdout=log,stderr=log)
    try:
        wait_for(lambda:status()['nativeDrag'],4)
        time.sleep(.2);close(proc,new)
        wait_for(lambda:not status()['dragging'])
        assert pointer.wait(timeout=5)==0
    finally:
        if pointer.poll() is None:pointer.terminate();pointer.wait(timeout=3)
    wait_for(lambda:len(tiled())==3);wait_for(settled)
    for _ in range(3):
        before=targets();selected=next(t for t in tiled() if 'Slow C' in t['title'])
        x,y,w,h=selected['target'];point=(x+w*.75,y+h*.5)
        assert call('TestPointer','resize',*point)=='true'
        call('TestPointer','motion',point[0]-80,point[1])
        call('TestWindow',selected['id'],'switch-desktop-2')
        wait_for(lambda:not status()['dragging'])
        call('TestWindow',selected['id'],'switch-desktop-1');wait_for(settled)
        assert targets()==before,'desktop change committed an unfinished resize'
    report['interrupted_interactions']='passed'

    # Capacity is temporary, not the user's permanent floating choice. When
    # other clients minimize, a previously oversized client must join tiling.
    proc,new=spawn('Audit capacity','large-minimum')
    wait_for(lambda:get(new)['capacityBlocked'])
    assert not get(new)['manualFloating'] and not get(new)['tiled']
    for old in original:call('TestWindow',old,'minimize')
    wait_for(lambda:get(new)['tiled']);wait_for(settled)
    assert len(tiled())==1
    initial_scale=float(os.environ.get('FLUIDTILES_TEST_SCALE','1.45'))
    subprocess.run(['kscreen-doctor','output.Virtual-0.scale.2'],env=env,check=True,stdout=log,stderr=log,timeout=10)
    wait_for(lambda:not get(new)['tiled'] and get(new)['capacityBlocked'])
    assert not get(new)['manualFloating']
    subprocess.run(['kscreen-doctor',f'output.Virtual-0.scale.{initial_scale}'],env=env,check=True,stdout=log,stderr=log,timeout=10)
    wait_for(lambda:get(new)['tiled']);wait_for(settled)
    close(proc,new)
    for old in original:call('TestWindow',old,'restore')
    wait_for(lambda:len(tiled())==3);wait_for(settled)
    unchanged_restore(original)
    report['capacity_recovery']='passed'

    # Disable while in a native state, then leave it: KWin must restore the
    # saved floating rectangle even though the client was already detached.
    for operation,reverse in [('fullscreen','unfullscreen'),('maximize','unmaximize'),('minimize','restore')]:
        call('TestWindow',ident,operation);wait_for(lambda:not get(ident)['tiled'])
        call('SetEnabled',False);wait_for(lambda:not tiled())
        call('TestWindow',ident,reverse)
        wait_for(lambda:max(abs(a-b) for a,b in zip(get(ident)['actual'],original[ident]))<2)
        call('SetEnabled',True);wait_for(lambda:len(tiled())==3);wait_for(settled)
    report['disable_during_native_states']='passed'
    x,y,w,h=max(tiled(),key=lambda t:t['target'][3])['target']
    assert call('TestPointer','resize',x+w*.75,y+h*.5)=='true'
    call('TestPointer','motion',x+w*.75-80,y+h*.5)
    call('SetEnabled',False)
    wait_for(lambda:not status()['dragging'] and not tiled())
    call('SetEnabled',True);wait_for(lambda:len(tiled())==3);wait_for(settled)
    report['disable_during_resize']='passed'

    # A client initiates xdg_toplevel.resize itself using Qt's system-resize
    # API and the real button serial. No test endpoint initiates this resize.
    activate(max(tiled(),key=lambda t:t['target'][3])['id'])
    proc,new=spawn('Audit client system resize','system-resize')
    wait_for(lambda:get(new)['tiled']);wait_for(settled)
    before=targets();x,y,w,h=get(new)['actual']
    right=x<status()['outputs'][0]['geometry'][2]/2
    px=x+w-20 if right else x+20;py=y+h*.5
    pointer=subprocess.Popen([str(root/'build/fluidtiles-input'),str(px),str(py),'native-resize',
        str(px+80),str(py)],env=env,stdout=log,stderr=log)
    try:
        wait_for(lambda:status()['nativeResize'],4)
        wait_for(lambda:targets()!=before,4)
        assert pointer.wait(timeout=5)==0
        wait_for(lambda:not status()['dragging']);wait_for(settled)
        assert targets()!=before,'client-initiated resize did not persist'
    finally:
        if pointer.poll() is None:pointer.terminate();pointer.wait(timeout=3)
        close(proc,new)
    report['client_initiated_system_resize']='passed'
    return report


def run_native_resize(api,env,log):
    call,status,wait_for,tiled,settled,root=api
    report={}
    for cancel in [False,True]:
        selected=max(tiled(),key=lambda w:w['target'][3])
        ident=selected['id'];before={w['id']:w['target'] for w in tiled()}
        x,y,w,h=selected['actual']
        # Select the internal left/right edge of the full-height tile. KWin's
        # native command initiates its resize; subsequent input and Escape go
        # through the actual Wayland input/filter chain.
        right=x<status()['outputs'][0]['geometry'][2]/2
        px=x+w-12 if right else x+12;py=y+h*.5
        mode='native-resize-cancel' if cancel else 'native-resize'
        proc=subprocess.Popen([str(root/'build/fluidtiles-input'),str(px),str(py),mode,
            str(px+100),str(py)],env=env,stdout=log,stderr=log)
        try:
            time.sleep(.14)
            assert call('TestWindow',ident,'native-resize')=='true'
            wait_for(lambda:status()['nativeResize'],3)
            changed=wait_for(lambda:{w['id']:w['target'] for w in tiled()}!=before,4)
            assert changed
            assert proc.wait(timeout=5)==0
            wait_for(lambda:not status()['dragging']);wait_for(settled)
            after={w['id']:w['target'] for w in tiled()}
            if cancel:assert after==before,'native Escape failed to restore original splits'
            else:assert after!=before,'native resize did not persist layout changes'
            report[mode]='passed'
        finally:
            if proc.poll() is None:proc.terminate();proc.wait(timeout=3)
    return report


def run_unload_audit(api,log,RESULTS):
    call,status,wait_for,tiled,settled,root=api
    native_unload_report={}
    count=len(tiled())
    native_subject=next((w['id'] for w in tiled() if 'Slow C' in w['title']),tiled()[0]['id'])
    for operation,statement in [('fullscreen','w.fullScreen=false;'),
                                ('maximize','w.setMaximize(false,false);'),
                                ('minimize','w.minimized=false;')]:
        restore=next(w['restore'] for w in tiled() if w['id']==native_subject)
        call('TestWindow',native_subject,operation)
        wait_for(lambda:not next(w['tiled'] for w in status()['windows'] if w['id']==native_subject))
        subprocess.run(['kwriteconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled','false'],check=True)
        subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log)
        def unloaded():
            names=subprocess.check_output(['qdbus6','org.kde.KWin','/Effects','org.kde.kwin.Effects.loadedEffects'],text=True)
            return 'fluidtiles' not in names.splitlines()
        wait_for(unloaded)
        script=RESULTS/'restore-native.js'
        script.write_text('for (const w of workspace.windowList()) { if (String(w.internalId).replace(/[{}]/g, "") === '+json.dumps(native_subject)+') { '+statement+' } }')
        subprocess.run(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',str(script),'fluidtiles-audit-command'],check=True,stdout=log)
        subprocess.run(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.start'],check=True,stdout=log)
        last_native_info={}
        def restored_without_plugin():
            info=subprocess.check_output(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.getWindowInfo','{'+native_subject+'}'],text=True)
            values=dict(line.split(': ',1) for line in info.splitlines() if ': ' in line)
            actual=[float(values[k]) for k in ['x','y','width','height']]
            last_native_info['actual']=actual
            return max(abs(a-b) for a,b in zip(actual,restore))<2
        try:wait_for(restored_without_plugin)
        except AssertionError:
            print('Native unload restoration failed:',operation,'expected:',restore,'last:',last_native_info,file=sys.stderr)
            raise
        subprocess.run(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','fluidtiles-audit-command'],check=True,stdout=log)
        subprocess.run(['kwriteconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled','true'],check=True)
        subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log)
        wait_for(lambda:status()['enabled']);wait_for(lambda:len(tiled())==count);wait_for(settled)
        native_unload_report[operation]='passed'
    restore=next(w['restore'] for w in tiled() if w['id']==native_subject)
    call('TestWindow',native_subject,'minimize')
    wait_for(lambda:not next(w['tiled'] for w in status()['windows'] if w['id']==native_subject))
    subprocess.run(['kwriteconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled','false'],check=True)
    subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log)
    wait_for(unloaded)
    subprocess.run(['kwriteconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled','true'],check=True)
    subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log)
    wait_for(lambda:status()['enabled'])
    call('TestWindow',native_subject,'restore')
    wait_for(lambda:len(tiled())==count);wait_for(settled)
    assert next(w['restore'] for w in tiled() if w['id']==native_subject)==restore
    native_unload_report['reenable_while_minimized']='passed'
    return native_unload_report
