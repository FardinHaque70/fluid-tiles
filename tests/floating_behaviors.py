"""Centered float transitions through real KWin, including client/decoration acks."""
import hashlib
import json
import subprocess
import time


def run_floating_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    report={}
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def near(a,b,tolerance=2):return max(abs(x-y) for x,y in zip(a,b))<tolerance
    def activate(ident):
        assert call('TestWindow',ident,'activate')=='true'
        wait_for(lambda:status()['activeWindow']==ident)
    def ready(ident):
        w=get(ident)
        return not w['floatingTransition'] and not w['redirected'] and near(w['actual'],w['restore'])
    def centered(ident):
        w=get(ident);x,y,width,height=w['actual'];ax,ay,aw,ah=w['workArea']
        assert abs(x+width/2-(ax+aw/2))<1.2 and abs(y+height/2-(ay+ah/2))<1.2,('float not centered',w)
        assert w['manualFloating'] and not w['tiled']
        assert all(w['stackingOrder']>tile['stackingOrder'] for tile in tiled()),'Centered float behind tiles'
        return w
    def retile(ident):
        activate(ident);assert call('Action','float')=='true'
        wait_for(lambda:get(ident)['tiled']);wait_for(settled)
        assert not get(ident)['floatingTransition'] and not get(ident)['redirected']
    # Slower motion leaves enough time to observe intermediate presentation,
    # independent of when a delayed client acknowledges its final rectangle.
    subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps({'SpringFrequency':12,'AppearanceEnabled':True})],
        env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
    wait_for(lambda:status()['springFrequency']==12 and status()['appearance']['enabled']);wait_for(settled)
    source=next(w['id'] for w in tiled() if w['title'].endswith('A'))
    slow=next(w['id'] for w in tiled() if w['title'].endswith('Slow C'))
    for ident,label in [(source,'regular'),(slow,'slow_client')]:
        activate(ident);before=get(ident);assert call('Action','float')=='true'
        first=get(ident)
        assert first['floatingTransition'] and not first['tiled'],first
        assert not near(first['visual'],first['target']), 'Float snapped directly to center'
        # Default supported floating titlebars return before the native size is chosen.
        wait_for(lambda:not get(ident)['appearance']['noBorder'])
        if label=='regular' and abs(status()['outputs'][0]['scale']-1.45)<.001:
            # Pixel evidence that the floated source is rendered at its moving
            # presentation, rather than merely reporting a spring in Status.
            moving=get(ident);x,y,width,height=moving['visual']
            point=[x+max(16,width*.12),y+max(16,height*.2)]
            image=results/'float-center-in-flight.png'
            subprocess.run([str(root/'build/fluidtiles-capture'),str(image)],
                env={**env,'QT_QPA_PLATFORM':'offscreen'},check=True,stdout=log,stderr=log,timeout=12)
            spec={'logicalSize':status()['outputs'][0]['geometry'][2:],'points':[point]}
            pixel=json.loads(subprocess.check_output([str(root/'build/fluidtiles-imagecheck'),str(image),json.dumps(spec)],text=True))['pixels'][0][:3]
            assert max(abs(a-b) for a,b in zip(pixel,(33,97,140)))<15,('Animated float was not painted at its presented bounds',point,pixel,moving)
            report['in_flight_rendered_position_pixel_check']='passed'
        samples=[];deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            w=get(ident);samples.append({k:w[k] for k in ['actual','visual','target','appearance','redirected','floatingTransition']})
            assert w['appearance']['available'] if 'available' in w['appearance'] else status()['appearance']['available']
            if ready(ident):break
            time.sleep(.015)
        wait_for(lambda:ready(ident));wait_for(settled);after=centered(ident)
        assert near(after['actual'][2:],before['restore'][2:]),'Forgot the remembered floating size'
        distinct={tuple(round(v,1) for v in sample['visual']) for sample in samples}
        assert len(distinct)>=3,('No observed smooth intermediates',samples)
        assert any(sample['redirected'] for sample in samples),'Transition did not use the existing animated rendering path'
        assert all(not sample['appearance']['applied'] or near(sample['appearance']['frame'],sample['visual']) for sample in samples if sample['floatingTransition'])
        (results/('float-center-'+label+'.json')).write_text(json.dumps({'before':before,'samples':samples,'after':after},indent=2)+'\n')
        subprocess.run([str(root/'build/fluidtiles-capture'),str(results/('float-center-'+label+'.png'))],
            env={**env,'QT_QPA_PLATFORM':'offscreen'},check=True,stdout=log,stderr=log,timeout=12)
        retile(ident)
        report[label+'_center_animation_size_titlebar_and_stacking']='passed'

    # Retile before the centering animation finishes: preserve the presented
    # rectangle and retarget the same spring rather than jumping to native bounds.
    activate(source);call('Action','float');time.sleep(.07);before=get(source)
    assert before['floatingTransition'];call('Action','float')
    immediate=get(source);wait_for(lambda:get(source)['tiled'])
    assert not near(immediate['visual'],immediate['actual']), 'Rapid retile jumped to the committed floating frame'
    wait_for(settled);assert not get(source)['floatingTransition']
    report['retile_interrupts_center_animation_without_native_frame_jump']='passed'

    # Real KWin move/resize start signals must release animation ownership.
    for operation in ['native-move','native-resize']:
        activate(source);call('Action','float');wait_for(lambda:get(source)['floatingTransition'])
        assert call('TestWindow',source,operation)=='true'
        wait_for(lambda:not get(source)['floatingTransition'] and not get(source)['redirected'])
        call('TestWindow',source,'native-cancel');retile(source)
    report['native_move_and_resize_cancel_centering']='passed'

    for operation,reverse in [('minimize','restore'),('maximize','unmaximize'),('fullscreen','unfullscreen')]:
        activate(source);call('Action','float');assert get(source)['floatingTransition']
        call('TestWindow',source,operation)
        wait_for(lambda:not get(source)['floatingTransition'] and not get(source)['redirected'])
        call('TestWindow',source,reverse);wait_for(settled)
        assert get(source)['manualFloating'] and not get(source)['tiled']
        retile(source)
    report['native_states_interrupt_centering']='passed'

    activate(slow);call('Action','float');expected=get(slow)['target'];call('SetEnabled',False)
    wait_for(lambda:not tiled());wait_for(lambda:not get(slow)['floatingTransition'] and not get(slow)['redirected'])
    wait_for(lambda:near(get(slow)['actual'],expected))
    assert not get(slow)['appearance']['owned']
    call('SetEnabled',True);wait_for(lambda:len(tiled())==2);wait_for(settled);retile(slow)
    report['disable_during_center_animation_releases_appearance_and_texture']='passed'

    # Close a newly tiled client mid-flight. Offscreen resources must release
    # without retaining a dangling WindowItem or a permanent active animation.
    proc=subprocess.Popen([str(root/'build/fluidtiles-demo'),'Float close','#9c640c','0'],env=env,stdout=log,stderr=log);children.append(proc)
    ident=wait_for(lambda:next((w['id'] for w in tiled() if w['title'].endswith('Float close')),None));wait_for(settled)
    activate(ident);call('Action','float');assert get(ident)['floatingTransition']
    proc.terminate();proc.wait(timeout=4)
    wait_for(lambda:all(w['id']!=ident for w in status()['windows']));wait_for(settled)
    report['close_during_center_animation']='passed'

    activate(slow);call('Action','float');expected=get(slow)['target']
    subprocess.run(['python3',str(root/'tools/control.py'),'stop'],check=True,stdout=log,stderr=log,timeout=12)
    def unloaded_center():
        text=subprocess.check_output(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.getWindowInfo','{'+slow+'}'],text=True)
        values=dict(line.split(': ',1) for line in text.splitlines() if ': ' in line)
        return near([float(values[k]) for k in ['x','y','width','height']],expected)
    wait_for(unloaded_center)
    subprocess.run(['python3',str(root/'tools/control.py'),'start'],check=True,stdout=log,stderr=log,timeout=15)
    wait_for(lambda:len(tiled())==3);wait_for(settled)
    report['true_unload_during_slow_client_centering']='passed'
    data={'pass':True,'cases':report,'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest(),'final':status()}
    (results/'float-center.json').write_text(json.dumps(data,indent=2)+'\n')
    return report
