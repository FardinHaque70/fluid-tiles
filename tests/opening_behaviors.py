"""New-window entrances in disposable KWin, including remembered large frames."""
import hashlib
import json
import os
import subprocess
import time


def run_opening_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    repro=os.environ.get('FLUIDTILES_TEST_REPRODUCE')=='1'
    report={}
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def near(a,b,tolerance=2):return max(abs(x-y) for x,y in zip(a,b))<tolerance
    def center(r):return [r[0]+r[2]/2,r[1]+r[3]/2]
    def settings(values):
        subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps(values)],
            env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
        wait_for(lambda:status()['springFrequency']==values['SpringFrequency'])
    def capture(name):
        subprocess.run([str(root/'build/fluidtiles-capture'),str(results/name)],
            env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_CAPTURE_NATIVE':'1'},check=True,stdout=log,stderr=log,timeout=12)
    def spawn(label,kind='',delay=0,finish=True):
        proc=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#21618c',str(delay),kind],env=env,stdout=log,stderr=log)
        children.append(proc)
        ident=wait_for(lambda:next((w['id'] for w in tiled() if w['title'].endswith(label)),None))
        first=get(ident);samples=[first]
        if not finish:return proc,ident,samples
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            w=get(ident);samples.append(w)
            if near(w['visual'],w['target']) and not status()['animating']:break
            if len(samples)==3 and not repro:capture(label.replace(' ','-')+'.png')
            time.sleep(.015)
        wait_for(settled)
        (results/(label.replace(' ','-')+'-samples.json')).write_text(json.dumps(samples,indent=2)+'\n')
        return proc,ident,samples

    settings({'SpringFrequency':8,'AppearanceEnabled':True,'BorderWidth':3,'UseSystemBorderColors':False,'ActiveBorderColor':'#ff2222'})
    wait_for(lambda:status()['appearance']['enabled'])
    proc,ident,samples=spawn('First large opening','large-opening')
    first=samples[0];goal=first['target'];area=first['workArea']
    report['first_sample_size_fraction']=[first['visual'][i]/goal[i] for i in [2,3]]
    report['first_sample_center_error']=max(abs(a-b) for a,b in zip(center(first['visual']),center(area)))
    if repro:
        report['pass']=False
        (results/'opening-reproduction.json').write_text(json.dumps(report,indent=2)+'\n')
        return report
    start=first['openingFrom']
    assert near(center(start),center(area),.01),('Entrance did not start at the work-area center',first)
    assert all(abs(start[i]/goal[i]-.65)<.001 for i in [2,3]),('Entrance is not visibly smaller',start,goal)
    assert any(w['redirected'] and w['visual'][2]<goal[2]*.9 for w in samples),'First window skipped transformed entrance'
    assert near(get(ident)['actual'],goal) and not get(ident)['redirected']
    image=results/'First-large-opening.png'
    spec={'logicalSize':status()['outputs'][0]['geometry'][2:],'points':[
        [area[0]+area[2]*.02,area[1]+area[3]*.5],
        [area[0]+area[2]*.25,area[1]+area[3]*.5]]}
    pixels=json.loads(subprocess.check_output([str(root/'build/fluidtiles-imagecheck'),str(image),json.dumps(spec)],text=True))['pixels']
    assert max(pixels[0][:3])<10 and pixels[1][2]>70,('Capture did not render the centered smaller frame',pixels)
    report['first_large_window_centered_and_animated']='passed'

    second,other,more=spawn('Second opening',delay=80)
    start=more[0]['openingFrom'];area=more[0]['workArea'];goal=more[0]['target']
    assert near(center(start),center(area),.01) and not near(center(start),center(goal),10)
    assert any(w['redirected'] and not near(w['visual'],goal,15) for w in more)
    assert all(near(get(i)['actual'],get(i)['target']) and not get(i)['redirected'] for i in [ident,other])
    report['second_slow_window_moves_from_center_to_its_tile']='passed'

    call('TestWindow',ident,'activate');call('TestWindow',ident,'minimize');wait_for(lambda:not get(ident)['tiled']);wait_for(settled)
    original=get(ident)['openingFrom']
    call('TestWindow',ident,'restore');wait_for(lambda:get(ident)['tiled']);wait_for(settled)
    assert get(ident)['openingFrom']==original,'Restoring replayed a new entrance'
    report['minimize_restore_does_not_reinitialize_entrance']='passed'

    call('SetEnabled',False);wait_for(lambda:not status()['enabled'])
    third=subprocess.Popen([str(root/'build/fluidtiles-demo'),'Opened disabled','#7d3c98','0'],env=env,stdout=log,stderr=log);children.append(third)
    existing=wait_for(lambda:next((w['id'] for w in status()['windows'] if w['title'].endswith('Opened disabled')),None))
    call('SetEnabled',True);wait_for(lambda:len(tiled())==3);wait_for(settled)
    assert get(existing)['openingFrom']==[0,0,0,0],'Enabling manufactured a new-window entrance'
    report['enable_with_existing_windows_skips_entrance']='passed'

    # Interrupt real maps before their springs settle. Native states, disable,
    # and closure must release presentation ownership.
    for operation,undo,flag in [('fullscreen','unfullscreen','fullscreen'),('maximize','unmaximize','maximized'),('minimize','restore','minimized')]:
        current,opening,frames=spawn('Interrupted '+operation,finish=False)
        assert not near(frames[0]['visual'],frames[0]['target'],10)
        call('TestWindow',opening,operation)
        wait_for(lambda:get(opening)[flag] and not get(opening)['tiled'] and not get(opening)['redirected'])
        call('TestWindow',opening,undo);wait_for(lambda:get(opening)['tiled']);wait_for(settled)
        current.terminate();current.wait(timeout=3);wait_for(lambda:all(w['id']!=opening for w in status()['windows']));wait_for(settled)
    report['native_states_interrupt_opening']='passed'

    current,opening,frames=spawn('Interrupted disable',delay=80,finish=False)
    call('SetEnabled',False);wait_for(lambda:not status()['enabled'])
    assert not get(opening)['redirected'] and not get(opening)['tiled']
    current.terminate();current.wait(timeout=3);call('SetEnabled',True)
    wait_for(lambda:len(tiled())==3);wait_for(settled)
    current,opening,frames=spawn('Interrupted close',finish=False)
    current.terminate();current.wait(timeout=3)
    wait_for(lambda:all(w['id']!=opening for w in status()['windows']));wait_for(settled)
    report['disable_and_close_interrupt_opening']='passed'

    for kind in ['fixed','modal','default-ignored']:
        current=subprocess.Popen([str(root/'build/fluidtiles-demo'),'Excluded '+kind,'#7d3c98','0',kind],env=env,stdout=log,stderr=log);children.append(current)
        excluded=wait_for(lambda:next((w for w in status()['windows'] if w['title'].endswith('Excluded '+kind)),None))
        assert not excluded['tiled'] and excluded['openingFrom']==[0,0,0,0]
        current.terminate();current.wait(timeout=3)
    report['excluded_windows_keep_native_opening']='passed'

    subprocess.run(['kwriteconfig6','--file','kdeglobals','--group','KDE','--key','AnimationDurationFactor','0'],check=True)
    subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log,stderr=log)
    wait_for(lambda:status()['animationTimeFactor']==0)
    current,opening,frames=spawn('Animations disabled')
    assert frames[0]['openingFrom']==[0,0,0,0] and near(frames[0]['visual'],frames[0]['target'])
    report['disabled_animations_skip_entrance']='passed'

    report['pass']=True
    report['native_binary_sha256']=hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()
    (results/'opening.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
