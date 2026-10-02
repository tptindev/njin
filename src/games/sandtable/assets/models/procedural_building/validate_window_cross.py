"""Validate centered window crosses, curve fit, corners and generator reuse."""
from pathlib import Path
import json,math,sys,bpy,bmesh
ROOT=Path(__file__).resolve().parent/'retro'
sys.path.insert(0,str(ROOT.parent))
from retro_low_poly import apply_retro_style
from export_modules import materialize

def geometry():
    rows=[]
    for c in bpy.data.collections:
        ident=c.get('module_id','')
        if '/Window' not in ident and '/ArcWindow' not in ident:continue
        crosses=[o for o in c.objects if o.get('window_crossbar')]
        if ident.startswith('Indochine/') or '/ArcWindow' in ident:
            assert not crosses;continue
        assert len(crosses)==1,ident
        ob=crosses[0];data=ob.data
        bm=bmesh.new();bm.from_mesh(data)
        assert all(e.is_manifold for e in bm.edges),ident
        assert all(f.calc_area()>1e-9 for f in bm.faces),ident
        assert bm.calc_volume(signed=True)>0,ident
        bm.free()
        curve=list(ob['window_cross_curvature']);points=[]
        for v in data.vertices:
            x,y,z=v.co
            if curve:
                r,a=curve;x=2*(math.atan2(y-r,x)+math.pi/2)/a;y=r-math.hypot(v.co.x,v.co.y-r)
            points.append((x,y,z))
        for axis,lo,hi in [(0,.46,1.54),(1,-.035,0),(2,.91,2.37)]:
            assert abs(min(p[axis] for p in points)-lo)<1e-5
            assert abs(max(p[axis] for p in points)-hi)<1e-5
        assert max(abs(a-b) for a,b in zip(ob['window_cross_center'],[1.,1.64]))<1e-6
        assert abs(ob['bar_width_m']-.04)<1e-6
        frame=next(o for o in c.objects if 'continuous_frame' in o.name.lower())
        assert data.materials[0]==frame.data.materials[0]
        assert ob in bpy.data.collections[c.name+'_Dressing'].objects[:],ident
        rows.append({'id':ident,'triangles':sum(len(p.vertices)-2 for p in data.polygons)})
    return sorted(rows,key=lambda x:x['id'])

rows=geometry();assert len(rows)==2
apply_retro_style();apply_retro_style();assert geometry()==rows
scene=bpy.context.scene;corners=[]
for style in ('Indochine','Modern','Brick'):
    temp=bpy.data.scenes.new('Cross_Corner_Test');bpy.context.window.scene=temp
    materialize(bpy.data.collections['PBK_'+style+'_CornerWindow90'],temp.collection)
    count=sum(bool(o.get('window_crossbar')) for o in temp.objects)
    assert count==(0 if style=='Indochine' else 2),(style,count)
    corners.append({'id':style+'/CornerWindow90','crosses':count})
    bpy.context.window.scene=scene;obs=list(temp.objects);bpy.data.scenes.remove(temp)
    for ob in obs:bpy.data.objects.remove(ob,do_unlink=True)
ns={'__name__':'window_cross_generator_test','__file__':str(ROOT/'generate.py')}
exec(bpy.data.texts['PBK_Generator.py'].as_string(),ns);ns['load_library']()
assert geometry()==rows
assert len([c for c in bpy.data.collections if c.asset_data])==105
report={'passed':True,'revision':'unified-kit-v3-retro-v17-window-cross','crosses':rows,'corners':corners,
        'bar_width_m':.04,'centered':True,'frame_material_match':True,'dressing_linked':True,
        'scope':'flat glazed windows only; curved glass and shutter windows excluded',
        'idempotent':True,'generator_reopened':True,'export_run':False}
(ROOT/'window_cross_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print('WINDOW_CROSS_VALIDATION_PASS',json.dumps(report),flush=True)
