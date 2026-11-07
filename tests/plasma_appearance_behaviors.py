"""Plasma dialogs receive appearance while real shell surfaces stay untouched."""
import hashlib
import json
import subprocess


def run_plasma_appearance_audit(api, env, children, log, results):
    call, status, wait_for, tiled, settled, root = api
    assert env['WAYLAND_DISPLAY'] == 'fluidtiles-test'
    report = {}

    def get(ident):
        return next(w for w in status()['windows'] if w['id'] == ident)

    def apply(**values):
        subprocess.run([str(root/'build/fluidtiles-settings-tests'), 'apply', json.dumps(values)],
                       env={**env, 'QT_QPA_PLATFORM': 'offscreen', 'FLUIDTILES_SETTINGS_TEST': '1'},
                       check=True, stdout=log, stderr=log, timeout=12)
        for key, effective in [('AppearanceEnabled','enabled'),('OutlinesEnabled','outlinesEnabled'),
                               ('BorderWidth','borderWidth'),('CornerRadius','cornerRadius'),('TitlebarScope','titlebarScope')]:
            if key in values:
                wait_for(lambda: status()['appearance'][effective] == values[key])
        if 'AppearanceIgnoredApplications' in values:
            wait_for(lambda: status()['appearance']['ignoredApplications'] == values['AppearanceIgnoredApplications'])
        wait_for(settled)

    def spawn(label, kind=''):
        proc = subprocess.Popen([str(root/'build/fluidtiles-demo'), label, '#21618c', '0', kind],
                                env=env, stdout=log, stderr=log)
        children.append(proc)
        ident = wait_for(lambda: next((w['id'] for w in status()['windows'] if w['title'].endswith(label)), None))
        wait_for(settled)
        return proc, ident

    def capture(name):
        image = results/name
        subprocess.run([str(root/'build/fluidtiles-capture'), str(image)],
                       env={**env, 'QT_QPA_PLATFORM': 'offscreen', 'FLUIDTILES_CAPTURE_NATIVE': '1'},
                       check=True, stdout=log, stderr=log, timeout=12)
        return image

    apply(AppearanceEnabled=True, BorderWidth=3, CornerRadius=12, TitlebarScope=0,
          ExpandSingleWindow=False)
    defaults = status()['appearance']['ignoredApplications']
    assert 'plasmashell' not in defaults.splitlines() and 'org.kde.plasmashell' not in defaults.splitlines()
    # A regular tile also keeps the effect active during empty-dialog transitions.
    _, anchor = spawn('Plasma appearance anchor')
    wait_for(lambda: get(anchor)['tiled'])
    legacy = defaults + '\nplasmashell\norg.kde.plasmashell'
    for kind in ['plasma-settings', 'plasma-dialog', 'plasma-legacy']:
        apply(AppearanceIgnoredApplications=legacy)
        proc, ident = spawn('Plasma appearance ' + kind, kind)
        ids = [ident]
        if kind == 'plasma-dialog':
            ids.append(wait_for(lambda: next((w['id'] for w in status()['windows'] if w['title'].endswith('Plasma appearance '+kind+' parent')), None)))
            assert get(ident)['modal'] and get(ident)['transient']
        assert all(get(i)['ignored'] and not get(i)['tiled'] and not get(i)['appearance']['applied'] for i in ids)
        before = {i: get(i) for i in ids}
        capture(kind+'-before.png')
        apply(AppearanceIgnoredApplications=defaults)
        wait_for(lambda: all(get(i)['appearance']['applied'] and get(i)['appearance']['radius']==[12,12,12,12] for i in ids))
        for i in ids:
            w = get(i)
            assert w['ignored'] and not w['tiled'], 'Appearance changed Plasma tiling admission'
            assert max(abs(a-b) for a,b in zip(w['actual'],before[i]['actual'])) < 1
            assert not w['appearance']['titlebarOwned'] and w['appearance']['policy'] == before[i]['appearance']['policy']
            assert w['appearance']['border'] > 0
        capture(kind+'-rounded.png')
        # Rounding must remain independent of outlines and preserve native controls.
        apply(OutlinesEnabled=False)
        assert all(get(i)['appearance']['radius']==[12,12,12,12] and get(i)['appearance']['border']==0 for i in ids)
        capture(kind+'-rounding-only.png')
        apply(OutlinesEnabled=True)
        apply(AppearanceIgnoredApplications='plasmashell\norg.kde.plasmashell\ncustom.app')
        wait_for(lambda: all(not get(i)['appearance']['owned'] for i in ids))
        assert all(get(i)['appearance']['radius']==before[i]['appearance']['radius'] for i in ids)
        apply(AppearanceIgnoredApplications=defaults)
        wait_for(lambda: all(get(i)['appearance']['radius']==[12,12,12,12] for i in ids))
        call('SetEnabled',False)
        wait_for(lambda: all(not get(i)['appearance']['owned'] for i in ids))
        assert all(get(i)['appearance']['radius']==before[i]['appearance']['radius'] for i in ids)
        call('SetEnabled',True)
        wait_for(lambda: all(get(i)['appearance']['radius']==[12,12,12,12] for i in ids))
        proc.terminate(); proc.wait(timeout=5)
        wait_for(lambda: all(w['id'] not in ids for w in status()['windows']))
        wait_for(settled)
        report[kind+'_rounding_float_geometry_titlebars_exclusion_restore'] = 'passed'

    # No application exclusion may be responsible for protecting these surfaces.
    apply(AppearanceIgnoredApplications='')
    for appid, kind in [('org.kde.plasmashell','panel'), ('plasmashell','desktop')]:
        previous = {w['id'] for w in status()['windows']}
        proc = subprocess.Popen([str(root/'build/fluidtiles-panel'), appid, kind],
                                env=env, stdout=log, stderr=log)
        children.append(proc)
        ident = wait_for(lambda: next((w['id'] for w in status()['windows'] if w['id'] not in previous), None))
        wait_for(settled)
        w = get(ident)
        assert not w['tiled'] and not w['appearance']['applied'] and not w['appearance']['owned']
        assert w['appearance']['border']==0 and w['appearance']['radius']==[0,0,0,0]
        capture('plasma-'+kind+'-protected.png')
        proc.terminate(); proc.wait(timeout=5)
        wait_for(lambda: all(w['id']!=ident for w in status()['windows']))
        wait_for(settled)
        report[kind+'_structurally_excluded_without_app_rule'] = 'passed'
    apply(AppearanceIgnoredApplications=defaults)
    result = {'pass': True, 'cases': report, 'status': status(),
              'native_binary_sha256': hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
    (results/'plasma-appearance.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
