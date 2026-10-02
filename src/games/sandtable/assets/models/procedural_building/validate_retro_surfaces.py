"""Validate zero-thickness exterior shells, aperture rays and merged house walls."""
from pathlib import Path
import json,sys,math,bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
BASE=Path(__file__).resolve().parent;sys.path.insert(0,str(BASE))
from retro_low_poly import APERTURE
ROOT=BASE/'retro';doc=json.loads((ROOT/'kit_manifest.json').read_text())
errors=[];rows=[]
for entry in doc['modules']:
    c=bpy.data.collections[entry['collection']]
    surfaces=[o for o in c.objects if o.type=='MESH' and o.get('facade_surface')]
    if not surfaces:continue
    if len(surfaces)!=1:errors.append('multiple substrate surfaces '+entry['id'])
    for ob in surfaces:
        if ob.get('render_wall_thickness_m')!=0:errors.append('wall thickness '+entry['id'])
        if any(abs(p.normal.z)>1e-5 for p in ob.data.polygons):errors.append('wall cap remains '+entry['id'])
        role=entry['id'].split('/')[1]
        if role in ('Wall','Window','Door','DoorRigged','Shopfront','Balcony'):
            if any(abs(v.co.y)>1e-6 for v in ob.data.vertices):errors.append('nonplanar wall '+entry['id'])
            if any(p.normal.y>-.999 for p in ob.data.polygons):errors.append('wrong winding '+entry['id'])
            verts=[v.co for v in ob.data.vertices];faces=[p.vertices[:] for p in ob.data.polygons]
            tree=BVHTree.FromPolygons(verts,faces)
            part='Door' if role=='DoorRigged' else role
            if part in APERTURE:
                w,h,s=APERTURE[part]
                hit=tree.ray_cast(Vector((1,-1,s+h/2)),Vector((0,1,0)),2)[0]
                if hit is not None:errors.append('blocked aperture '+entry['id'])
            elif len(ob.data.polygons)!=1 or len(ob.data.vertices)!=4:errors.append('plain wall not one quad '+entry['id'])
        if role.startswith('Arc'):
            r=float(c['radius_m'])
            if any(abs(math.hypot(v.co.x,v.co.y-r)-r)>1e-5 for v in ob.data.vertices):errors.append('arc has inner/outer layers '+entry['id'])
    rows.append({'id':entry['id'],'surface_meshes':len(surfaces),'render_thickness_m':0.,'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in surfaces)})
houses=[]
for c in bpy.data.collections:
    if not c.get('spec_json'):continue
    shells=[o for o in c.objects if o.type=='MESH' and o.get('facade_surface')]
    if len(shells)!=1:errors.append('house exterior not one mesh '+c.name)
    houses.append({'name':c.name,'exterior_meshes':len(shells)})
if not rows or not houses:errors.append('no surfaces/houses audited')
report={'passed':not errors,'modules':rows,'houses':houses,'errors':errors,'export_run':False}
(ROOT/'retro_surface_validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('RETRO_SURFACE_VALIDATION',not errors,len(rows),len(houses),errors,flush=True)
if errors:raise SystemExit(1)
