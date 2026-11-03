"""Integrated decoration rendering and native-state restoration in real KWin."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def run_appearance_audit(api,env,children,log,results):
    call,status,wait_for,tiled,settled,root=api
    report={}
    (results/"appearance.json").unlink(missing_ok=True)
    def get(ident):return next(w for w in status()['windows'] if w['id']==ident)
    def apply(**values):
        subprocess.run([str(root/'build/fluidtiles-settings-tests'),'apply',json.dumps(values)],
            env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
        for key,effective in [('AppearanceEnabled','enabled'),('OutlinesEnabled','outlinesEnabled'),('RoundedCornersEnabled','roundedCornersEnabled'),('TitlebarControlEnabled','titlebarControlEnabled'),('TitlebarScope','titlebarScope'),('BorderWidth','borderWidth'),('CornerRadius','cornerRadius'),('TitlebarMode','titlebarMode')]:
            if key in values:wait_for(lambda:status()['appearance'][effective]==values[key])
    def capture(name):
        file=results/name
        subprocess.run([str(root/'build/fluidtiles-capture'),str(file)],env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_CAPTURE_NATIVE':'1'},
            check=True,stdout=log,stderr=log,timeout=12)
        return file
    def spawn(label,kind='',delay=0):
        p=subprocess.Popen([str(root/'build/fluidtiles-demo'),label,'#21618c',str(delay),kind],env=env,stdout=log,stderr=log)
        children.append(p)
        ident=wait_for(lambda:next((w['id'] for w in status()['windows'] if w['title'].endswith(label)),None))
        return p,ident
    def check_bounds():
        s=status();windows=[w for w in s['windows'] if w['tiled']]
        scale=s['outputs'][0]['scale'];gap=int(s['innerGap']*scale+.5)/scale
        for w in windows:
            a=w['appearance'];b=a['border'];f=w['target'];outer=a['outer']
            assert a['applied'] and a['noBorder'] and b>0,w
            assert abs(outer[0]-(a['frame'][0]-b))<.01
            assert f[2]+1>=w['minimum'][0] and f[3]+1>=w['minimum'][1]
            assert not w['redirected'],'Settled appearance must not retain offscreen texture'
        for a in windows:
            for b in windows:
                if a['id']>=b['id']:continue
                x,y,w,h=a['appearance']['outer'];ox,oy,ow,oh=b['appearance']['outer']
                assert min(x+w,ox+ow)<=max(x,ox)+.01 or min(y+h,oy+oh)<=max(y,oy)+.01,'Outlines overlap'
                # Any shared horizontal/vertical neighbor keeps the configured
                # empty gap between the outer edges of the outlines.
                if min(y+h,oy+oh)>max(y,oy)+1:
                    distance=max(x,ox)-min(x+w,ox+ow)
                    assert distance>=gap-1.1,(distance,gap)
    def color_at_border(image,window,expected):
        output=status()['outputs'][0]['geometry']
        x,y,w,h=window['appearance']['frame'];b=window['appearance']['border']
        # Interior of the straight top stroke, away from AA and corners.
        spec={'logicalSize':output[2:],'points':[[x+w*.5,y-b*.5]]}
        pixel=json.loads(subprocess.check_output([str(root/'build/fluidtiles-imagecheck'),str(image),json.dumps(spec)],text=True))['pixels'][0][:3]
        rgb=tuple(int(expected[i:i+2],16) for i in (3,5,7))
        assert max(abs(a-b) for a,b in zip(pixel,rgb))<12,(pixel,rgb,window)

    # Exercise the actual KDE form, all default values, native color controls
    # and the Appearance page before taking the native baseline.
    subprocess.run([str(root/'build/fluidtiles-settings-tests'),str(results)],
        env={**env,'QT_QPA_PLATFORM':'offscreen','FLUIDTILES_SETTINGS_TEST':'1'},check=True,stdout=log,stderr=log,timeout=12)
    apply(AppearanceEnabled=False)
    for label,delay in [('Appearance A',0),('Appearance B',0),('Appearance Slow',80)]:
        _,ident=spawn(label,delay=delay);wait_for(lambda:get(ident)['tiled']);wait_for(settled)
    original={w['id']:{'restore':w['restore'],'policy':w['appearance']['policy'],'radius':w['appearance']['radius']} for w in tiled()}
    assert not status()['appearance']['enabled']
    native_radii={}
    native_source=next(w['id'] for w in tiled() if w['title'].endswith('Appearance A'))
    for operation,reverse in [('fullscreen','unfullscreen'),('maximize','unmaximize')]:
        call('TestWindow',native_source,operation);wait_for(lambda:not get(native_source)['tiled'])
        native_radii[operation]=get(native_source)['appearance']['radius']
        call('TestWindow',native_source,reverse);wait_for(lambda:get(native_source)['tiled']);wait_for(settled)
    capture('appearance-00-baseline.png')
    apply(AppearanceEnabled=True,BorderWidth=2,CornerRadius=8,TitlebarMode=1,
          UseSystemBorderColors=False,ActiveBorderColor='#ff4444',InactiveBorderColor='#44cc66')
    if os.environ.get('FLUIDTILES_TEST_BAD_APPEARANCE_SHADER')=='1':
        wait_for(lambda:not status()['appearance']['available']);wait_for(settled)
        assert all(not w['appearance']['owned'] and not w['appearance']['applied'] for w in tiled())
        assert all(w['appearance']['policy']==original[w['id']]['policy'] for w in tiled())
        capture('appearance-shader-fallback.png')
        result={'pass':True,'cases':{'shader_failure_restores_native_appearance':'passed'},'final':status(),
            'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest()}
        (results/'appearance.json').write_text(json.dumps(result,indent=2)+'\n');return result['cases']
    wait_for(lambda:all(w['appearance']['noBorder'] for w in tiled()));wait_for(settled)
    check_bounds()
    image=capture('appearance-01-enabled.png')
    s=status()
    assert s['appearance']['available'] and s['appearance']['shaderReady'],s['appearance']
    for w in tiled():color_at_border(image,w,s['appearance']['activeColor'] if w['id']==s['activeWindow'] else s['appearance']['inactiveColor'])
    report['initial_native_outline_corners_and_reserved_gaps']='passed'

    # The user's authentication-dialog preference changes appearance only.
    # Exercise the real Polkit app ID and a native modal dialog without an
    # actual authentication request or password entry.
    auth_app='org.kde.polkit-kde-authentication-agent-1'
    exclusions=status()['appearance']['ignoredApplications']
    assert auth_app in exclusions.splitlines()
    auth,auth_id=spawn('Authentication appearance preference','authentication-prompt')
    wait_for(lambda:get(auth_id)['ignored'] and not get(auth_id)['tiled'])
    wait_for(lambda:not get(auth_id)['appearance']['applied']);wait_for(settled)
    before=get(auth_id)
    auth_frame=before['actual'];auth_policy=before['appearance']['policy']
    apply(AppearanceIgnoredApplications='\n'.join(line for line in exclusions.splitlines() if line.strip()!=auth_app))
    wait_for(lambda:get(auth_id)['appearance']['applied'] and get(auth_id)['appearance']['border']>0)
    wait_for(settled)
    after=get(auth_id)
    assert after['ignored'] and not after['tiled'],'Appearance preference admitted authentication dialog to tiling'
    assert max(abs(a-b) for a,b in zip(auth_frame,after['actual']))<1,'Authentication dialog moved or resized'
    assert after['appearance']['policy']==auth_policy and not after['appearance']['titlebarOwned']
    image=capture('appearance-authentication-floating-outline.png')
    s=status();color_at_border(image,after,s['appearance']['activeColor'] if s['activeWindow']==auth_id else s['appearance']['inactiveColor'])
    apply(AppearanceIgnoredApplications=exclusions)
    wait_for(lambda:not get(auth_id)['appearance']['applied'] and not get(auth_id)['appearance']['owned'])
    assert get(auth_id)['ignored'] and not get(auth_id)['tiled']
    auth.terminate();auth.wait(timeout=5)
    wait_for(lambda:all(w['id']!=auth_id for w in status()['windows']));wait_for(settled)
    report['authentication_prompt_outline_without_tiling']='passed'

    # Every section owns only its feature and retains the other two choices.
    apply(OutlinesEnabled=False);wait_for(settled)
    assert all(w['appearance']['border']==0 and w['appearance']['noBorder'] and w['appearance']['radius']==[8,8,8,8] for w in tiled())
    apply(OutlinesEnabled=True,RoundedCornersEnabled=False);wait_for(settled)
    assert all(w['appearance']['border']>0 and w['appearance']['noBorder'] and w['appearance']['radius']==[0,0,0,0] for w in tiled())
    apply(RoundedCornersEnabled=True,TitlebarControlEnabled=False);wait_for(settled)
    assert all(w['appearance']['border']>0 and not w['appearance']['noBorder'] and w['appearance']['radius']==[8,8,8,8] for w in tiled())
    apply(TitlebarControlEnabled=True);wait_for(settled);check_bounds()
    report['independent_outline_rounding_and_titlebar_controls']='passed'

    (results/'appearance-first-status.json').write_text(json.dumps(status(),indent=2))
    if os.environ.get('FLUIDTILES_TEST_APPEARANCE_SMOKE')=='1':
        (results/'appearance.json').write_text(json.dumps({'pass':True,'cases':report,'status':status()},indent=2));return report
    source=next(w['id'] for w in tiled() if w['title'].endswith('Appearance A'))
    def pixels(image,points):
        spec={'logicalSize':status()['outputs'][0]['geometry'][2:],'points':points}
        return json.loads(subprocess.check_output([str(root/'build/fluidtiles-imagecheck'),str(image),json.dumps(spec)],text=True))['pixels']
    def native_info(ident):
        text=subprocess.check_output(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.getWindowInfo','{'+ident+'}'],text=True)
        return dict(line.split(': ',1) for line in text.splitlines() if ': ' in line)
    def restored(ident):return max(abs(a-b) for a,b in zip(get(ident)['actual'],original[ident]['restore']))<2
    def close(proc,ident):
        proc.terminate();proc.wait(timeout=5)
        wait_for(lambda:all(w['id']!=ident for w in status()['windows']));wait_for(settled)
    def run_input(ident,mode,destination):
        x,y,w,h=get(ident)['actual'];origin=(x+w*.55,y+h*.5)
        p=subprocess.Popen([str(root/'build/fluidtiles-input'),str(origin[0]),str(origin[1]),mode,str(destination[0]),str(destination[1])],env=env,stdout=log,stderr=log)
        try:
            wait_for(lambda:status()['dragging'],5)
            assert p.wait(timeout=6)==0
            wait_for(lambda:not status()['dragging']);wait_for(settled)
        finally:
            if p.poll() is None:p.terminate();p.wait(timeout=3)

    call('TestWindow',source,'activate');wait_for(lambda:status()['activeWindow']==source)
    image=capture('appearance-02-focus.png');s=status()
    for w in tiled():color_at_border(image,w,s['appearance']['activeColor'] if w['id']==source else s['appearance']['inactiveColor'])
    x,y,w,h=get(source)['appearance']['frame'];b=get(source)['appearance']['border']
    point=[x+w*.5,y-b*.5];full=pixels(image,[point])[0]
    call('TestWindow',source,'half-opacity');wait_for(lambda:get(source)['opacity']==.5)
    faded=capture('appearance-02-opacity.png');dim=pixels(faded,[point])[0]
    assert 20<dim[0]<full[0]-20,('outline ignored native window opacity',full,dim)
    call('TestWindow',source,'full-opacity');wait_for(lambda:get(source)['opacity']==1)
    report['native_window_opacity_modulates_outline']='passed'
    # The outline itself legitimately occupies part of the rounded corner's
    # frame bounding box. Test the surface mask separately with zero stroke.
    apply(BorderWidth=0);wait_for(settled)
    image=capture('appearance-02-corner-mask.png')
    x,y,w,h=get(source)['appearance']['frame']
    sample=pixels(image,[[x+.1,y+.1],[x+12,y+2]])
    assert max(sample[0][:3])<25,('corner not clipped',sample)
    assert max(sample[1][:3])>50,('window edge incorrectly clipped',sample)
    # Keep the real client/buffer unchanged and deliberately stretch only its
    # visual presentation. This deterministically reproduces a lagging client
    # without relying on screenshot-versus-buffer timing.
    scale=status()['outputs'][0]['scale']
    x,y,w,h=get(source)['appearance']['frame']
    radius=status()['appearance']['cornerRadius']
    steps=int(radius*scale+.999)
    points=[[x+(ix+.1)/scale,y+(iy+.1)/scale] for iy in range(steps) for ix in range(steps)]
    original_mask=pixels(image,points)
    original_count=sum(max(p[:3])>30 for p in original_mask)
    captures_before=status()['appearance']['captures']
    for operation in ['preview-wide','preview-tall']:
        native=get(source)['actual']
        assert call('TestPointer','resize',x+w*.75,y+h*.5)=='true'
        assert call('TestWindow',source,operation)=='true'
        wait_for(lambda:get(source)['redirected'])
        stretched=capture('appearance-02-'+operation+'.png')
        assert max(abs(a-b) for a,b in zip(get(source)['actual'],native))<1,'Fixture must keep native client geometry unchanged'
        count=sum(max(p[:3])>30 for p in pixels(stretched,points))
        assert count>=original_count-5,('corner radius distorted by source snapshot',operation,original_count,count)
        assert call('TestPointer','cancel',0,0)=='true';wait_for(settled)
    assert status()['appearance']['captures']>captures_before,'The source-clipping bypass was never entered'
    apply(BorderWidth=2);wait_for(settled)
    report['presentation_space_clipping_of_stretched_snapshot']='passed'
    report['focus_colors_and_corner_pixels']='passed'
    from stacking_behaviors import run_stacking_audit
    for key,value in run_stacking_audit(api,env,children,log,results).items():report['appearance_'+key]=value
    wait_for(settled);check_bounds()
    for w in tiled():original[w['id']]['restore']=w['restore']

    # Reversible server decorations; appearance stays active on the float.
    previous=get(source)['appearance']['outer']
    call('TestWindow',source,'activate');wait_for(lambda:status()['activeWindow']==source)
    call('Action','float');wait_for(lambda:not get(source)['tiled'] and not get(source)['appearance']['noBorder'])
    original[source]['restore']=get(source)['restore'] # centering is now an explicit floating choice
    wait_for(lambda:restored(source));wait_for(settled)
    assert get(source)['appearance']['applied']
    assert all(get(source)['stackingOrder']>w['stackingOrder'] for w in tiled())
    capture('appearance-03-floating.png')
    # The old long vertical outline now lies inside a different tile. It must
    # not survive the move as an overlay/ghost.
    x,y,w,h=previous
    ghost=pixels(results/'appearance-03-floating.png',[[x+w-1,y+h*.22]])[0][:3]
    assert max(abs(a-b) for a,b in zip(ghost,(255,68,68)))>30,ghost
    call('TestWindow',source,'activate');call('Action','float')
    wait_for(lambda:get(source)['tiled'] and get(source)['appearance']['noBorder']);wait_for(settled)
    check_bounds();report['manual_float_restores_titlebar_and_removes_old_outline']='passed'

    # The real Close tooltip lives on a floating server-decorated window now.
    fixed,floating=spawn('Appearance fixed','fixed')
    wait_for(lambda:get(floating)['appearance']['applied']);wait_for(settled)
    call('TestWindow',floating,'cover-divider');wait_for(settled)
    x,y,w,h=get(floating)['actual']
    pointer=subprocess.Popen([str(root/'build/fluidtiles-input'),str(x+w-14),str(y+14),'hover-close'],env=env,stdout=log,stderr=log)
    try:
        popup=wait_for(lambda:next((w for w in status()['windows'] if w['popup'] and w['internal']),None),4)
        assert not popup['tiled'] and not popup['appearance']['applied'] and not popup['appearance']['owned']
        assert pointer.wait(timeout=6)==0
    finally:
        if pointer.poll() is None:pointer.terminate();pointer.wait(timeout=3)
    wait_for(lambda:not any(w['popup'] for w in status()['windows']))
    report['floating_titlebar_tooltip_excluded']='passed'
    # Native occlusion: a tiled outline behind the fixed float must not paint
    # across its opaque interior. Sample at an overlapping tiled boundary.
    fx,fy,fw,fh=get(floating)['actual']
    capture('appearance-04-floating-occlusion.png')
    covered=[]
    for tile in tiled():
        ox,oy,ow,oh=tile['appearance']['outer']
        lo=max(fy+40,oy+12);hi=min(fy+fh-12,oy+oh-12)
        if hi>lo:
            for edge in [ox+1,ox+ow-1]:
                if fx+12<edge<fx+fw-12:covered.append([edge,(lo+hi)/2])
        lo=max(fx+12,ox+12);hi=min(fx+fw-12,ox+ow-12)
        if hi>lo:
            for edge in [oy+1,oy+oh-1]:
                if fy+40<edge<fy+fh-12:covered.append([(lo+hi)/2,edge])
    assert covered,'Occlusion fixture must actually cover a tiled outline'
    for p in pixels(results/'appearance-04-floating-occlusion.png',covered):
        assert max(abs(a-b) for a,b in zip(p[:3],(68,204,102)))>25,'Tiled outline painted above floating content'
    report['floating_occludes_tiled_outlines']='passed'
    close(fixed,floating)

    for state,reverse in [('fullscreen','unfullscreen'),('maximize','unmaximize'),('minimize','restore')]:
        call('TestWindow',source,state)
        wait_for(lambda:not get(source)['tiled'] and not get(source)['appearance']['owned'])
        assert not get(source)['appearance']['applied']
        assert get(source)['appearance']['policy']==original[source]['policy']
        if state in native_radii:assert get(source)['appearance']['radius']==native_radii[state],('native corners not restored',state,get(source)['appearance'],native_radii)
        call('TestWindow',source,reverse)
        wait_for(lambda:get(source)['tiled'] and get(source)['appearance']['titlebarOwned']);wait_for(settled)
    report['native_state_suppression_and_reentry']='passed'

    # Direct resizing and focus-color changes during the live grab. Capture
    # while a slow client can still be displaying its previous buffer size.
    call('TestWindow',source,'activate');x,y,w,h=get(source)['target'];point=(x+w*.75,y+h*.5)
    assert call('TestPointer','resize',*point)=='true'
    for offset in [-60,100,-80,60]:call('TestPointer','motion',point[0]+offset,point[1])
    apply(ActiveBorderColor='#3399ff')
    wait_for(lambda:status()['appearance']['activeColor']=='#ff3399ff')
    assert status()['dragging'],'A color-only setting cancelled resize'
    capture('appearance-05-resizing.png')
    assert call('TestPointer','cancel',0,0)=='true';wait_for(settled)
    # Changing an inset is a geometry change and must cancel safely.
    x,y,w,h=get(source)['target'];point=(x+w*.75,y+h*.5)
    assert call('TestPointer','resize',*point)=='true'
    call('TestPointer','motion',point[0]+50,point[1]);apply(BorderWidth=3)
    wait_for(lambda:not status()['dragging']);wait_for(settled);check_bounds()
    apply(BorderWidth=2);wait_for(settled)
    report['resize_capture_and_geometry_vs_color_settings']='passed'

    # Interrupt springs mid-flight. There is one shared visual rectangle for
    # the content and outline, including the custom offscreen corner shader.
    call('TestWindow',source,'activate')
    for _ in range(8):call('Action','split');time.sleep(.025)
    capture('appearance-06-interrupted.png');wait_for(settled);check_bounds()
    report['interrupted_animation_and_unredirect_at_rest']='passed'
    for edge in ['left','right','top','bottom']:
        src=get(source);dst=next(w for w in tiled() if w['id']!=source)
        x,y,w,h=src['target'];start=(x+w/2,y+h/2)
        x,y,w,h=dst['target'];nx,ny={'left':(.1,.5),'right':(.9,.5),'top':(.5,.1),'bottom':(.5,.9)}[edge]
        assert call('TestPointer','move',*start)=='true'
        call('TestPointer','motion',x+w*nx,y+h*ny);call('TestPointer','release',0,0)
        wait_for(settled);assert len(tiled())==3;check_bounds()
    report['all_edge_drops_with_reserved_outlines']='passed'


    csd,client=spawn('Appearance client header','csd-system-move')
    wait_for(lambda:get(client)['tiled']);wait_for(settled)
    assert not get(client)['appearance']['titlebarOwned'],'Client header was treated as a server decoration'
    assert get(client)['appearance']['applied']
    other=next(w for w in tiled() if w['id']!=client);x,y,w,h=other['target']
    run_input(client,'client-move',(x+w/2,y+h/2))
    assert get(client)['tiled'];wait_for(settled)
    capture('appearance-07-client-header.png')
    close(csd,client)
    report['client_header_and_real_wayland_system_move']='passed'

    # Separate appearance and tiling rules. Excluding an application restores
    # its native frame while all its normal windows remain tiled.
    apply(AppearanceIgnoredApplications='fluidtiles-demo')
    wait_for(lambda:all(not w['appearance']['applied'] and not w['appearance']['owned'] for w in tiled()));wait_for(settled)
    assert len(tiled())==3 and all(not w['ignored'] for w in tiled())
    assert all(w['appearance']['policy']==original[w['id']]['policy'] for w in tiled())
    apply(AppearanceIgnoredApplications='');wait_for(lambda:all(w['appearance']['applied'] for w in tiled()));wait_for(settled)
    apply(CornerRadius=0);wait_for(lambda:all(w['appearance']['radius']==[0,0,0,0] for w in tiled()))
    apply(CornerRadius=8);wait_for(settled)
    report['appearance_exclusions_and_native_corner_mode']='passed'

    # A real native Force rule must take precedence over titlebar hiding.
    rules=Path(env['XDG_CONFIG_HOME'])/'kwinrulesrc'
    assert str(rules.parent)==env['XDG_CONFIG_HOME']
    rules.write_text('[General]\nOrder=fluidtiles-force-server\n\n[fluidtiles-force-server]\nEnabled=true\nDescription=Fluid Tiles isolated server-frame test\nwmclass=fluidtiles-forced-frame\nwmclassmatch=1\ndecorationpolicy=server\ndecorationpolicyrule=2\n')
    subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log)
    forced=None
    try:
        forced,ident=spawn('Appearance forced server','forced-server')
        wait_for(lambda:get(ident)['tiled']);wait_for(settled)
        a=get(ident)['appearance']
        assert a['applied'] and not a['noBorder'] and not a['titlebarSupported'] and a['policy']==2,a
        call('SetEnabled',False);wait_for(lambda:not tiled())
        assert get(ident)['appearance']['policy']==2
        call('SetEnabled',True);wait_for(lambda:len(tiled())==4);wait_for(settled)
        assert get(ident)['appearance']['policy']==2
        close(forced,ident);forced=None
    finally:
        if forced is not None and forced.poll() is None:forced.terminate();forced.wait(timeout=4)
        rules.unlink(missing_ok=True)
        subprocess.run(['qdbus6','org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,stdout=log)
    wait_for(lambda:len(tiled())==3);wait_for(settled)
    report['forced_kwin_server_decoration_rule']='passed'

    # Restore titlebars without disabling outlines, then exercise all-window
    # hiding with a normal floating application and retain its outer geometry.
    apply(TitlebarMode=0);wait_for(lambda:all(not w['appearance']['noBorder'] for w in tiled()));wait_for(settled)
    apply(TitlebarMode=1);wait_for(lambda:all(w['appearance']['titlebarOwned'] for w in tiled()));wait_for(settled)
    call('TestWindow',source,'activate');call('Action','float');wait_for(lambda:not get(source)['tiled']);wait_for(lambda:restored(source))
    before=get(source)['actual'];apply(TitlebarMode=2)
    wait_for(lambda:get(source)['appearance']['titlebarOwned']);wait_for(lambda:max(abs(a-b) for a,b in zip(get(source)['actual'],before))<2)
    apply(TitlebarMode=1)
    wait_for(lambda:not get(source)['appearance']['noBorder']);wait_for(lambda:max(abs(a-b) for a,b in zip(get(source)['actual'],before))<2)
    call('TestWindow',source,'activate');call('Action','float');wait_for(lambda:get(source)['tiled']);wait_for(settled)
    report['titlebar_modes_and_floating_geometry']='passed'

    apply(UseSystemBorderColors=True)
    palette=(17,127,239) if status()['appearance']['activeColor']!='#ff117fef' else (18,126,238)
    expected='#ff'+''.join(f'{v:02x}' for v in palette)
    subprocess.run(['kwriteconfig6','--file','kdeglobals','--group','Colors:Selection','--key','BackgroundNormal',','.join(str(v) for v in palette)],check=True)
    wait_for(lambda:status()['appearance']['activeColor']==expected)
    report['live_kde_color_scheme']='passed'

    # Disable in each native state, restore independently, then re-enable.
    for state,reverse in [('fullscreen','unfullscreen'),('maximize','unmaximize'),('minimize','restore')]:
        call('TestWindow',source,state);wait_for(lambda:not get(source)['tiled'])
        call('SetEnabled',False);wait_for(lambda:not tiled())
        assert all(not w['appearance']['owned'] for w in status()['windows'])
        call('TestWindow',source,reverse);wait_for(lambda:restored(source))
        assert get(source)['appearance']['policy']==original[source]['policy']
        call('SetEnabled',True);wait_for(lambda:len(tiled())==3);wait_for(settled)
    report['disable_in_native_states_and_restore_geometry']='passed'

    apply(AppearanceEnabled=False);wait_for(settled)
    assert all(not w['appearance']['owned'] for w in tiled())
    assert all(w['appearance']['radius']==original[w['id']]['radius'] for w in tiled())
    apply(AppearanceEnabled=True);wait_for(settled)
    # Unload the actual renderer with hidden titlebars; inspect KWin directly.
    subprocess.run(['python3',str(root/'tools/control.py'),'stop'],check=True,stdout=log,stderr=log,timeout=12)
    for ident,baseline in original.items():
        def restored_without_renderer():
            info=native_info(ident)
            return info.get('decorationPolicy')==str(baseline['policy']) and max(abs(float(info[k])-v) for k,v in zip(['x','y','width','height'],baseline['restore']))<2
        wait_for(restored_without_renderer)
    # Start with appearance explicitly off to read back restored native radii.
    for key in ['OutlinesEnabled','RoundedCornersEnabled','TitlebarControlEnabled']:
        subprocess.run(['kwriteconfig6','--file','kwinrc','--group','Script-fluidtiles-controller','--key',key,'false'],check=True)
    subprocess.run(['python3',str(root/'tools/control.py'),'start'],check=True,stdout=log,stderr=log,timeout=15)
    wait_for(lambda:len(tiled())==3);wait_for(settled)
    assert all(get(i)['appearance']['radius']==baseline['radius'] for i,baseline in original.items())
    report['true_unload_restores_decorations_geometry_and_corners']='passed'
    apply(AppearanceEnabled=True);wait_for(settled);check_bounds()
    # Matched virtual-output metrics are diagnostic, not a hardware 144 Hz
    # acceptance claim. The guided live comparison is recorded separately.
    metrics={}
    for enabled in [False,True]:
        apply(AppearanceEnabled=enabled);wait_for(settled)
        call('ResetMetrics');call('TestWindow',source,'activate')
        for _ in range(10):call('Action','split');time.sleep(.03)
        wait_for(settled);metrics[str(enabled)]=status()['metrics']
    apply(AppearanceEnabled=True);wait_for(settled)
    capture('appearance-08-final.png')
    time.sleep(.7)
    before=status()['appearance'];time.sleep(1.0);after=status()['appearance']
    assert before['outlineDraws']==after['outlineDraws'] and before['captures']==after['captures'],('appearance repaints while idle',before,after)
    report['no_idle_outline_repaints_or_captures']='passed'
    report['matched_virtual_pacing_and_restored_final_state']='passed'
    result={'pass':True,'scale':status()['outputs'][0]['scale'],'cases':report,'metrics':metrics,'final':status(),
        'native_binary_sha256':hashlib.sha256((root/'build/plugins/kwin/effects/plugins/fluidtiles.so').read_bytes()).hexdigest(),
        'limitations':['Virtual presentation timing is not a physical 144 Hz benchmark','Application-drawn headers and transparency remain client-controlled']}
    (results/'appearance.json').write_text(json.dumps(result,indent=2)+'\n')
    return report
