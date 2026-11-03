"""Singleton resize ownership and outlines in the real KWin desktop slide."""
import hashlib
import json
import os
import subprocess
import time


def run_interaction_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    report={};repro=os.environ.get('FLUIDTILES_TEST_REPRODUCE')=='1'
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def near(a,b,tolerance=2):return max(abs(x-y) for x,y in zip(a,b))<tolerance
    def activate(ident):
        call('TestWindow',ident,'activate');wait_for(lambda:status()['activeWindow']==ident)
    def spawn(label,color='#21618c',delay=0,kind=''):
        proc=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,color,str(delay),kind],env=env,stdout=log,stderr=log);children.append(proc)
        ident=wait_for(lambda:next((w['id'] for w in tiled() if w['title'].endswith(label)),None));wait_for(settled)
        return proc,ident
    def capture(name):
        file=results/name
        subprocess.run([str(root/'build/fluidtiles-capture'),str(file)],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_CAPTURE_NATIVE':'1'},check=True,stdout=log,stderr=log,timeout=12)
        return file
    def border_samples(image,ident,color=None):
        s=status();w=get(ident);x,y,width,height=w['appearance']['frame'];b=w['appearance']['border'];area=s['outputs'][0]['geometry']
        points=[[area[0]+area[2]*i/400,y-b/2] for i in range(1,400)]
        spec={'logicalSize':area[2:],'points':points}
        pixels=json.loads(subprocess.check_output([str(root/'build/fluidtiles-imagecheck'),str(image),json.dumps(spec)],text=True))['pixels']
        colors=[color] if color else [(255,34,34),(34,255,68)]
        return sum(any(max(abs(a-b) for a,b in zip(p[:3],color))<16 for color in colors) for p in pixels)
    subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps({'SpringFrequency':12,'AppearanceEnabled':True,'BorderWidth':4,'UseSystemBorderColors':False,'ActiveBorderColor':'#ff2222','InactiveBorderColor':'#22ff44'})],
        env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
    wait_for(lambda:status()['appearance']['enabled'] and status()['springFrequency']==12)
    proc,ident=spawn('Single resize')
    activate(ident);before=get(ident);x,y,width,height=before['actual'];point=(x+width*.85,y+height*.85)
    accepted=call('TestPointer','resize',*point)=='true'
    if accepted:call('TestPointer','cancel',0,0);wait_for(settled)
    # Real native resize on the singleton; the old renderer kept its visual
    # rectangle full-screen even while the native client geometry shrank.
    px=x+width-12;py=y+height-12
    pointer=subprocess.Popen([str(root/'build/fluidtiles-input'),str(px),str(py),'native-resize',str(px-220),str(py-130)],env=env,stdout=log,stderr=log)
    samples=[]
    try:
        time.sleep(.14);call('TestWindow',ident,'native-resize');wait_for(lambda:status()['nativeResize'],3)
        deadline=time.monotonic()+4
        while time.monotonic()<deadline and pointer.poll() is None:
            s=status();w=get(ident)
            if s['nativeResize']:samples.append({k:w[k] for k in ['actual','visual','target','requested','redirected']})
            time.sleep(.025)
        assert pointer.wait(timeout=3)==0
        wait_for(lambda:not status()['dragging']);wait_for(settled)
    finally:
        if pointer.poll() is None:pointer.terminate();pointer.wait(timeout=3)
    assert any(sample['actual'][2]<width-80 for sample in samples),'Native fixture never resized its client'
    mismatch=max(max(abs(a-b) for a,b in zip(sample['actual'],sample['visual'])) for sample in samples)
    report['singleton_meta_resize_accepted']=accepted
    report['singleton_native_visual_mismatch']=mismatch
    (results/'singleton-native-samples.json').write_text(json.dumps(samples,indent=2)+'\n')
    if not repro:
        assert accepted,'Singleton Meta resize rejected'
        assert mismatch<2,('Renderer fights native singleton resize',mismatch)
        assert near(get(ident)['target'],before['target']) and near(get(ident)['actual'],before['target'])

    if not repro:
        # All corners get a temporary freely resizable frame without changing
        # the tile allocation or the remembered persistent floating rectangle.
        restore=before['restore'];goal=before['target']
        for left,top in [(False,False),(True,False),(True,True),(False,True)]:
            activate(ident);w=get(ident);x,y,width,height=w['actual']
            origin=(x+width*(.15 if left else .85),y+height*(.15 if top else .85))
            assert call('TestPointer','resize',*origin)=='true'
            assert status()['freeResize']
            call('TestPointer','motion',origin[0]+(170 if left else -170),origin[1]+(110 if top else -110))
            wait_for(lambda:near(get(ident)['actual'],get(ident)['visual']))
            held=get(ident)
            assert held['visual'][2]<width-100 and held['visual'][3]<height-70
            assert held['target']==goal and held['restore']==restore
            time.sleep(.1);assert near(get(ident)['requested'],held['visual']), 'Tiler fought the held resize'
            if not left and not top:
                capture('singleton-held-resize.png')
                # Escape and release share the return animation, with the tree unchanged.
                phase='release'
            else:phase='cancel'
            call('TestPointer',phase,0,0);immediate=get(ident)
            assert not near(immediate['visual'],goal),'Release skipped the return animation'
            wait_for(settled)
            assert near(get(ident)['actual'],goal) and get(ident)['restore']==restore
            assert not get(ident)['redirected']
        report['all_corner_free_resize_and_animated_return']='passed'
        # A slow Qt client initiates xdg_toplevel.resize with the real press
        # serial. This exercises the singleton native path without TestWindow
        # starting its grab, as well as delayed size acknowledgements.
        call('TestWindow',ident,'desktop-2');wait_for(lambda:not get(ident)['visible']);wait_for(settled)
        slow_proc,slow=spawn('Single client system resize','#117864',80,'system-resize')
        w=get(slow);sx,sy,sw,sh=w['actual'];origin=(sx+sw-15,sy+sh*.5)
        pointer=subprocess.Popen([str(root/'build/fluidtiles-input'),str(origin[0]),str(origin[1]),'native-resize',str(origin[0]-200),str(origin[1])],env=env,stdout=log,stderr=log)
        seen=[]
        try:
            wait_for(lambda:status()['nativeResize'] and status()['freeResize'],3)
            while pointer.poll() is None:
                current=get(slow)
                if status()['nativeResize']:seen.append(current)
                time.sleep(.025)
            assert pointer.wait(timeout=3)==0
            wait_for(lambda:not status()['dragging']);wait_for(settled)
        finally:
            if pointer.poll() is None:pointer.terminate();pointer.wait(timeout=3)
        assert any(w['actual'][2]<sw-70 for w in seen),'Client system resize never shrank'
        assert all(near(w['actual'],w['visual']) for w in seen),'Slow-client native resize was masked by tiling'
        assert near(get(slow)['actual'],get(slow)['target']) and not get(slow)['redirected']
        slow_proc.terminate();slow_proc.wait(timeout=4)
        wait_for(lambda:all(w['id']!=slow for w in status()['windows']))
        call('TestWindow',ident,'desktop-1');wait_for(lambda:get(ident)['visible']);wait_for(settled)
        report['slow_client_initiated_singleton_system_resize']='passed'

        # Cancellation by desktop change and disable must release the temporary
        # resize state without changing persistent floating membership.
        for operation in ['desktop','disable']:
            activate(ident);w=get(ident);x,y,width,height=w['actual'];origin=(x+width*.8,y+height*.8)
            call('TestPointer','resize',*origin);call('TestPointer','motion',origin[0]-160,origin[1]-100)
            wait_for(lambda:near(get(ident)['actual'],get(ident)['visual']))
            if operation=='desktop':
                call('TestWindow',ident,'switch-desktop-2');wait_for(lambda:not status()['dragging'])
                call('TestWindow',ident,'switch-desktop-1');wait_for(lambda:get(ident)['visible'])
                wait_for(lambda:not status()['fullScreenEffect']);wait_for(settled)
            else:
                call('SetEnabled',False);wait_for(lambda:not status()['dragging'] and not tiled())
                call('SetEnabled',True);wait_for(lambda:len(tiled())==1);wait_for(settled)
            assert not get(ident)['manualFloating'] and near(get(ident)['actual'],get(ident)['target'])
        report['free_resize_desktop_and_disable_interruptions']='passed'

    _,other=spawn('Desktop slide','#7d3c98')
    call('TestWindow',other,'desktop-2');wait_for(lambda:not get(other)['visible']);wait_for(settled)
    call('TestWindow',other,'switch-desktop-2');wait_for(lambda:get(other)['visible'])
    wait_for(lambda:not status()['fullScreenEffect']);wait_for(settled)
    call('TestWindow',ident,'switch-desktop-1');wait_for(lambda:get(ident)['visible'])
    wait_for(lambda:not status()['fullScreenEffect']);wait_for(settled)
    activate(ident)
    baseline=capture('desktop-before-slide.png');baseline_count=border_samples(baseline,ident)
    assert baseline_count>250,('Fixture lacks baseline outlines',baseline_count)
    assert call('TestWindow',ident,'desktop-gesture-half')=='true'
    wait_for(lambda:'SlideEffect' in status()['fullScreenEffect'])
    mid=capture('desktop-mid-slide.png');count=border_samples(mid,ident)
    report['slide_border_samples']=count
    if not repro:
        assert count>300,('Outlines disappear during desktop slide',count)
        outgoing=border_samples(mid,ident,(255,34,34));incoming=border_samples(mid,ident,(34,255,68))
        assert outgoing>130 and incoming>130,('Outgoing/incoming desktop border missing',outgoing,incoming)
        assert status()['appearance']['slideOutlineDraws']>0
        report['outgoing_and_incoming_borders_during_actual_slide']='passed'
    call('TestWindow',ident,'desktop-gesture-cancel');wait_for(lambda:not status()['fullScreenEffect']);wait_for(settled)
    call('TestWindow',other,'switch-desktop-2');wait_for(lambda:get(other)['visible'])
    wait_for(lambda:not status()['fullScreenEffect']);wait_for(settled)
    after=capture('desktop-after-slide.png');assert border_samples(after,other)>250
    if not repro:
        for _ in range(3):
            call('TestWindow',ident,'switch-desktop-1');time.sleep(.06)
            call('TestWindow',other,'switch-desktop-2');time.sleep(.04)
        wait_for(lambda:not status()['fullScreenEffect']);wait_for(settled)
        assert border_samples(capture('desktop-interrupted-slide-final.png'),other)>250
        before_idle=status()['appearance']['outlineDraws'];time.sleep(.4)
        assert status()['appearance']['outlineDraws']==before_idle,'Slide left continuous idle outline repaints'
        report['interrupted_desktop_slides_and_idle_release']='passed'
    if repro:
        data={'reproduced':not accepted and mismatch>50 and count<20,'observations':report,'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
        (results/'reproduction.json').write_text(json.dumps(data,indent=2)+'\n');return data
    data={'pass':True,'cases':report,'final':status(),'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
    (results/'interactions.json').write_text(json.dumps(data,indent=2)+'\n')
    return report
