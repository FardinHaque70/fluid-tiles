"""Animation catalog/lifecycle regressions, exclusively in disposable KWin."""
import json
import hashlib
import os
import subprocess
import time


def run_animation_audit(api, env, children, log, results):
    call,status,wait_for,tiled,settled,root=api
    assert env['WAYLAND_DISPLAY']=='fluidtiles-test'
    report={'presets':{}}
    catalog=json.loads((root/'package/contents/data/animations/catalog.json').read_text())
    def configure(preset='fluid',speed=1,overrides=None,pending=False):
        document=dict(version=1,preset=preset,speed=speed,enabled=True,overrides=overrides or {},customPresets=[])
        subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps({'AnimationSettings':document,'ExpandSingleWindow':False,'AppearanceEnabled':True,'BorderWidth':3,'UseSystemBorderColors':False,'ActiveBorderColor':'#ff2222'})],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=15)
        wait_for(lambda:status()['animation']['pending'] if pending else status()['animation']['settings']==document)
        assert not status()['animation']['error']
        return document
    def window(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def spawn(label,delay=0):
        p=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#21618c',str(delay),'large-opening'],env=env,stdout=log,stderr=log);children.append(p)
        ident=wait_for(lambda:next((w['id'] for w in tiled() if w['title'].endswith(label)),None))
        return p,ident
    def close(p,ident,animate=True):
        before=status()['animation']['completedCloses'];p.terminate();p.wait(timeout=4)
        if animate:wait_for(lambda:status()['animation']['completedCloses']>before)
        wait_for(lambda:all(w['id']!=ident for w in status()['windows']))
        wait_for(settled);assert status()['animation']['closingVisuals']==0
    subprocess.run([str(root/'build/fluidtiles-settings-tests'),'animations',str(results)],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=20)
    for preset in catalog['presets']:
        name=preset['id'];configure(name,speed=.5 if name=='fluid' else 1)
        p,ident=spawn('Animation '+name,80 if name=='fluid' else 0)
        first=window(ident);wait_for(settled)
        w=window(ident);assert not w['redirected'] and abs(w['animationOpacity']-1)<.001
        assert max(abs(a-b) for a,b in zip(w['actual'],w['target']))<2
        if name=='fluid':
            assert abs(first['openingFrom'][2]/first['target'][2]-.65)<.001
            subprocess.run([str(root/'build/fluidtiles-capture'),str(results/'fluid-settled.png')],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_CAPTURE_NATIVE':'1'},check=True,stdout=log,stderr=log,timeout=12)
            p.terminate();p.wait(timeout=4)
            ghost=wait_for(lambda:next((w for w in status()['animation']['closing'] if w['id']==ident),None))
            assert 0<=ghost['opacity']<=1
            subprocess.run([str(root/'build/fluidtiles-capture'),str(results/'fluid-closing.png')],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_CAPTURE_NATIVE':'1'},check=True,stdout=log,stderr=log,timeout=12)
            wait_for(settled);assert status()['animation']['closingVisuals']==0
        else:close(p,ident,name not in ['classic','off'])
        report['presets'][name]='passed'
    configure('hyde-macos')
    p,ident=spawn('Close during entrance',80)
    close(p,ident)
    report['close_during_entrance']='passed'
    # Sample the middle of an intentionally slow exit. Merely retaining a
    # deleted reference does not prove that its transformed visual was painted.
    fluid=next(p for p in catalog['presets'] if p['id']=='fluid')
    exit_profile=json.loads(json.dumps(fluid['profiles']['close']))
    linear=dict(type='bezier',x1=0,y1=0,x2=1,y2=1,duration=1800)
    exit_profile.update(curve=linear,opacity=linear,size=.5)
    configure(overrides={'close':exit_profile});p,ident=spawn('Mid exit pixels');wait_for(settled)
    original=window(ident)['visual'];p.terminate();p.wait(timeout=4)
    ghost=wait_for(lambda:next((w for w in status()['animation']['closing'] if w['id']==ident and .6<w['opacity']<.75),None))
    image=results/'closing-midpoint.png'
    subprocess.run([str(root/'build/fluidtiles-capture'),str(image)],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_CAPTURE_NATIVE':'1'},check=True,stdout=log,stderr=log,timeout=12)
    x,y,w,h=original
    spec={'logicalSize':status()['outputs'][0]['geometry'][2:],'points':[[x+w*.03,y+h*.5],[x+w*.3,y+h*.3]],'dominantRed':True}
    checked=json.loads(subprocess.check_output([str(root/'build/fluidtiles-imagecheck'),str(image),json.dumps(spec)],text=True));pixels=checked['pixels']
    assert max(pixels[0][:3])<15,('Closing source leaked outside its transformed rectangle',pixels,ghost)
    assert 25<pixels[1][2]<135,('Closing content not visibly faded',pixels,ghost)
    assert checked['redPixelCount']>200,('Closing outline was lost',checked)
    wait_for(settled);assert status()['animation']['closingVisuals']==0
    report['closing_transform_and_opacity_pixels']='passed'
    report['closing_outline_pixels']=checked['redPixelCount']
    configure(overrides={'close':exit_profile});anchor,anchor_id=spawn('Desktop anchor');wait_for(settled)
    p,ident=spawn('Close interrupted desktop');wait_for(settled);p.terminate();p.wait(timeout=4)
    wait_for(lambda:status()['animation']['closingVisuals']>0)
    call('TestWindow',anchor_id,'switch-desktop-2');wait_for(lambda:status()['animation']['closingVisuals']==0)
    call('TestWindow',anchor_id,'switch-desktop-1');wait_for(settled);close(anchor,anchor_id)
    report['desktop_change_releases_closing_visual']='passed'
    configure(overrides={'close':exit_profile});p,ident=spawn('Closing true unload');wait_for(settled)
    p.terminate();p.wait(timeout=4);wait_for(lambda:status()['animation']['closingVisuals']>0)
    subprocess.run(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','fluidtiles-controller'],check=True,stdout=log,stderr=log)
    wait_for(lambda:'fluidtiles' not in subprocess.check_output(['qdbus6','org.kde.KWin','/Effects','org.kde.kwin.Effects.loadedEffects'],text=True).splitlines())
    subprocess.run(['python3',str(root/'tools/control.py'),'start'],check=True,stdout=log,stderr=log,timeout=15)
    wait_for(lambda:status()['enabled']);assert status()['animation']['closingVisuals']==0
    report['true_unload_releases_closing_visual']='passed'
    # Native states must not become synthetic close events or replay entrances.
    configure();p,ident=spawn('Native states');wait_for(settled)
    for operation,undo,flag in [('minimize','restore','minimized'),('maximize','unmaximize','maximized'),('fullscreen','unfullscreen','fullscreen')]:
        original=window(ident)['openingFrom'];call('TestWindow',ident,operation)
        wait_for(lambda:window(ident)[flag] and not window(ident)['tiled']);assert not window(ident)['redirected']
        call('TestWindow',ident,undo);wait_for(lambda:window(ident)['tiled']);wait_for(settled)
        assert window(ident)['openingFrom']==original
    close(p,ident);report['native_state_interruption']='passed'
    configure('hyde-macos');p,ident=spawn('Close interrupted disable');wait_for(settled)
    p.terminate();p.wait(timeout=4);wait_for(lambda:status()['animation']['closingVisuals']>0)
    call('SetEnabled',False);wait_for(lambda:not status()['enabled']);assert status()['animation']['closingVisuals']==0
    call('SetEnabled',True);wait_for(lambda:status()['enabled']);report['disable_releases_closing_visual']='passed'
    configure();p,a=spawn('Reflow A');wait_for(settled);q,b=spawn('Reflow B');wait_for(settled)
    call('TestWindow',a,'activate')
    for _ in range(12):call('Action','split')
    wait_for(settled);assert all(not w['redirected'] for w in tiled())
    call('Action','float');wait_for(lambda:not window(a)['tiled']);wait_for(settled)
    assert not window(a)['redirected'];call('Action','float');wait_for(lambda:window(a)['tiled']);wait_for(settled)
    report['retarget_and_float']='passed'
    # Changing only animation settings must not cancel a held tiling interaction.
    rect=window(a)['visual'];x,y=rect[0]+rect[2]/2,rect[1]+rect[3]/2
    assert call('TestPointer','move',x,y)=='true'
    configure('hyde-fast',pending=True);assert status()['dragging'] and status()['animation']['pending']
    call('TestPointer','release',x,y);wait_for(lambda:not status()['dragging']);wait_for(lambda:not status()['animation']['pending']);wait_for(settled)
    report['settings_deferred_during_drag']='passed'
    report['matched_virtual_comparison']=[]
    for preset in ['classic','fluid','fluid','classic']:
        configure(preset);wait_for(settled);call('TestWindow',a,'activate');call('ResetMetrics')
        for _ in range(4):
            call('Action','split');time.sleep(.12);call('Action','split');wait_for(settled)
        report['matched_virtual_comparison'].append({'preset':preset,'metrics':status()['metrics']})
    configure();wait_for(settled)
    close(q,b);close(p,a)
    configure('off');p,ident=spawn('No animations');wait_for(settled);assert window(ident)['openingFrom']==[0,0,0,0];close(p,ident,False)
    report['off']='passed'
    configure();p,ident=spawn('Idle animation');wait_for(settled);time.sleep(.2);before=status();time.sleep(.5);after=status()
    assert after['animation']['textureCaptures']==before['animation']['textureCaptures']
    assert not after['animating'] and after['animation']['closingVisuals']==0
    report['idle_no_capture_or_animation']='passed';report['metrics']=after['metrics'];close(p,ident)
    report['pass']=True
    report['native_binary_sha256']=hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()
    (results/'animations.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def run_activation_update_audit(api,env,log,results):
    call,status,wait_for,tiled,settled,root=api
    assert env['WAYLAND_DISPLAY']=='fluidtiles-test'
    def autoload():
        return subprocess.check_output(['kreadconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled'],text=True).strip()
    def install():subprocess.run(['python3',str(root/'tools/control.py'),'install'],check=True,env=env,stdout=log,stderr=log,timeout=20)
    before=status()['animation']['settings'];install()
    assert status()['enabled'] and autoload()=='true'
    assert status()['animation']['settings']==before
    call('SetEnabled',False);wait_for(lambda:not status()['enabled']);install()
    assert not status()['enabled'] and autoload()=='true','Update enabled emergency-disabled tiling'
    assert status()['animation']['settings']==before
    subprocess.run(['python3',str(root/'tools/control.py'),'stop'],check=True,env=env,stdout=log,stderr=log,timeout=15)
    install();assert autoload()=='false'
    probe=subprocess.run(['qdbus6','org.kde.KWin','/org/kde/FluidTiles','org.kde.FluidTiles.Status'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert probe.returncode!=0,'Update started an inactive plugin'
    report={'pass':True,'enabled_update':'passed','emergency_disabled_update':'passed','fully_disabled_update':'passed','preferences_retained':'passed'}
    (results/'activation-update.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
