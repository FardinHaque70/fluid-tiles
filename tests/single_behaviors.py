"""A lone tile fills the work area, with real panel reservations and restoration."""
import hashlib
import json
import subprocess
import time


def run_single_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    report={}
    scale=status()['outputs'][0]['scale']
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def near(a,b):return max(abs(x-y) for x,y in zip(a,b))<1.1/scale
    def apply(**values):
        subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps(values)],
            env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
        if 'ExpandSingleWindow' in values:wait_for(lambda:status()['expandSingleWindow']==values['ExpandSingleWindow'])
        wait_for(settled)
    def spawn(label,kind='',delay=0):
        p=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#21618c',str(delay),kind],env=env,stdout=log,stderr=log);children.append(p)
        ident=wait_for(lambda:next((w['id'] for w in status()['windows'] if w['title'].endswith(label)),None))
        wait_for(settled);return p,ident
    def close(p,ident):
        p.terminate();p.wait(timeout=5);wait_for(lambda:all(w['id']!=ident for w in status()['windows']));wait_for(settled)
    def expanded(ident):
        w=get(ident)
        return w['tiled'] and w['appearance']['expanded'] and near(w['actual'],w['workArea']) and not w['redirected']
    def assert_expanded(ident):
        wait_for(lambda:expanded(ident))
        w=get(ident)
        assert not w['fullscreen'] and not w['maximized'],'Automatic expansion changed native window state'
        assert w['appearance']['border']==0 and w['appearance']['radius']==[0,0,0,0]
    def pair():
        windows=[w for w in tiled() if w['visible']]
        return len(windows)==2 and all(not w['appearance']['expanded'] and w['appearance']['border']>0 and not w['redirected'] for w in windows)
    assert status()['expandSingleWindow'],'Missing configuration must default to enabled'
    subprocess.run([str(root/'build/fluidtiles-settings-tests'),str(results)],
        env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
    apply(AppearanceEnabled=True,InnerGap=10,OuterGap=12,SpringFrequency=12)
    assert (root/'build/fluidtiles-panel').exists(),'Single-window panel test requires LayerShellQt development files'
    panel=subprocess.Popen([str(root/'build/fluidtiles-panel')],env=env,stdout=log,stderr=log);children.append(panel)
    a,ia=spawn('Expanded A')
    wait_for(lambda:get(ia)['workArea'][1]>=47)
    assert_expanded(ia);original=get(ia)['restore']
    (results/'single-panel-state.json').write_text(json.dumps(status(),indent=2)+'\n')
    panel_state=next(w for w in status()['windows'] if w['class']=='fluidtiles-panel')
    assert not panel_state['tiled'] and not panel_state['appearance']['applied'],'Layer-shell panel received application appearance'
    image=results/'single-panel-and-expanded-tile.png'
    subprocess.run([str(root/'build/fluidtiles-capture'),str(image)],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_CAPTURE_NATIVE':'1'},check=True,stdout=log,stderr=log,timeout=12)
    spec={'logicalSize':status()['outputs'][0]['geometry'][2:],'points':[[813,24],[813,57]]}
    pixels=json.loads(subprocess.check_output([str(root/'build/fluidtiles-imagecheck'),str(image),json.dumps(spec)],text=True))['pixels']
    assert max(abs(x-y) for x,y in zip(pixels[0][:3],[255,204,0]))<12,('Panel covered',pixels)
    assert max(abs(x-y) for x,y in zip(pixels[1][:3],[33,97,140]))<12,('Gap below panel',pixels)
    report['default_enabled_real_panel_reservation_and_pixels']='passed'
    apply(ExpandSingleWindow=False)
    w=get(ia);area=w['workArea'];outer=w['appearance']['outer']
    assert not w['appearance']['expanded'] and w['appearance']['border']>0
    gap=round(12*status()['outputs'][0]['scale'])/status()['outputs'][0]['scale']
    assert abs(outer[0]-area[0]-gap)<1 and abs(outer[1]-area[1]-gap)<1
    assert w['restore']==original
    apply(ExpandSingleWindow=True);assert_expanded(ia)
    report['live_option_preserves_saved_geometry_and_normal_appearance']='passed'
    b,ib=spawn('Expanded slow B',delay=80);wait_for(pair)
    assert get(ia)['restore']==original
    close(b,ib);assert_expanded(ia)
    report['second_window_restores_spacing_and_closure_reexpands']='passed'
    blocked,blocked_id=spawn('Expanded capacity blocked','large-minimum')
    wait_for(lambda:get(blocked_id)['capacityBlocked'])
    assert_expanded(ia)
    assert not get(blocked_id)['tiled']
    close(blocked,blocked_id)
    report['infeasible_second_window_leaves_valid_single_expanded']='passed'
    floating,fi=spawn('Expanded floating dialog','authentication-prompt')
    call('TestWindow',fi,'activate');wait_for(lambda:status()['activeWindow']==fi)
    assert_expanded(ia);assert not get(fi)['tiled'] and get(fi)['ignored']
    assert status()['activeWindow']==fi,'Automatic expansion stole focus from a dialog'
    close(floating,fi)
    report['floating_dialog_keeps_focus_and_does_not_change_tile_count']='passed'
    call('TestWindow',ia,'activate');call('Action','float')
    wait_for(lambda:not get(ia)['tiled']);wait_for(settled)
    assert not get(ia)['appearance']['expanded'] and get(ia)['appearance']['border']>0
    call('TestWindow',ia,'activate');call('Action','float');wait_for(settled);assert_expanded(ia)
    original=get(ia)['restore']
    report['explicit_float_and_retile_restore_appearance']='passed'
    for op,undo in [('fullscreen','unfullscreen'),('maximize','unmaximize'),('minimize','restore')]:
        call('TestWindow',ia,op);wait_for(lambda:not get(ia)['tiled'])
        assert not get(ia)['appearance']['expanded']
        call('TestWindow',ia,undo);wait_for(settled);assert_expanded(ia)
        assert get(ia)['restore']==original
    report['native_state_round_trips_keep_independent_ownership']='passed'
    call('TestWindow',ia,'desktop-2');wait_for(lambda:not get(ia)['visible'])
    b,ib=spawn('Expanded desktop one');assert_expanded(ib)
    assert get(ia)['appearance']['expanded'],'Other desktop lost its single-tile policy'
    call('TestWindow',ia,'desktop-1');wait_for(pair)
    close(b,ib);assert_expanded(ia)
    report['independent_desktops_and_returning_second_window']='passed'
    call('TestWindow',ia,'activate');w=get(ia);x,y,width,height=w['actual']
    origin=(x+width*.8,y+height*.8)
    assert call('TestPointer','resize',*origin)=='true'
    call('TestPointer','motion',origin[0]-180,origin[1]-100)
    wait_for(lambda:get(ia)['actual'][2]<width-100)
    assert get(ia)['restore']==original
    call('TestPointer','release',0,0);wait_for(settled);assert_expanded(ia)
    assert call('TestPointer','resize',*origin)=='true'
    apply(ExpandSingleWindow=False);assert not status()['dragging']
    assert not get(ia)['appearance']['expanded']
    apply(ExpandSingleWindow=True);assert_expanded(ia)
    report['free_resize_return_and_configuration_interruption']='passed'
    call('SetEnabled',False);wait_for(lambda:not tiled())
    wait_for(lambda:near(get(ia)['actual'],original))
    assert not get(ia)['appearance']['expanded']
    call('SetEnabled',True);wait_for(settled);assert_expanded(ia)
    subprocess.run(['python3',str(root/'tools/control.py'),'stop'],check=True,stdout=log,stderr=log,timeout=12)
    def native_restored():
        info=subprocess.check_output(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.getWindowInfo','{'+ia+'}'],text=True)
        values=dict(line.split(': ',1) for line in info.splitlines() if ': ' in line)
        return near([float(values[key]) for key in ['x','y','width','height']],original)
    wait_for(native_restored)
    subprocess.run(['python3',str(root/'tools/control.py'),'start'],check=True,stdout=log,stderr=log,timeout=15)
    wait_for(settled);assert_expanded(ia)
    report['disable_and_true_unload_restore_native_geometry']='passed'
    close(a,ia)
    bounded,bid=spawn('Expanded bounded exception','single-bounded')
    wait_for(lambda:get(bid)['tiled'])
    assert not get(bid)['appearance']['expanded'] and get(bid)['actual'][2]<=1580+1
    close(bounded,bid)
    report['native_maximum_keeps_valid_ordinary_tile']='passed'
    csd,cid=spawn('Expanded application header','csd-system-move')
    assert_expanded(cid);assert not get(cid)['appearance']['titlebarOwned']
    time.sleep(1);before=status()['appearance'];time.sleep(1);after=status()['appearance']
    assert before['outlineDraws']==after['outlineDraws'] and before['captures']==after['captures']
    report['client_header_and_idle_rendering']='passed'
    close(csd,cid)
    panel.terminate();panel.wait(timeout=5)
    data={'pass':True,'cases':report,'scale':status()['outputs'][0]['scale'],
          'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
    (results/'single.json').write_text(json.dumps(data,indent=2)+'\n')
    return report
