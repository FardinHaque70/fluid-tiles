"""Inspect every painted frame across a real Dolphin closing handoff."""
import json, subprocess, time

def run_last_close_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    document=dict(version=1,preset='fluid',enabled=True,speed=.5,overrides={},customPresets=[])
    subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps({'AnimationSettings':document,'ExpandSingleWindow':True,'AppearanceEnabled':True})],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log)
    panel=subprocess.Popen([str(root/'build/fluidtiles-panel'),'close-flash-background','desktop'],env=env,stdout=log,stderr=log);children.append(panel)
    folder=results/'dolphin-folder';folder.mkdir(exist_ok=True)
    p=subprocess.Popen(['dolphin','--new-window',str(folder)],env=env,stdout=log,stderr=log);children.append(p)
    ident=wait_for(lambda:next((w['id'] for w in tiled() if w['class']=='org.kde.dolphin'),None),20)
    wait_for(settled);time.sleep(.15)
    call('TestWindow',ident,'close')
    wait_for(lambda:status()['animation']['closingVisuals']==0 and not tiled())
    # Request another presentation so the recorded sequence includes the frame
    # after cleanup too. The probe itself does not schedule any animation work.
    subprocess.run([str(root/'build/fluidtiles-capture'),str(results/'after.png')],env={**env,'QT_QPA_PLATFORM':'offscreen'},check=True,stdout=log,stderr=log,timeout=10)
    wait_for(lambda:sum(not f['closing'] for f in status()['animation']['testCloseFrames'])>=3)
    frames=status()['animation']['testCloseFrames']
    assert any(f['closing'] for f in frames) and any(not f['closing'] for f in frames),frames
    flashes=[i for i,f in enumerate(frames) if not f['closing'] and f['rgb'][2]>100]
    report={'pass':not flashes,'flashFrames':flashes,'frames':frames}
    (results/'closing-flash.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)
    if not flashes:return report
    if not __import__('os').environ.get('FLUIDTILES_TEST_ALLOW_FLASH'):raise AssertionError('Full-opacity flash after fade: '+str(flashes))
    return report
