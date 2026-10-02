"""Check actual stair heights, closed topology and reusable generator after reopening."""
from pathlib import Path
import json,sys,bpy,bmesh
from mathutils import Vector
BASE=Path(__file__).resolve().parent
ROOT=BASE/'retro'
sys.path.insert(0,str(BASE))
from retro_low_poly import apply_retro_style

def check():
    rows=[]
    for style in ('Indochine','Modern','Brick'):
        c=bpy.data.collections['PBK_'+style+'_Stair']
        meshes=[o for o in c.objects if o.type=='MESH']
        assert len(meshes)==1,(style,'not a single mesh')
        ob=meshes[0];data=ob.data
        bm=bmesh.new();bm.from_mesh(data)
        assert all(e.is_manifold for e in bm.edges),(style,'open or overlapping topology')
        volume=bm.calc_volume(signed=True);bm.free()
        assert abs(volume-7.2)<1e-5,(style,'winding or volume',volume)
        heights=[]
        for i in range(15):
            hit,co,normal,_=ob.ray_cast(Vector((1,(i+.5)*.25,4)),Vector((0,0,-1)))
            assert hit and abs(co.z-(i+1)*.2)<1e-5 and normal.z>.999,(style,i,'missing tread')
            heights.append(round(co.z,3))
        assert sum(abs(p.normal.z-1)<1e-5 for p in data.polygons)==15
        assert sum(len(p.vertices)-2 for p in data.polygons)==124
        rows.append({'module':style+'/Stair','vertices':len(data.vertices),'triangles':124,
                     'tread_heights_m':heights,'volume_m3':round(volume,5),'closed':True})
    return rows

rows=check()
apply_retro_style();apply_retro_style()
assert check()==rows,'style pass changed staircase'
ns={'__name__':'stair_generator_check','__file__':str(ROOT/'generate.py')}
exec(bpy.data.texts['PBK_Generator.py'].as_string(),ns)
ns['load_library']()
assert check()==rows,'reopened generator changed staircase'
report={'passed':True,'revision':'unified-kit-v3-retro-v16-stairs','cases':rows,
        'idempotent_style_pass':True,'generator_reopened':True,'export_run':False}
(ROOT/'stair_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print('RETRO_STAIR_VALIDATION_PASS',json.dumps(report),flush=True)
