"""Exercise the real one-shot KConfig migration in disposable config roots."""
import importlib.util
import os
from pathlib import Path
import tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('control',root/'tools/control.py')
control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)
for legacy,mode,preset in [('false','2',{}),('true','0',{}),('true','2',{'RoundedCornersEnabled':'false'}),('true','1',{})]:
    with tempfile.TemporaryDirectory(prefix='fluidtiles-config-') as config:
        os.environ['XDG_CONFIG_HOME']=config
        group='Script-'+control.PLUGIN
        def write(key,value):control.run('kwriteconfig6','--file','kwinrc','--group',group,'--key',key,value)
        def read(key):return control.run('kreadconfig6','--file','kwinrc','--group',group,'--key',key,'--default','MISSING').stdout.strip()
        write('AppearanceEnabled',legacy);write('TitlebarMode',mode)
        for k,v in preset.items():write(k,v)
        control.migrate_appearance_settings()
        expected={'OutlinesEnabled':legacy,'RoundedCornersEnabled':legacy,'TitlebarControlEnabled':'true' if legacy=='true' and mode!='0' else 'false','TitlebarScope':'1' if mode=='2' else '0',**preset}
        assert {k:read(k) for k in expected}==expected
        assert read('AppearanceEnabled')==read('TitlebarMode')=='MISSING'
        # Defaults/delete or later choices must not be overwritten on reinstall.
        write('OutlinesEnabled','true');write('TitlebarScope','0')
        control.migrate_appearance_settings()
        assert read('OutlinesEnabled')=='true' and read('TitlebarScope')=='0'
print('Appearance migration: four legacy/preexisting configurations passed')
