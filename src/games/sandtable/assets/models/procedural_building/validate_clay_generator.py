"""Verify a reopened clay library can generate both straight and rounded houses."""
from pathlib import Path
import json,math
import bpy
ROOT=Path(__file__).resolve().parent/'clay'
scene=bpy.data.scenes['PBK_Modular_Buildings'];bpy.context.window.scene=scene
ns={'__name__':'clay_test','__file__':str(ROOT/'generate.py')}
exec(bpy.data.texts['PBK_Generator.py'].as_string(),ns)
ns['load_library']()
errors=[];results=[]
for name,shape,style in [('Clay_Test_Straight','Rectangle','Modern'),('Clay_Test_Rounded','CornerShopHouseRounded','Indochine')]:
    c,spec=ns['generate_building'](scene.collection,name=name,shape=shape,width=5,depth=5,floors=2,style=style,seed=312,offset=(0,65,0))
    meshes=[o for o in c.objects if o.type=='MESH']
    if any(not o.data.get('clay_processed') or not o.data.uv_layers for o in meshes):errors.append('unstyled generated mesh: '+name)
    rigs=[o for o in c.objects if o.type=='ARMATURE']
    if len(rigs)!=1:errors.append('expected one entrance rig: '+name)
    for frame in (1,24,72):
        scene.frame_set(frame);deps=bpy.context.evaluated_depsgraph_get()
        for rig in rigs:
            leaf=next(o for o in rig.children if o.type=='MESH')
            evaluated=leaf.evaluated_get(deps);mesh=evaluated.to_mesh()
            if len(mesh.vertices)!=len(leaf.data.vertices):errors.append('unexpected deformation topology')
            # All bevel-created vertices must remain fully weighted to the hinge.
            for v in leaf.data.vertices:
                if not v.groups or abs(sum(g.weight for g in v.groups)-1)>1e-5:errors.append('unweighted clay door vertex')
            evaluated.to_mesh_clear()
    results.append({'name':name,'shape':shape,'style':style,'direct_meshes':len(meshes),'entrance_rigs':len(rigs)})
active=[c for c in bpy.data.collections if c.asset_data]
if len(active)!=105:errors.append('catalog duplicated after reopening: '+str(len(active)))
report={'passed':not errors,'active_assets':len(active),'generated_cases':results,'errors':list(set(errors))}
(ROOT/'generator_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('CLAY_GENERATOR_VALIDATION',json.dumps(report),flush=True)
if errors:raise SystemExit(1)
