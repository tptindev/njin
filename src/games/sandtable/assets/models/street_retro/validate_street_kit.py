"""Reopen and audit catalog meshes, dimensions, UVs and pedestrian clearance."""
from pathlib import Path
import json,math
import bpy,bmesh
from mathutils import Matrix,Vector
ROOT=Path(__file__).resolve().parent
manifest=json.loads((ROOT/'asset_manifest.json').read_text(encoding='utf8'))
layout=json.loads((ROOT/'sidewalk_layout.json').read_text(encoding='utf8'))
errors=[];rows=[]
def points(c,matrix=Matrix.Identity(4)):
    result=[]
    for ob in c.objects:
        if ob.type=='MESH':result.extend(matrix@ob.matrix_local@v.co for v in ob.data.vertices)
        elif ob.instance_collection:result.extend(points(ob.instance_collection,matrix@ob.matrix_local))
    return result
for item in manifest['assets']:
    c=bpy.data.collections.get(item['collection'])
    if not c or not c.asset_data:errors.append('missing asset '+item['id']);continue
    pts=points(c);lo=[min(v[i] for v in pts) for i in range(3)];hi=[max(v[i] for v in pts) for i in range(3)]
    if abs(lo[2])>.0001:errors.append('not grounded '+item['id'])
    delta=max(abs(actual[i]-item['bounds_m'][key][i]) for key,actual in [('min',lo),('max',hi)] for i in range(3))
    if delta>1e-5:errors.append('bounds mismatch '+item['id'])
    tris=0
    for ob in c.objects:
        if ob.type!='MESH':continue
        if any(abs(v-1)>1e-6 for v in ob.scale) or ob.location.length>1e-6 or ob.rotation_euler.to_quaternion().angle>1e-6:errors.append('unapplied transform '+ob.name)
        if not ob.data.uv_layers and not manifest.get('visual_profile','').startswith('city-'):errors.append('missing UV '+ob.name)
        if ob.modifiers:errors.append('unbaked modifiers '+ob.name)
        bm=bmesh.new();bm.from_mesh(ob.data)
        if any(not e.is_manifold for e in bm.edges):errors.append('nonmanifold '+ob.name)
        if any(f.calc_area()<1e-10 for f in bm.faces):
            errors.append('zero area '+ob.name)
            print('SMALL_FACES',ob.name,[(f.calc_area(),[list(v.co) for v in f.verts]) for f in bm.faces if f.calc_area()<1e-10][:3],flush=True)
        bm.free();tris+=sum(len(p.vertices)-2 for p in ob.data.polygons)
    rows.append({'id':item['id'],'ground_z_m':lo[2],'triangles_direct_meshes':tris,'dimensions_m':item['dimensions_m']})
wl,wh=layout['walkway']['bounds_xy_m']
for place in layout['placements']:
    item=next(i for i in manifest['assets'] if i['id']==place['asset_id'])
    c=bpy.data.collections[item['collection']];matrix=Matrix.Translation(Vector(place['position_m']))@Matrix.Rotation(math.radians(place['rotation_z_degrees']),4,'Z')
    pts=points(c,matrix);lo=[min(v[i] for v in pts) for i in range(2)];hi=[max(v[i] for v in pts) for i in range(2)]
    if all(lo[i]<wh[i] and hi[i]>wl[i] for i in range(2)):errors.append('walkway obstructed '+place['asset_id'])
if len(manifest['assets'])!=25:errors.append('unexpected catalog count')
report={'passed':not errors,'revision':manifest['revision'],'asset_count':len(rows),'walkway_clear':not any('walkway' in s for s in errors),'walkway_width_m':layout['walkway']['width_m'],'assets':rows,'errors':errors,'glb_export_run':False,'runtime_import_tested':False}
(ROOT/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('STREET_VALIDATION',json.dumps({'passed':report['passed'],'asset_count':len(rows),'errors':errors}),flush=True)
if errors:raise SystemExit(1)

