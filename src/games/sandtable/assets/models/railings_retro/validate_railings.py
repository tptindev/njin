"""Validate the saved source geometry, joints and deterministic selection rules."""
from pathlib import Path
import json,math,sys,bpy,bmesh
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
from rule_reference import choose,boundary_masks
catalog=json.loads((ROOT/'asset_manifest.json').read_text(encoding='utf8'))
rules=json.loads((ROOT/'railing_rules.json').read_text(encoding='utf8'))
assert len(catalog['assets'])==66
assert len({a['id'] for a in catalog['assets']})==66
assert set(rules['asset_ids'])=={a['id'] for a in catalog['assets']}
covered=set(rules['terminals'].values())|set(rules['route_shapes'].values())-{'selected context design'}
results=[]
for entry in catalog['assets']:
    c=bpy.data.collections[entry['collection']]
    assert c.asset_data and c['asset_id']==entry['id']
    assert entry['triangles']<1500
    points=[]
    for ob in c.objects:
        assert ob.type=='MESH' and not ob.modifiers and not ob.vertex_groups
        assert ob.matrix_world==__import__('mathutils').Matrix.Identity(4)
        data=ob.data;bm=bmesh.new();bm.from_mesh(data)
        assert all(e.is_manifold for e in bm.edges),(entry['id'],ob.name,'open component')
        assert all(f.calc_area()>1e-9 for f in bm.faces),(entry['id'],'degenerate face')
        assert bm.calc_volume(signed=True)>0,(entry['id'],'inverted normals')
        bm.free();points.extend(v.co for v in data.vertices)
        for mat in data.materials:
            bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
            assert bs.inputs['Alpha'].default_value==1
            assert not any(n.type=='TEX_IMAGE' for n in mat.node_tree.nodes)
    for axis in range(3):
        assert abs(min(p[axis] for p in points)-entry['bounds_m']['min'][axis])<1e-6
        assert abs(max(p[axis] for p in points)-entry['bounds_m']['max'][axis])<1e-6
    # Main handrail terminates exactly at both advertised connection points.
    if entry['design'] not in ('EndPost','TransitionPost'):
        body=next(o for o in c.objects if o.name.endswith('_RailBody'))
        for name in ('handrail_start','handrail_end'):
            center=Vector(entry['sockets'][name])
            assert min((p.co-center).length for p in body.data.vertices)<.05,(entry['id'],name)
    if entry['design']=='Corner90':assert entry['sockets']['end']==[2,2,0]
    if entry['design'].startswith('Stair'):
        assert entry['sockets']['end']==[0,3.75,3]
        assert entry['connection_orientation']['start_yaw_degrees']==90
    results.append({'id':entry['id'],'triangles':entry['triangles'],'passed':True})
for context in rules['contexts']:
    weights=rules['contexts'][context]['design_weights']
    seen={choose(rules,seed,'edge-chain',context,'Modern').rsplit('/',1)[1] for seed in range(2000)}
    assert seen==set(weights);covered|=seen
    for style in rules['styles']:
        for shape in rules['route_shapes']:
            a=choose(rules,7261,'same-chain',context,style,shape)
            assert a==choose(rules,7261,'same-chain',context,style,shape)
assert covered=={a['design'] for a in catalog['assets']}
assert boundary_masks(3)==[{'start':True,'end':False},{'start':True,'end':False},{'start':True,'end':True}]
report={'passed':True,'revision':catalog['revision'],'assets_checked':66,'designs_covered':len(covered),
        'total_triangles':sum(a['triangles'] for a in catalog['assets']),
        'checks':['closed component topology and outward normals','nondegenerate faces','bounds and origin',
                  'rail endpoints match sockets','stair and corner orientation','opaque texture-free materials',
                  'deterministic choice and full design coverage','shared post ownership'],
        'export_run':False,'runtime_pending':['game placement, culling and LOD','physics barriers','GLB roundtrip']}
(ROOT/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
status=json.loads((ROOT/'source_status.json').read_text(encoding='utf8'))
status['source_validated']=True
(ROOT/'source_status.json').write_text(json.dumps(status,indent=2)+'\n',encoding='utf8')
print('RAILINGS_VALIDATION_PASS',json.dumps(report),flush=True)
