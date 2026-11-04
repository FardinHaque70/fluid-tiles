"""Run the real animation migration against private KConfig files."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('control',root/'tools/control.py')
control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)
for frequency,expected in [('8',8),('30',30),('60',60),('invalid',24)]:
    with tempfile.TemporaryDirectory(prefix='fluidtiles-animation-config-') as config:
        os.environ['XDG_CONFIG_HOME']=config
        group='Script-'+control.PLUGIN
        def write(key,value):control.run('kwriteconfig6','--file','kwinrc','--group',group,'--key',key,value)
        def read(key):return control.run('kreadconfig6','--file','kwinrc','--group',group,'--key',key).stdout.strip()
        write('SpringFrequency',frequency);write('InnerGap','17');write('OutlinesEnabled','false')
        control.run('kwriteconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled','false')
        control.migrate_animation_settings()
        settings=json.loads(read('AnimationSettings'))
        assert settings['preset']=='fluid' and settings['speed']==1
        legacy=settings['customPresets'][0]
        assert legacy['id']=='classic-previous'
        assert legacy['profiles']['open']['curve']['stiffness']==expected**2
        assert legacy['profiles']['float']['curve']['damping']==expected*2
        assert read('InnerGap')=='17' and read('OutlinesEnabled')=='false' and read('SpringFrequency')==frequency
        assert control.run('kreadconfig6','--file','kwinrc','--group','Plugins','--key','fluidtiles-controllerEnabled').stdout.strip()=='false'
        # Explicit Defaults (absent settings) and later user choices survive.
        control.run('kwriteconfig6','--file','kwinrc','--group',group,'--key','AnimationSettings','--delete')
        control.migrate_animation_settings();assert read('AnimationSettings')==''
        write('AnimationSettings','my later preference');control.migrate_animation_settings()
        assert read('AnimationSettings')=='my later preference'
with tempfile.TemporaryDirectory(prefix='fluidtiles-animation-existing-') as config:
    os.environ['XDG_CONFIG_HOME']=config
    control.run('kwriteconfig6','--file','kwinrc','--group',group,'--key','AnimationSettings','existing document')
    control.migrate_animation_settings()
    assert control.run('kreadconfig6','--file','kwinrc','--group',group,'--key','AnimationSettings').stdout.strip()=='existing document'
print('Animation migration: legacy, invalid, existing, Defaults and reinstall cases passed')
