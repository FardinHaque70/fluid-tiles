"""Exclusive Scale/Fade ownership through real activation/configuration paths."""
import hashlib,json,subprocess,sys,time

def run_effect_ownership_audit(api,env,log,results):
    call,status,wait_for,tiled,settled,root=api
    assert env['WAYLAND_DISPLAY']=='fluidtiles-test'
    def run(*args):return subprocess.check_output([str(a) for a in args],env=env,text=True,stderr=log).strip()
    def read(key):return run('kreadconfig6','--file','kwinrc','--group','Plugins','--key',key,'--default','MISSING')
    def loaded(name):return run('qdbus6','org.kde.KWin','/Effects','org.kde.kwin.Effects.isEffectLoaded',name)=='true'
    def config(scale,fade):
        for name,value in [('scale',scale),('fade',fade)]:
            run('kwriteconfig6','--file','kwinrc','--group','Plugins','--key',name+'Enabled',str(value).lower())
            run('qdbus6','org.kde.KWin','/Effects','org.kde.kwin.Effects.'+('loadEffect' if value else 'unloadEffect'),name)
        wait_for(lambda:loaded('scale')==scale and loaded('fade')==fade)
    def exclusive():return read('scaleEnabled')==read('fadeEnabled')=='false' and not loaded('scale') and not loaded('fade')
    def stop():run(sys.executable,root/'tools/control.py','stop')
    def start():run(sys.executable,root/'tools/control.py','start');wait_for(exclusive)
    report={};animation=status()['animation']['settings']
    wait_for(exclusive);report['activation_disables_config_and_unloads_effects']='passed'
    call('SetEnabled',False)
    wait_for(lambda:read('scaleEnabled')=='true' and loaded('scale'))
    assert read('fadeEnabled')=='MISSING' and not loaded('fade')
    report['emergency_disable_restores_enabled_and_implicit_disabled_states']='passed'
    call('SetEnabled',True);wait_for(exclusive)
    # Apply attempts to enable them in KDE Desktop Effects while Fluid is active.
    for name in ['scale','fade']:run('kwriteconfig6','--file','kwinrc','--group','Plugins','--key',name+'Enabled','true')
    run('qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure')
    wait_for(exclusive);time.sleep(.35);assert exclusive()
    report['desktop_effects_apply_keeps_single_owner']='passed'
    stop();wait_for(lambda:read('scaleEnabled')=='true' and loaded('scale'))
    assert read('fadeEnabled')=='MISSING' and not loaded('fade')
    report['script_unload_restores_original_choices']='passed'
    for scale,fade in [(False,True),(False,False),(True,True)]:
        config(scale,fade);start();stop()
        wait_for(lambda:loaded('scale')==scale and loaded('fade')==fade)
        assert read('scaleEnabled')==str(scale).lower() and read('fadeEnabled')==str(fade).lower()
    report['mixed_and_disabled_preferences_survive_restart']='passed'
    # A loaded but emergency-disabled controller must leave KDE effects alone.
    start();call('SetEnabled',False);stop()
    run(sys.executable,'-c',"import sys;sys.path.insert(0,'tools');import control;control.start(enable_tiling=False)")
    wait_for(lambda:status() is not None)
    assert not status()['enabled'] and loaded('scale') and loaded('fade')
    report['disabled_controller_does_not_claim_effects']='passed'
    assert status()['animation']['settings']==animation
    report['animation_document_unchanged']='passed'
    result={'pass':True,'cases':report,'nativeSha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
    (results/'effect-ownership.json').write_text(json.dumps(result,indent=2)+'\n');return result
