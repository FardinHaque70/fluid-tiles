"""Check native KWin stacking and restoration, not just effect draw order."""
import json
import subprocess
import sys


def run_stacking_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    report={}
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def above_tiles(ident):
        s=status();floating=next(w for w in s['windows'] if w['id']==ident)
        return not floating['tiled'] and all(floating['stackingOrder']>w['stackingOrder'] for w in s['windows'] if w['tiled'])
    def activate_tiles(ident):
        for tile in list(tiled()):
            call('TestWindow',tile['id'],'activate')
            wait_for(lambda:above_tiles(ident))
    def close(proc,ident):
        if proc.poll() is None:proc.terminate()
        proc.wait(timeout=5)
        wait_for(lambda:all(w['id']!=ident for w in status()['windows']));wait_for(settled)
    def native_info(ident):
        output=subprocess.check_output(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.getWindowInfo','{'+ident+'}'],text=True)
        return dict(line.split(': ',1) for line in output.splitlines() if ': ' in line)

    wait_for(lambda:all(w['keepBelow'] and not w['keepAbove'] for w in tiled()))
    original=[w['id'] for w in tiled()]
    source=original[0]
    # Use the real Settings application from the user's report. Give the
    # existing test-only autoclose enough time for repeated tile activations.
    settings=subprocess.Popen([str(root/'build/fluidtiles-settings'),str(results/'stacking-settings.png')],
        env={**env,'FLUIDTILES_SETTINGS_TEST':'1','FLUIDTILES_SETTINGS_HOLD_MS':'12000'},stdout=log,stderr=log)
    children.append(settings)
    try:
        window=wait_for(lambda:next((w for w in status()['windows'] if w['class']=='org.fluidtiles.Settings'),None),5)
        ident=window['id'];wait_for(lambda:above_tiles(ident))
        if status().get('appearance',{}).get('enabled'):
            wait_for(lambda:get(ident)['appearance']['applied'] and get(ident)['appearance']['border']>0)
            assert not get(ident)['appearance']['noBorder'],'Default policy must retain floating Settings titlebar'
        activate_tiles(ident)
        x,y,w,h=get(ident)['actual'];point=(x+w/2,y+h/2)
        assert call('TestPointer','resize',*point)=='false','Resize grabbed a tile behind Settings'
        assert call('TestPointer','move',*point)=='false','Move grabbed a tile behind Settings'
        subprocess.run([str(root/'build/fluidtiles-input'),str(point[0]),str(point[1]),'click'],
            env=env,check=True,stdout=log,stderr=log,timeout=4)
        wait_for(lambda:status()['activeWindow']==ident)
        (results/'stacking-settings-state.json').write_text(json.dumps(status(),indent=2)+'\n')
        report['settings_above_tiles_and_receives_clicks']='passed'
        call('TestWindow',source,'activate');call('TestWindow',source,'fullscreen')
        wait_for(lambda:get(source)['fullscreen'] and not get(source)['keepBelow'])
        wait_for(lambda:get(source)['stackingOrder']>get(ident)['stackingOrder'])
        call('TestWindow',source,'unfullscreen');wait_for(lambda:get(source)['tiled']);wait_for(settled)
        report['native_fullscreen_above_floats']='passed'
    finally:
        if settings.poll() is None:settings.terminate()
        settings.wait(timeout=4)
    wait_for(lambda:all(w['class']!='org.fluidtiles.Settings' for w in status()['windows']))

    call('TestWindow',source,'activate');call('Action','float')
    wait_for(lambda:get(source)['manualFloating'] and above_tiles(source))
    activate_tiles(source)
    call('TestWindow',source,'activate');call('Action','float')
    wait_for(lambda:get(source)['tiled'] and get(source)['keepBelow']);wait_for(settled)
    report['manual_float_and_retile_layers']='passed'

    fixed=subprocess.Popen([str(root/'build/fluidtiles-demo'),'Stack fixed','#9c640c','0','fixed'],env=env,stdout=log,stderr=log)
    children.append(fixed)
    floating=wait_for(lambda:next((w for w in status()['windows'] if w['title'].endswith('Stack fixed')),None))['id']
    try:
        activate_tiles(floating)
        report['excluded_fixed_client_above_tiles']='passed'
        # Explicit native flags changed during tiling must survive disabling.
        call('TestWindow',source,'keep-above')
        wait_for(lambda:get(source)['keepBelow'] and not get(source)['keepAbove'])
        call('TestWindow',floating,'keep-below')
        wait_for(lambda:not get(floating)['keepBelow'] and get(floating)['layerOwned'])
        activate_tiles(floating)
        call('SetEnabled',False);wait_for(lambda:not tiled())
        assert get(source)['keepAbove'] and not get(source)['keepBelow']
        assert get(floating)['keepBelow'] and not get(floating)['layerOwned']
        assert all(not get(i)['keepBelow'] for i in original)
        report['disable_restores_native_layer_preferences']='passed'
        call('SetEnabled',True);wait_for(lambda:len(tiled())==3);wait_for(settled)
        activate_tiles(floating)
        # True native effect unload must restore flags too, independent of the
        # renderer and its diagnostics endpoint.
        subprocess.run([sys.executable,str(root/'tools/control.py'),'stop'],check=True,stdout=log,stderr=log,timeout=12)
        info={i:native_info(i) for i in [source,floating]}
        (results/'stacking-after-unload.json').write_text(json.dumps(info,indent=2)+'\n')
        assert info[source].get('keepAbove')=='true' and info[source].get('keepBelow')=='false',info
        assert info[floating].get('keepBelow')=='true',info
        fixed.terminate();fixed.wait(timeout=4)
        subprocess.run([sys.executable,str(root/'tools/control.py'),'start'],check=True,stdout=log,stderr=log,timeout=15)
        wait_for(lambda:len(tiled())==3);wait_for(settled)
        call('TestWindow',source,'normal-layer')
        wait_for(lambda:get(source)['keepBelow'] and not get(source)['keepAbove'])
        call('SetEnabled',False);wait_for(lambda:not tiled())
        assert all(not get(i)['keepAbove'] and not get(i)['keepBelow'] for i in original)
        call('SetEnabled',True);wait_for(lambda:len(tiled())==3);wait_for(settled)
        report['unload_and_reenable_restore_layers']='passed'
    finally:
        if fixed.poll() is None:close(fixed,floating)
    return report
