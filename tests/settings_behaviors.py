"""Change preferences through KDE's real config module in isolated KWin."""
import json
import subprocess


def run_settings_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    report={}
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def apply(**values):
        subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps(values)],
            env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},
            check=True,stdout=log,stderr=log,timeout=10)
        for key,effective in [('IgnoreApplications','applications'),('IgnoreTitles','titles'),('IgnoreRoles','roles')]:
            if key in values:wait_for(lambda:status()['ignoreRules'][effective]==values[key])
    def normal():
        apply(InnerGap=10,OuterGap=10,SpringFrequency=24,IgnoreApplications='',IgnoreTitles='',IgnoreRoles='')
        wait_for(lambda:len(tiled())==3 and status()['innerGap']==10 and status()['outerGap']==10);wait_for(settled)
    def spawn(label,kind=''):
        p=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#9c640c','0',kind],env=env,stdout=log,stderr=log)
        children.append(p)
        window=wait_for(lambda:next((w for w in status()['windows'] if w.get('title','').endswith(label)),None),15)
        return p,window['id']
    def close(p,ident):
        p.terminate();p.wait(timeout=5)
        wait_for(lambda:all(w['id']!=ident for w in status()['windows']));wait_for(settled)
    def is_restored(ident,rect):return max(abs(a-b) for a,b in zip(get(ident)['actual'],rect))<2

    # The form itself must load, save multiline rules, reload, restore defaults,
    # and render both pages. This leaves defaults saved for the remaining tests.
    subprocess.run([str(root/'build/fluidtiles-settings-tests'),str(results)],
        env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},
        check=True,stdout=log,stderr=log,timeout=10)
    wait_for(lambda:status()['innerGap']==10);wait_for(settled)
    report['real_kde_dialog_roundtrip_defaults']='passed'
    defaults=status()['ignoreRules']
    assert 'krunner' in defaults['applications'].splitlines() and defaults['roles']=='quake'
    p,new=spawn('Krohnkite default ignored','default-ignored')
    assert get(new)['ignored'] and not get(new)['tiled'] and len(tiled())==3
    close(p,new)
    report['krohnkite_default_exclusions']='passed'
    p,new=spawn('Parentless modal','modal')
    assert get(new)['modal'] and not get(new)['tiled'] and len(tiled())==3
    close(p,new)
    report['parentless_modal_exclusion']='passed'

    settings=subprocess.Popen([str(root/'build/fluidtiles-settings'),str(results/'settings-dialog.png')],
        env={**env,'FLUIDTILES_SETTINGS_TEST':'1'},stdout=log,stderr=log)
    try:
        window=wait_for(lambda:next((w for w in status()['windows'] if w['class']=='org.fluidtiles.Settings'),None),4)
        assert not window['tiled'] and len(tiled())==3,'Settings dialog entered tiling'
        assert settings.wait(timeout=5)==0
        wait_for(lambda:all(w['id']!=window['id'] for w in status()['windows']))
    finally:
        if settings.poll() is None:settings.terminate();settings.wait(timeout=3)
    report['standalone_dialog_stays_floating']='passed'

    # No KWin reconfigure/restart call: the real configuration dialog's Save
    # must reach the running native controller by itself.
    apply(InnerGap=22.5,OuterGap=18.5,SpringFrequency=32)
    wait_for(lambda:status()['innerGap']==22.5 and status()['outerGap']==18.5 and status()['springFrequency']==32)
    wait_for(settled)
    scale=status()['outputs'][0]['scale']
    snapped=lambda n:int(n*scale+.5)/scale
    windows=tiled()
    assert abs(min(w['target'][0] for w in windows)-snapped(18.5))<.01
    full=max(windows,key=lambda w:w['target'][3]);x,y,width,height=full['target']
    others=[w['target'] for w in windows if w['id']!=full['id']]
    for r in others:
        gap=r[0]-(x+width) if r[0]>x else x-(r[0]+r[2])
        assert abs(gap-snapped(22.5))<.01
    apply(InnerGap=0,OuterGap=0);wait_for(lambda:status()['innerGap']==0 and status()['outerGap']==0);wait_for(settled)
    assert min(w['target'][0] for w in tiled())==0
    normal();report['live_gaps_and_responsiveness']='passed'

    source=next(w for w in tiled() if 'Slow C' in w['title']);ident=source['id'];restore=source['restore']
    apply(IgnoreTitles='\n *SLOW C\n')
    wait_for(lambda:get(ident)['ignored'] and not get(ident)['tiled']);wait_for(lambda:is_restored(ident,restore))
    assert len(tiled())==2 and not get(ident)['manualFloating']
    apply(IgnoreTitles='');wait_for(lambda:len(tiled())==3);wait_for(settled)
    assert get(ident)['restore']==restore
    report['ignore_existing_title_and_remove_rule']='passed'

    # A rule applied while a client is fullscreen must govern the subsequent
    # normal window as well, without resizing the fullscreen surface itself.
    call('TestWindow',ident,'fullscreen');wait_for(lambda:not get(ident)['tiled'])
    apply(IgnoreTitles='*Slow C');wait_for(lambda:get(ident)['ignored'])
    assert get(ident)['fullscreen']
    call('TestWindow',ident,'unfullscreen');wait_for(lambda:is_restored(ident,restore))
    assert not get(ident)['tiled'];normal()
    report['ignore_rule_during_fullscreen']='passed'

    apply(IgnoreApplications='\n FLUIDTILES-* \n')
    wait_for(lambda:not tiled());wait_for(lambda:all(w['ignored'] for w in status()['windows'] if w['class']=='fluidtiles-demo'))
    p,new=spawn('Ignored on open')
    assert get(new)['ignored'] and not get(new)['tiled']
    apply(IgnoreApplications='');wait_for(lambda:len(tiled())==4);wait_for(settled)
    close(p,new);wait_for(lambda:len(tiled())==3)
    report['ignore_applications_and_new_windows']='passed'

    assert all(not w['role'] for w in tiled())
    apply(IgnoreRoles='*')
    assert len(tiled())==3 and not any(w['ignored'] for w in tiled())
    apply(IgnoreRoles='')
    report['absent_roles_do_not_match']='passed'

    # A real client title change must update membership without changing rules.
    apply(IgnoreTitles='*Audit Ignored title')
    p,new=spawn('Dynamic title','changing-title')
    wait_for(lambda:get(new)['tiled'])
    wait_for(lambda:get(new)['ignored'] and not get(new)['tiled'],5)
    wait_for(lambda:not get(new)['ignored'] and get(new)['tiled'],5)
    wait_for(settled);close(p,new);normal()
    report['dynamic_title_rules']='passed'

    # Saving during a resize cancels capture safely. Saving while tiling is
    # disabled must change preferences without re-enabling it.
    source=max(tiled(),key=lambda w:w['target'][3]);x,y,w,h=source['target']
    assert call('TestPointer','resize',x+w*.75,y+h*.5)=='true'
    call('TestPointer','motion',x+w*.75-60,y+h*.5)
    apply(InnerGap=16);wait_for(lambda:not status()['dragging'] and status()['innerGap']==16);wait_for(settled)
    call('SetEnabled',False);wait_for(lambda:not tiled())
    apply(InnerGap=14,IgnoreTitles='*Slow C')
    wait_for(lambda:status()['innerGap']==14 and get(ident)['ignored'])
    assert not status()['enabled'] and not tiled()
    apply(InnerGap=10,IgnoreTitles='');call('SetEnabled',True)
    wait_for(lambda:len(tiled())==3);wait_for(settled);normal()
    report['apply_during_resize_and_disabled']='passed'
    return report
