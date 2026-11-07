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
# Factory exclusions migrate once; explicit custom lists and tiling rules survive.
import xml.etree.ElementTree as ET
ns={'k':'http://www.kde.org/standards/kcfg/1.0'}
schema=ET.parse(root/'package/contents/config/main.xml')
old=schema.find(".//k:entry[@name='IgnoreApplications']/k:default",ns).text
new=schema.find(".//k:entry[@name='AppearanceIgnoredApplications']/k:default",ns).text
for appearance,expected in [(old,new), ('  '+old.replace('\n',' \n  ').upper()+' \n',None),
                            (old+'\ncustom.app',old+'\ncustom.app'),('plasmashell\ncustom.app','plasmashell\ncustom.app'),('', ''),(None,None)]:
    with tempfile.TemporaryDirectory(prefix='fluidtiles-config-') as config:
        os.environ['XDG_CONFIG_HOME']=config
        args=('kwriteconfig6','--file','kwinrc','--group','Script-'+control.PLUGIN)
        def write(key,value):control.run(*args,'--key',key,value)
        def read(key):return control.run('kreadconfig6','--file','kwinrc','--group','Script-'+control.PLUGIN,'--key',key,'--default','MISSING').stdout.rstrip('\n')
        write('AppearanceSettingsVersion','1');write('IgnoreApplications','custom.tiling\nplasmashell')
        write('RoundedCornersEnabled','false');write('CornerRadius','13');write('OutlinesEnabled','false')
        if appearance is not None:write('AppearanceIgnoredApplications',appearance)
        control.migrate_appearance_settings()
        actual=read('AppearanceIgnoredApplications')
        if appearance is None:assert actual=='MISSING'
        elif expected is None:
            assert {line.strip().casefold() for line in actual.splitlines() if line.strip()}==set(new.splitlines())
        else:assert actual==expected,(actual,expected)
        assert read('AppearanceSettingsVersion')=='2'
        assert read('IgnoreApplications')=='custom.tiling\nplasmashell'
        assert read('RoundedCornersEnabled')=='false' and read('CornerRadius')=='13' and read('OutlinesEnabled')=='false'
        write('AppearanceIgnoredApplications','plasmashell\nintentional.exclusion')
        control.migrate_appearance_settings()
        assert read('AppearanceIgnoredApplications')=='plasmashell\nintentional.exclusion','Reinstall overwrote a new explicit preference'
print('Appearance migration: four legacy and six default/custom configurations passed')
