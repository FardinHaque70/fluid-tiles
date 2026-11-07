#!/usr/bin/env python3
"""Exercise the real plugin in a disposable KWin/Wayland process.

No host kwinrc, plugin installation, compositor restart, or user window is touched.
Synthetic drags use the same split handling as the input filter; physical input
routing and subjective smoothness still require a live interaction check.
"""
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import hashlib
from audit_behaviors import run_audit, run_native_resize, run_unload_audit
from settings_behaviors import run_settings_audit
from admission_behaviors import run_popup_audit, run_fifth_audit
from stacking_behaviors import run_stacking_audit
from insertion_behaviors import run_insertion_audit
from appearance_behaviors import run_appearance_audit
from plasma_appearance_behaviors import run_plasma_appearance_audit
from floating_behaviors import run_floating_audit
from interaction_behaviors import run_interaction_audit
from pair_behaviors import run_pair_audit
from opening_behaviors import run_opening_audit
from single_behaviors import run_single_audit
from animation_behaviors import run_animation_audit, run_activation_update_audit
from last_close_behaviors import run_last_close_audit
from effect_ownership_behaviors import run_effect_ownership_audit

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / os.environ.get("FLUIDTILES_TEST_RESULTS","test-results")

def call(method, *args):
    return subprocess.check_output(
        ["qdbus6", "org.kde.KWin", "/org/kde/FluidTiles", "org.kde.FluidTiles." + method,
         *[str(a).lower() if isinstance(a, bool) else str(a) for a in args]],
        text=True, stderr=subprocess.PIPE, timeout=5).strip()

def status():
    return json.loads(call("Status"))

def wait_for(predicate, seconds=12):
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        try:
            last = predicate()
            if last:
                return last
        except (subprocess.SubprocessError, json.JSONDecodeError):
            pass
        time.sleep(.05)
    try:
        (RESULTS/"failure-status.json").write_text(json.dumps(status(),indent=2))
    except Exception:
        pass
    raise AssertionError(f"Timed out waiting for compositor state; last={last}")

def tiled():
    return [w for w in status()["windows"] if w["tiled"]]

def settled():
    s = status()
    if s["animating"]:
        return False
    return all(max(abs(a-b) for a,b in zip(w["actual"],w["target"])) < 2
               for w in s["windows"] if w["tiled"] and w["visible"])

def capture(name):
    subprocess.run([str(ROOT/"build/fluidtiles-capture"), str(RESULTS/name)], check=True,
                   env={**os.environ,"QT_QPA_PLATFORM":"offscreen"}, timeout=12,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

def check_no_black_content(name):
    s=status()
    check={"output":s["outputs"][0]["geometry"],"windows":[w["visual"] for w in s["windows"] if w["tiled"]]}
    subprocess.run([str(ROOT/"build/fluidtiles-capture"),str(RESULTS/name),json.dumps(check)],check=True,
        env={**os.environ,"QT_QPA_PLATFORM":"offscreen"},timeout=12,stdout=subprocess.DEVNULL)

def inner():
    children=[]
    log=open(RESULTS/"kwin.log","w")
    kwin=subprocess.Popen(["kwin_wayland","--virtual","--width",os.environ.get("FLUIDTILES_TEST_WIDTH",("2648" if os.environ.get("FLUIDTILES_TEST_FIFTH")=="1" else "1600")),
        "--height",os.environ.get("FLUIDTILES_TEST_HEIGHT",("1448" if os.environ.get("FLUIDTILES_TEST_FIFTH")=="1" else "1000")),
        "--scale",os.environ.get("FLUIDTILES_TEST_SCALE","1.45"),
        "--no-lockscreen","--no-kactivities","--no-global-shortcuts","--socket","fluidtiles-test"],
        stdout=log,stderr=log)
    try:
        if os.environ.get("FLUIDTILES_TEST_CONTROL")=="1":
            def scripting_ready():
                return subprocess.run(["qdbus6","org.kde.KWin","/Scripting",
                    "org.kde.kwin.Scripting.isScriptLoaded","fluidtiles-controller"],
                    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
            wait_for(scripting_ready)
            # Upgrade existing manual settings once, preserving any preferences
            # already in the standard script group. Defaults must stay defaults
            # on subsequent installations rather than reimporting legacy keys.
            for group,key,value in [('Effect-fluidtiles','InnerGap','14'),('Effect-fluidtiles','OuterGap','9'),
                                    ('Effect-fluidtiles','SpringFrequency','30'),('Script-fluidtiles-controller','InnerGap','7')]:
                subprocess.run(['kwriteconfig6','--file','kwinrc','--group',group,'--key',key,value],check=True)
            subprocess.run([sys.executable,str(ROOT/"tools/control.py"),"install"],check=True,stdout=log,stderr=log,timeout=15)
            migrated_animation=json.loads(subprocess.check_output(['kreadconfig6','--file','kwinrc','--group','Script-fluidtiles-controller','--key','AnimationSettings'],text=True))
            assert migrated_animation['preset']=='fluid'
            backup=next(p for p in migrated_animation['customPresets'] if p['id']=='classic-previous')
            assert backup['profiles']['open']['curve']['stiffness']==900,'Previous animation preference was not backed up'
            for key,value in [('InnerGap','7'),('OuterGap','9'),('SpringFrequency','30')]:
                assert subprocess.check_output(['kreadconfig6','--file','kwinrc','--group','Script-fluidtiles-controller',
                    '--key',key],text=True).strip()==value
            obsolete=subprocess.run([sys.executable,str(ROOT/"tools/control.py"),"try"],stdout=log,stderr=log,timeout=10)
            assert obsolete.returncode!=0
            assert subprocess.check_output(["kreadconfig6","--file","kwinrc","--group","Plugins",
                "--key","fluidtiles-controllerEnabled"],text=True).strip()=="false"
            installed=Path(os.environ["XDG_DATA_HOME"])/"kwin/scripts/fluidtiles-controller/metadata.json"
            metadata=installed.read_text()
            invalid=json.loads(metadata);invalid["KPlugin"]["Version"]="audit-incompatible"
            installed.write_text(json.dumps(invalid))
            try:
                failed=subprocess.run([sys.executable,str(ROOT/"tools/control.py"),"start"],stdout=log,stderr=log,timeout=15)
                assert failed.returncode!=0
                assert subprocess.check_output(["kreadconfig6","--file","kwinrc","--group","Plugins",
                    "--key","fluidtiles-controllerEnabled"],text=True).strip()=="false"
                assert subprocess.check_output(["qdbus6","org.kde.KWin","/Scripting",
                    "org.kde.kwin.Scripting.isScriptLoaded","fluidtiles-controller"],text=True).strip()=="false"
            finally:installed.write_text(metadata)
            subprocess.run([sys.executable,str(ROOT/"tools/control.py"),"start"],check=True,stdout=log,stderr=log,timeout=15)
        # The runtime must use the same identity and setting as System Settings.
        assert subprocess.check_output(["kreadconfig6","--file","kwinrc","--group","Plugins",
            "--key","fluidtiles-controllerEnabled"],text=True).strip()=="true"
        wait_for(lambda: status()["enabled"])
        assert subprocess.check_output(["qdbus6","org.kde.KWin","/Scripting",
            "org.kde.kwin.Scripting.isScriptLoaded","fluidtiles-loader-trial"],text=True).strip()=="false"
        env={**os.environ,"QT_QPA_PLATFORM":"wayland","WAYLAND_DISPLAY":"fluidtiles-test",
             "QT_QPA_PLATFORMTHEME":"generic","QT_NO_XDG_DESKTOP_PORTAL":"1"}
        requested_scale=float(os.environ.get("FLUIDTILES_TEST_SCALE","1.45"))
        subprocess.run(["kscreen-doctor",f"output.Virtual-0.scale.{requested_scale}"],
            env=env,check=True,stdout=log,stderr=log,timeout=10)
        wait_for(lambda:abs(status()["outputs"][0]["scale"]-requested_scale)<.001)
        (RESULTS/"support.txt").write_text(subprocess.check_output(
            ["qdbus6","org.kde.KWin","/KWin","org.kde.KWin.supportInformation"],text=True))
        if os.environ.get('FLUIDTILES_TEST_EFFECT_OWNERSHIP')=='1':
            report=run_effect_ownership_audit((call,status,wait_for,tiled,settled,ROOT),env,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_LAST_CLOSE')=='1':
            report=run_last_close_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_PLASMA_APPEARANCE')=='1':
            report=run_plasma_appearance_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps({'pass':report['pass'],'cases':report['cases']}),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_SINGLE')=='1':
            report=run_single_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_ANIMATIONS')=='1':
            report=run_animation_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps({'pass':report['pass'],'presets':list(report['presets']),'report':str(RESULTS/'animations.json')}),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_ACTIVATION_UPDATE')=='1':
            report=run_activation_update_audit((call,status,wait_for,tiled,settled,ROOT),env,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_OPENING')=='1':
            report=run_opening_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get("FLUIDTILES_TEST_FIFTH")=="1":
            if os.environ.get('FLUIDTILES_TEST_APPEARANCE_FIFTH')=='1':
                subprocess.run([str(ROOT/'build/fluidtiles-settings-tests'),'apply','{"AppearanceEnabled":true}'],
                    env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
                wait_for(lambda:status()['appearance']['enabled'])
            run_fifth_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print("Fifth-window regression passed",flush=True);return
        if os.environ.get('FLUIDTILES_TEST_INSERTION_ONLY')=='1':
            report=run_insertion_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_APPEARANCE')=='1':
            report=run_appearance_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_PAIRS')=='1':
            report=run_pair_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_INTERACTIONS')=='1':
            report=run_interaction_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        for label,color,delay in [("A","#21618c",0),("B","#7d3c98",0),("Slow C","#117864",80)]:
            children.append(subprocess.Popen([str(ROOT/"build/fluidtiles-demo"),label,color,str(delay)],
                env=env,stdout=log,stderr=log))
            try:
                wait_for(lambda: len(tiled())==len(children),30)
            except AssertionError:
                print("Test application exit codes:",[p.poll() for p in children],file=sys.stderr)
                raise
            wait_for(settled)
        if os.environ.get('FLUIDTILES_TEST_FLOAT_CENTER')=='1':
            report=run_floating_audit((call,status,wait_for,tiled,settled,ROOT),env,children,log,RESULTS)
            print(json.dumps(report),flush=True);return
        if os.environ.get('FLUIDTILES_TEST_UNLOAD_ONLY')=='1':
            report=run_unload_audit((call,status,wait_for,tiled,settled,ROOT),log,RESULTS)
            print(json.dumps(report),flush=True)
            return
        initial=status()
        assert len(tiled())==3
        def check_three_window_dwindle():
            windows=tiled()
            tallest=max(w["target"][3] for w in windows)
            full=[w for w in windows if abs(w["target"][3]-tallest)<1]
            assert len(full)==1,"Three windows must form a full-height tile beside two stacked tiles"
            stacked=[w["target"] for w in windows if w not in full]
            a,b=sorted(stacked,key=lambda r:r[1])
            assert abs(a[0]-b[0])<1 and abs(a[2]-b[2])<1,(a,b)
            assert abs(a[1]+a[3]+status()["innerGap"]-b[1])<1,(a,b)
        check_three_window_dwindle()
        # Re-enabling with three existing clients batches their insertion into
        # one reconcile; this previously produced three columns from empty boxes.
        call("SetEnabled",False)
        wait_for(lambda:not tiled())
        call("SetEnabled",True)
        wait_for(lambda:len(tiled())==3);wait_for(settled)
        check_three_window_dwindle()
        audit_api=(call,status,wait_for,tiled,settled,ROOT)
        if os.environ.get('FLUIDTILES_TEST_STACKING_ONLY')=='1':
            report=run_stacking_audit(audit_api,env,children,log,RESULTS)
            (RESULTS/'stacking.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report),flush=True);return
        admission_report=run_popup_audit(audit_api,env,children,log,RESULTS)
        settings_report=run_settings_audit(audit_api,env,children,log,RESULTS)
        if os.environ.get('FLUIDTILES_TEST_SETTINGS_ONLY')=='1':
            (RESULTS/'settings.json').write_text(json.dumps(settings_report,indent=2)+'\n')
            print(json.dumps(settings_report),flush=True);return
        stacking_report=run_stacking_audit(audit_api,env,children,log,RESULTS)
        audit_report=run_audit(audit_api,env,children,log)
        native_resize_report=run_native_resize(audit_api,env,log)
        initial=status()
        capture("01-tiled.png")
        # Choose the leftmost, full-height tile. Resize its vertical boundary.
        left=min(tiled(),key=lambda w:w["target"][0])
        x,y,width,height=left["target"]
        start=(x+width*.75,y+height*.5)
        assert call("TestPointer","resize",*start)=="true"
        maximum_visual_error=0
        request_start=sum(w["requests"] for w in tiled())
        points=[]
        begin=time.monotonic()
        # Two seconds of drag with immediate reversals at ~120 input updates/s.
        for i in range(241):
            px=start[0]+140*math.sin(i/240*math.tau*2)
            call("TestPointer","motion",px,start[1])
            if i%12==0:
                s=status()
                for w in s["windows"]:
                    if w["tiled"]:
                        maximum_visual_error=max(maximum_visual_error,max(abs(a-b) for a,b in zip(w["visual"],w["target"])))
                        assert w["visual"][2]>0 and w["visual"][3]>0
                points.append(s)
            if i==65:
                check_no_black_content("02-resizing.png")
            time.sleep(max(0,begin+i/120-time.monotonic()))
        assert call("TestPointer","release",0,0)=="true"
        wait_for(settled)
        after_drag=status()
        assert maximum_visual_error<.001, maximum_visual_error
        # Exercise the actual Wayland input device -> KWin filter route too.
        # The helper refuses to connect to any socket except fluidtiles-test.
        left=min(tiled(),key=lambda w:w["target"][0])
        x,y,width,height=left["target"]
        input_report={}
        for mode,px,py in [("meta",x+width*.75,y+height*.5),
                           ("divider",x+width+5,y+height*.25)]:
            proc=subprocess.Popen([str(ROOT/"build/fluidtiles-input"),str(px),str(py),mode],env=env,stdout=log,stderr=log)
            try:
                wait_for(lambda:status()["dragging"],4)
                active=status()
                time.sleep(.4)
                middle=status()
                assert any(abs(a["target"][2]-b["target"][2])>5
                    for a,b in zip(active["windows"],middle["windows"]) if a["tiled"] and b["tiled"])
                capture("05-input-"+mode+".png")
                assert proc.wait(timeout=5)==0
                wait_for(lambda:not status()["dragging"])
                wait_for(settled)
                input_report[mode]=status()
            finally:
                if proc.poll() is None:proc.terminate();proc.wait(timeout=3)
        # Interrupt layout springs repeatedly, then demand actual convergence.
        call("ResetMetrics")
        for i in range(12):
            call("Action","split")
            time.sleep(.035)
        wait_for(settled)
        after_interrupt=status()
        capture("03-retargeted.png")
        before={w["id"]:w["target"] for w in tiled()}
        source,destination=tiled()[:2]
        def center(w):
            x,y,width,height=w["target"];return x+width/2,y+height/2
        assert call("TestPointer","move",*center(source))=="true"
        assert call("TestPointer","motion",*center(destination))=="true"
        assert call("TestPointer","release",0,0)=="true"
        wait_for(settled)
        after={w["id"]:w["target"] for w in tiled()}
        assert after[source["id"]]==before[destination["id"]]
        assert after[destination["id"]]==before[source["id"]]
        # Regression: a normal server-decorated titlebar drag must retain the
        # source tile, swap on drop, and snap back when Escape cancels the move.
        native_drag_report={}
        for mode in ["titlebar","titlebar-cancel","titlebar-left","titlebar-right","titlebar-top","titlebar-bottom","meta-move"]:
            source,destination=tiled()[:2]
            before={w["id"]:w["target"] for w in tiled()}
            x,y,width,height=source["actual"]
            drop=center(destination)
            dx,dy,dw,dh=destination["target"]
            if mode=="titlebar-left":drop=(dx+dw*.05,dy+dh*.5)
            if mode=="titlebar-right":drop=(dx+dw*.95,dy+dh*.5)
            if mode=="titlebar-top":drop=(dx+dw*.5,dy+dh*.05)
            if mode=="titlebar-bottom":drop=(dx+dw*.5,dy+dh*.95)
            start_y=y+height/2 if mode=="meta-move" else y+12
            proc=subprocess.Popen([str(ROOT/"build/fluidtiles-input"),str(x+width/2),str(start_y),mode,
                *map(str,drop)],env=env,stdout=log,stderr=log)
            try:
                wait_for(lambda:status()["dragging"] if mode=="meta-move" else status().get("nativeDrag"),4)
                displacement=0
                while proc.poll() is None:
                    snapshot=status()
                    windows=[w for w in snapshot["windows"] if w["tiled"]]
                    assert len(windows)==3,"titlebar drag detached a tiled window"
                    moved=next(w for w in windows if w["id"]==source["id"])
                    if snapshot["dragging"]:
                        assert moved["target"]==before[source["id"]],"drag changed reserved layout before drop"
                        geometry=moved["visual"] if mode=="meta-move" else moved["actual"]
                        displacement=max(displacement,abs(geometry[0]-before[source["id"]][0]),
                            abs(geometry[1]-before[source["id"]][1]))
                    time.sleep(.03)
                assert proc.returncode==0
                assert displacement>40,"window did not move freely during titlebar drag"
                wait_for(lambda:not status()["dragging"]);wait_for(settled)
                after={w["id"]:w["target"] for w in tiled()}
                if mode in ["titlebar","meta-move"]:
                    assert after[source["id"]]==before[destination["id"]],(before,after)
                    assert after[destination["id"]]==before[source["id"]]
                elif mode=="titlebar-cancel":assert after==before,"Escape changed tile ordering"
                else:
                    a=after[source["id"]];b=after[destination["id"]]
                    scale=status()["outputs"][0]["scale"]
                    gap=math.floor(status()["innerGap"]*scale+.5)/scale
                    if mode=="titlebar-left":error=abs(a[0]+a[2]+gap-b[0])
                    elif mode=="titlebar-right":error=abs(b[0]+b[2]+gap-a[0])
                    elif mode=="titlebar-top":error=abs(a[1]+a[3]+gap-b[1])
                    else:error=abs(b[1]+b[3]+gap-a[1])
                    assert error<.01,(mode,a,b,gap)
                native_drag_report[mode]=status()
            finally:
                if proc.poll() is None:proc.terminate();proc.wait(timeout=3)
        subject=tiled()[-1]["id"]
        for operation,expected in [("minimize",2),("restore",3),("fullscreen",2),("unfullscreen",3)]:
            assert call("TestWindow",subject,operation)=="true"
            wait_for(lambda:len(tiled())==expected);wait_for(settled)
        # Close an application and verify the tree fills the resulting hole.
        closing=next(p for p in reversed(children) if p.poll() is None)
        closing.terminate(); closing.wait(timeout=5)
        wait_for(lambda:len(tiled())==2); wait_for(settled)
        capture("04-after-close.png")
        # Every visible window must be restored when disabled, including after
        # receiving delayed asynchronous buffers from an earlier layout.
        call("SetEnabled",False)
        wait_for(lambda: all(max(abs(a-b) for a,b in zip(w["actual"],w["restore"]))<2
            for w in status()["windows"] if w["class"]=="fluidtiles-demo"))
        restored=status()
        assert not any(w["tiled"] for w in restored["windows"])
        call("SetEnabled",True); wait_for(lambda:len(tiled())==2); wait_for(settled)
        if os.environ.get("FLUIDTILES_TEST_CONTROL")=="1":
            subprocess.run([sys.executable,str(ROOT/"tools/control.py"),"stop"],check=True,stdout=log,stderr=log,timeout=10)
            subprocess.run([sys.executable,str(ROOT/"tools/control.py"),"start"],check=True,stdout=log,stderr=log,timeout=15)
            wait_for(lambda:len(tiled())==2);wait_for(settled)
        # Test the user-local QML loader's lifetime, not just the native effect.
        for enabled in [False,True]:
            subprocess.run(["kwriteconfig6","--file","kwinrc","--group","Plugins",
                "--key","fluidtiles-controllerEnabled",str(enabled).lower()],check=True)
            subprocess.run(["qdbus6","org.kde.KWin","/KWin","org.kde.KWin.reconfigure"],check=True,stdout=log)
            if enabled:
                wait_for(lambda:status()["enabled"]);wait_for(lambda:len(tiled())==2);wait_for(settled)
                package=Path(os.environ['XDG_DATA_HOME'])/'kwin/scripts/fluidtiles-controller'
                revision=json.loads((package/'metadata.json').read_text()).get('X-FluidTiles-Revision')
                if revision:
                    assert Path(status()['libraryPath']).resolve()==(package/'versions'/revision/'contents/native/kwin/effects/plugins/fluidtiles.so').resolve()
            else:
                def unloaded():
                    loaded=subprocess.check_output(["qdbus6","org.kde.KWin","/Effects","org.kde.kwin.Effects.loadedEffects"],text=True)
                    return "fluidtiles" not in loaded.splitlines()
                wait_for(unloaded)
                assert subprocess.check_output(["qdbus6","org.kde.KWin","/Scripting",
                    "org.kde.kwin.Scripting.isScriptLoaded","fluidtiles-controller"],text=True).strip()=="false"
        native_unload_report=run_unload_audit((call,status,wait_for,tiled,settled,ROOT),log,RESULTS)
        # Change the installed payload while the fixed Settings entry point has
        # already been cached by Qt. Re-enable via KWin reconfigure, not the CLI,
        # and prove the actual mapped native library uses the new revision.
        update_verified=False
        if os.environ.get('FLUIDTILES_TEST_CONTROL')=='1':
            old_revision=revision
            marker=ROOT/'build/package/audit-update.txt'
            animation_before_update=status()['animation']['settings']
            enabled_before_update=status()['enabled']
            assert not marker.exists()
            try:
                marker.write_text('Disposable integration update marker\n')
                subprocess.run([sys.executable,str(ROOT/'tools/control.py'),'install'],check=True,stdout=log,stderr=log,timeout=15)
            finally:marker.unlink(missing_ok=True)
            revision=json.loads((package/'metadata.json').read_text())['X-FluidTiles-Revision']
            assert revision!=old_revision
            assert status()['enabled']==enabled_before_update,'Update changed the current activation state'
            subprocess.run(['kwriteconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled','true'],check=True)
            subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log)
            wait_for(lambda:status()['enabled']);wait_for(lambda:len(tiled())==2);wait_for(settled)
            assert Path(status()['libraryPath']).resolve()==(package/'versions'/revision/'contents/native/kwin/effects/plugins/fluidtiles.so').resolve()
            assert status()['innerGap']==10 and status()['outerGap']==10 and status()['springFrequency']==24,'Reinstall reimported legacy values over Defaults'
            assert status()['animation']['settings']==animation_before_update,'Reinstall changed animation preferences'
            update_verified=True
        report={"pass":True,"scale":os.environ.get("FLUIDTILES_TEST_SCALE","1.45"),
            "native_binary_sha256":hashlib.sha256((ROOT/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest(),
            "maximum_drag_visual_target_error":maximum_visual_error,
            "drag_request_count":sum(w["requests"] for w in after_drag["windows"])-request_start,
            "initial":initial,"drag":after_drag,"interrupt":after_interrupt,
            "wayland_input":input_report,
            "native_titlebar":native_drag_report,
            "behavior_audit":audit_report,"native_resize":native_resize_report,
            "settings":settings_report,"admission":admission_report,"stacking":stacking_report,
            "settings_update_payload_verified":update_verified,
            "native_state_after_unload":native_unload_report,
            "restored":restored,"final":status(),
            "limitations":["Wayland fake-input exercises compositor routing; physical mouse feel still needs user evaluation",
                "Virtual-output presentation timing is not a physical 144 Hz benchmark",
                "Slow demo blocks each client paint for 80 ms; not a full Wayland protocol compliance test"]}
        (RESULTS/"integration.json").write_text(json.dumps(report,indent=2))
        (RESULTS/"failure-status.json").unlink(missing_ok=True)
        print(json.dumps({k:report[k] for k in ["pass","scale","maximum_drag_visual_target_error","drag_request_count"]}),flush=True)
    finally:
        for p in children:
            if p.poll() is None: p.terminate()
        for p in children:
            try:p.wait(timeout=3)
            except subprocess.TimeoutExpired:p.kill();p.wait()
        kwin.terminate()
        try:kwin.wait(timeout=6)
        except subprocess.TimeoutExpired:kwin.kill();kwin.wait()
        log.close()

def main():
    if "--inner" in sys.argv:
        inner();return
    RESULTS.mkdir(parents=True,exist_ok=True)
    (RESULTS/'integration.json').unlink(missing_ok=True)
    (RESULTS/'failure-status.json').unlink(missing_ok=True)
    # All paths are disposable and project-owned; never override HOME.
    session=ROOT/os.environ.get("FLUIDTILES_TEST_SESSION","test-session")
    session.mkdir(exist_ok=True)
    for name in ["runtime","config","data","cache"]:
        (session/name).mkdir(exist_ok=True,mode=0o700)
    (session/"config/kdeglobals").write_text("[KDE]\nAnimationDurationFactor="+os.environ.get("FLUIDTILES_TEST_ANIMATION_FACTOR","1")+"\n")
    shutil.copytree(ROOT/"build/package",session/"data/kwin/scripts/fluidtiles-controller",dirs_exist_ok=True)
    package=session/"data/kwin/scripts/fluidtiles-controller"
    metadata=json.loads((package/"metadata.json").read_text())
    metadata["X-KDE-ConfigModule"]=str(package/"contents/native/kcm_fluidtiles.so")
    (package/"metadata.json").write_text(json.dumps(metadata,indent=2)+'\n')
    (session/"config/kwinrc").write_text("""[Plugins]
fluidtilesEnabled=false
fluidtiles-controllerEnabled=true
blurEnabled=false
scaleEnabled=false
maximizeEnabled=false
fullscreenEnabled=false
squashEnabled=false
slideEnabled=false
fadingpopupsEnabled=false
blendchangesEnabled=false
[Script-fluidtiles-controller]
AppearanceEnabled=false
[Desktops]
Number=2
[Compositing]
GLCore=true
""")
    if os.environ.get("FLUIDTILES_TEST_DESKTOP_EFFECTS")=="1":
        p=session/"config/kwinrc"
        p.write_text(p.read_text().replace("scaleEnabled=false","scaleEnabled=true").replace("blurEnabled=false","blurEnabled=true"))
    if os.environ.get("FLUIDTILES_TEST_INTERACTIONS")=="1":
        p=session/"config/kwinrc"
        p.write_text(p.read_text().replace("slideEnabled=false","slideEnabled=true").replace("Number=2","Number=2\nRows=1"))
    if os.environ.get("FLUIDTILES_TEST_CONTROL")=="1":
        p=session/"config/kwinrc"
        p.write_text(p.read_text().replace("fluidtiles-controllerEnabled=true","fluidtiles-controllerEnabled=false"))
    if os.environ.get('FLUIDTILES_TEST_SINGLE')!='1':
        p=session/'config/kwinrc'
        p.write_text(p.read_text().replace('[Script-fluidtiles-controller]','[Script-fluidtiles-controller]\nExpandSingleWindow=false'))
    env={**os.environ,"XDG_RUNTIME_DIR":str(session/"runtime"),"XDG_CONFIG_HOME":str(session/"config"),
        "XDG_DATA_HOME":str(session/"data"),"XDG_CACHE_HOME":str(session/"cache"),
        "QT_QPA_PLATFORM":"offscreen",
        "FLUIDTILES_TEST_MODE":"1","KWIN_COMPOSE":"O2",
        "FLUIDTILES_BASELINE":("0" if os.environ.get("FLUIDTILES_TEST_APPEARANCE")=="1" else "1")}
    env.pop("SESSION_MANAGER",None)
    env.pop("QT_PLUGIN_PATH",None)
    env.pop("WAYLAND_DISPLAY",None)
    env.pop("DISPLAY",None)
    result=subprocess.run(["dbus-run-session","--",sys.executable,__file__,"--inner"],env=env)
    raise SystemExit(result.returncode)

if __name__=="__main__":main()
