#!/usr/bin/env python3
"""Build/install/control the project-owned, user-local KWin package."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
PLUGIN="fluidtiles-controller"
LEGACY_PLUGINS=("fluidtiles-loader","fluidtiles-loader-trial")

def run(*args,check=True):
    return subprocess.run([str(a) for a in args],text=True,check=check,capture_output=True)

def dbus(path,method,*args,check=True):
    return run("qdbus6","org.kde.KWin",path,method,*args,check=check).stdout.strip()

def installed_path():
    return Path(os.environ.get("XDG_DATA_HOME",str(Path.home()/".local/share")))/"kwin/scripts"/PLUGIN

def settings_launcher():
    return installed_path().parents[2]/"applications/org.fluidtiles.Settings.desktop"

def install_settings_launcher():
    launcher=settings_launcher()
    if launcher.exists() and "X-FluidTiles-Managed=true" not in launcher.read_text():
        raise RuntimeError("Refusing to replace an unrelated application launcher")
    # Desktop Exec quoting (no shell is involved).
    executable=str(installed_path()/"contents/native/fluidtiles-settings")
    quoted='"'+executable.replace('\\','\\\\\\\\').replace('"','\\\\"').replace('$','\\\\$').replace('`','\\\\`')+'"'
    launcher.parent.mkdir(parents=True,exist_ok=True)
    launcher.write_text("[Desktop Entry]\nType=Application\nName=Fluid Tiles Settings\n"
        "Comment=Configure tiling gaps, animation and ignored windows\nIcon=preferences-system-windows\n"
        f"Exec={quoted}\nTerminal=false\nCategories=Settings;DesktopSettings;\nX-FluidTiles-Managed=true\n")

def payload_path():
    dest=installed_path()
    revision=json.loads((dest/"metadata.json").read_text()).get("X-FluidTiles-Revision")
    if not revision:return dest
    if len(revision)!=64 or any(c not in "0123456789abcdef" for c in revision):
        raise RuntimeError("Invalid installed package revision")
    return dest/"versions"/revision

def set_autoload(enabled):
    run("kwriteconfig6","--file","kwinrc","--group","Plugins","--key",PLUGIN+"Enabled",str(enabled).lower())

def loaded(name=PLUGIN):
    return dbus("/Scripting","org.kde.kwin.Scripting.isScriptLoaded",name)=="true"

def status():
    r=run("qdbus6","org.kde.KWin","/org/kde/FluidTiles","org.kde.FluidTiles.Status",check=False)
    return json.loads(r.stdout) if r.returncode==0 else None

def stop():
    set_autoload(False)
    for name in [PLUGIN,*LEGACY_PLUGINS]:
        if name!=PLUGIN:
            run("kwriteconfig6","--file","kwinrc","--group","Plugins","--key",name+"Enabled","false")
        if loaded(name):dbus("/Scripting","org.kde.kwin.Scripting.unloadScript",name)
    # Clean up an older trial or an independently loaded helper effect too.
    if status() is not None:
        dbus("/Effects","org.kde.kwin.Effects.unloadEffect","fluidtiles")
    deadline=time.monotonic()+5
    while status() is not None and time.monotonic()<deadline:time.sleep(.05)
    if status() is not None:raise RuntimeError("Native effect did not unload; package was not modified")

def start(*,enable_tiling=True):
    if not installed_path().exists():raise RuntimeError("Install the package first")
    if any(loaded(name) for name in LEGACY_PLUGINS):stop()
    try:
        return start_installed(enable_tiling=enable_tiling)
    except (RuntimeError,subprocess.CalledProcessError):
        # A failed activation must not leave a checked setting or a partially
        # running loader that can unexpectedly activate on the next reconfigure.
        stop()
        raise

def start_installed(*,enable_tiling=True):
    set_autoload(True)
    if not loaded(PLUGIN):
        script_id=dbus("/Scripting","org.kde.kwin.Scripting.loadDeclarativeScript",payload_path()/"contents/ui/main.qml",PLUGIN)
        if not script_id.isdigit():raise RuntimeError("KWin did not allocate a script instance")
        # KWin allocates script IDs from list size, so gaps left by unrelated
        # scripts can make /Scripting/ScriptN refer to a different instance.
        # Its manager starts the actual registered objects directly. Use the
        # installed package ID and enabled setting so Settings can stop it.
        dbus("/Scripting","org.kde.kwin.Scripting.start")
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        s=status()
        if s is not None:
            expected=json.loads((installed_path()/"metadata.json").read_text())["KPlugin"]["Version"]
            if s["version"]!=expected:raise RuntimeError(f"Loaded version {s['version']} does not match installed {expected}")
            if enable_tiling and not s["enabled"]:
                dbus("/org/kde/FluidTiles","org.kde.FluidTiles.SetEnabled","true")
                s=status()
            return s
        time.sleep(.1)
    raise RuntimeError("Loader started but native effect did not appear; inspect journalctl --user for Fluid Tiles")

def migrate_appearance_settings():
    """Split the previous master control without changing effective choices."""
    missing="__fluidtiles_missing__"
    args=("--file","kwinrc","--group","Script-"+PLUGIN)
    def read(key,default=missing):
        return run("kreadconfig6",*args,"--key",key,"--default",default).stdout.strip()
    if read("AppearanceSettingsVersion","0")!="0":return
    enabled=read("AppearanceEnabled","true").lower() in ("true","1","yes","on")
    mode=read("TitlebarMode","1")
    values={"OutlinesEnabled":str(enabled).lower(),"RoundedCornersEnabled":str(enabled).lower(),
        "TitlebarControlEnabled":str(enabled and mode!="0").lower(),"TitlebarScope":"1" if mode=="2" else "0"}
    for key,value in values.items():
        if read(key)==missing:run("kwriteconfig6",*args,"--key",key,value)
    # Remove obsolete values so KDE's Defaults cannot later resurrect them.
    for key in ("AppearanceEnabled","TitlebarMode"):run("kwriteconfig6",*args,"--key",key,"--delete")
    run("kwriteconfig6",*args,"--key","AppearanceSettingsVersion","1")

def migrate_animation_settings():
    """Adopt Fluid once, retaining the previous spring as a recoverable preset."""
    args=("--file","kwinrc","--group","Script-"+PLUGIN)
    if run("kreadconfig6",*args,"--key","AnimationSettingsVersion","--default","0").stdout.strip()!="0":return
    saved=run("kreadconfig6",*args,"--key","AnimationSettings").stdout.strip()
    if not saved:
        try:omega=float(run("kreadconfig6",*args,"--key","SpringFrequency","--default","24").stdout.strip())
        except ValueError:omega=24
        omega=max(8,min(60,omega))
        catalog=json.loads((ROOT/"build/package/contents/data/animations/catalog.json").read_text())
        profiles=next(p["profiles"] for p in catalog["presets"] if p["id"]=="classic")
        for event,profile in profiles.items():
            if event!="close":profile["curve"].update(mass=1,stiffness=omega*omega,damping=2*omega)
        document={"version":1,"preset":"fluid","enabled":True,"speed":1,"overrides":{},"customPresets":[
            {"id":"classic-previous","name":"Classic — previous settings","profiles":profiles}]}
        run("kwriteconfig6",*args,"--key","AnimationSettings",json.dumps(document,ensure_ascii=False,separators=(",",":")))
    run("kwriteconfig6",*args,"--key","AnimationSettingsVersion","1")

def install():
    source=ROOT/"build/package"
    if not (source/"contents/native/kwin/effects/plugins/fluidtiles.so").is_file():
        raise RuntimeError("Run cmake --build build first")
    dest=installed_path()
    if dest.exists():
        metadata=json.loads((dest/"metadata.json").read_text())
        if metadata.get("KPlugin",{}).get("Id")!=PLUGIN:raise RuntimeError("Refusing to replace an unrelated package")
    launcher=settings_launcher()
    if launcher.exists() and "X-FluidTiles-Managed=true" not in launcher.read_text():
        raise RuntimeError("Refusing to replace an unrelated application launcher")
    updating=dest.exists()
    previous=status() if updating else None
    was_loaded=loaded() if updating else False
    autoload=run("kreadconfig6","--file","kwinrc","--group","Plugins","--key",PLUGIN+"Enabled","--default","false").stdout.strip().lower()=="true" if updating else False
    stop()
    # Preserve the early prototype's manual preferences when moving to the
    # standard KWin Script settings group. Existing GUI preferences win.
    missing="__fluidtiles_missing__"
    migrated=run("kreadconfig6","--file","kwinrc","--group","Script-"+PLUGIN,"--key","SettingsVersion","--default","0").stdout.strip()
    if migrated=="0":
        for key in ["InnerGap","OuterGap","SpringFrequency"]:
            new=run("kreadconfig6","--file","kwinrc","--group","Script-"+PLUGIN,"--key",key,"--default",missing).stdout.strip()
            if new==missing:
                old=run("kreadconfig6","--file","kwinrc","--group","Effect-fluidtiles","--key",key,"--default",missing).stdout.strip()
                if old!=missing:run("kwriteconfig6","--file","kwinrc","--group","Script-"+PLUGIN,"--key",key,old)
    run("kwriteconfig6","--file","kwinrc","--group","Script-"+PLUGIN,"--key","SettingsVersion","1")
    migrate_appearance_settings()
    migrate_animation_settings()
    dest.parent.mkdir(parents=True,exist_ok=True)
    # Stage before replacing only this project's package. Qt may retain the
    # QML module in memory; rebuilds of its bootstrap can require a new session.
    with tempfile.TemporaryDirectory(prefix="fluidtiles-stage-",dir=dest.parent) as tmp:
        stage=Path(tmp)/PLUGIN
        shutil.copytree(source,stage)
        # Qt can retain plugin libraries and QML components after an unload.
        # Immutable payload URLs ensure a code update loads the new binary.
        digest=hashlib.sha256()
        for p in sorted(source.rglob("*")):
            if p.is_file():
                digest.update(str(p.relative_to(source)).encode());digest.update(p.read_bytes())
        revision=digest.hexdigest()
        shutil.copytree(source,stage/"versions"/revision)
        metadata=json.loads((stage/"metadata.json").read_text())
        metadata["X-FluidTiles-Revision"]=revision
        metadata["X-KDE-ConfigModule"]=str(dest/"versions"/revision/"contents/native/kcm_fluidtiles.so")
        (stage/"metadata.json").write_text(json.dumps(metadata,indent=2)+"\n")
        if dest.exists():shutil.rmtree(dest)
        stage.rename(dest)
    # One-time migration gives the corrected loader a fresh Qt component URL.
    # Only remove our recognized previous package, never a similarly named one.
    legacy=dest.parent/LEGACY_PLUGINS[0]
    if legacy.exists():
        meta=json.loads((legacy/"metadata.json").read_text())
        if meta.get("KPlugin",{}).get("Id")==LEGACY_PLUGINS[0]:shutil.rmtree(legacy)
    install_settings_launcher()
    # An update keeps both the Scripts checkbox and the separate emergency
    # tiling state. First installation continues to leave the plugin disabled.
    if was_loaded or previous is not None:
        start(enable_tiling=previous is None or previous["enabled"])
    set_autoload(autoload)
    print(f"Installed {dest}")

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("command",choices=["install","start","try","stop","status","configure","remove"])
    args=parser.parse_args()
    try:
        if args.command=="install":install()
        elif args.command=="start":print(json.dumps(start(),indent=2))
        elif args.command=="try":
            raise RuntimeError("Session-only trials were removed because they bypassed the Settings checkbox. Use 'start' to enable the installed script (including at login), or 'stop' to disable it.")
        elif args.command=="stop":stop();print("Disabled; original window geometry restored")
        elif args.command=="status":print(json.dumps(status(),indent=2))
        elif args.command=="configure":
            executable=installed_path()/"contents/native/fluidtiles-settings"
            if not executable.is_file():raise RuntimeError("Install the package first")
            subprocess.Popen([str(executable)],start_new_session=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            print("Opened Fluid Tiles Settings")
        elif args.command=="remove":
            stop()
            dest=installed_path()
            if dest.exists():
                meta=json.loads((dest/"metadata.json").read_text())
                if meta.get("KPlugin",{}).get("Id")!=PLUGIN:raise RuntimeError("Refusing to remove an unrelated package")
                shutil.rmtree(dest)
            launcher=settings_launcher()
            if launcher.exists() and "X-FluidTiles-Managed=true" in launcher.read_text():launcher.unlink()
            print("Removed Fluid Tiles user-local package")
    except (RuntimeError,subprocess.CalledProcessError) as e:
        parser.exit(1,str(e)+"\n"+(getattr(e,"stderr","") or ""))

if __name__=="__main__":main()
