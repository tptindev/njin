"""Verify exported bytes, re-import skin/animations and render representative poses."""
import bpy
import json
import math
import struct
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).parent/'generated'
expected={'Idle_Loop','Idle_Talking_Loop','Walk_Loop','Jog_Fwd_Loop','Sprint_Loop','Punch_Jab','Punch_Cross','Hit_Chest','Death01','Sitting_Idle_Loop','Crouch_Idle_Loop','Pistol_Idle_Loop','Pistol_Shoot'}
reports=[]
for path in sorted(ROOT.glob('*.glb')):
    raw=path.read_bytes()
    magic,version,size=struct.unpack_from('<4sII',raw)
    assert magic==b'glTF' and version==2 and size==len(raw)
    n,kind=struct.unpack_from('<II',raw,12); data=json.loads(raw[20:20+n])
    names={a['name'] for a in data['animations']}
    assert names==expected, (path,names)
    assert len(data['skins'])==1 and len(data['skins'][0]['joints'])==18
    assert all('JOINTS_0' in p['attributes'] and 'WEIGHTS_0' in p['attributes'] for m in data['meshes'] for p in m['primitives'])
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(path))
    rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)]
    assert len(meshes)==1
    assert all(abs(sum(g.weight for g in v.groups)-1)<1e-5 for o in meshes for v in o.data.vertices)
    sampled={}
    rig.animation_data.use_nla=False
    for action in bpy.data.actions:
        rig.animation_data.action=action
        if hasattr(action,'slots') and len(action.slots): rig.animation_data.action_slot=action.slots[0]
        bounds=[]; endpoints=[]
        for frame in [action.frame_range[0],sum(action.frame_range)/2,action.frame_range[1]]:
            bpy.context.scene.frame_set(math.floor(frame),subframe=frame-math.floor(frame))
            dg=bpy.context.evaluated_depsgraph_get()
            coords=[o.matrix_world@v.co for o in meshes for v in o.evaluated_get(dg).data.vertices]
            assert all(math.isfinite(c) for v in coords for c in v)
            endpoints.append(coords)
            bounds.append([[min(v[i] for v in coords),max(v[i] for v in coords)] for i in range(3)])
        if action.name.endswith('_Loop'):
            error=max((a-b).length for a,b in zip(endpoints[0],endpoints[-1]))
            assert error<1e-4, (action.name,error)
        sampled[action.name]=bounds
    reports.append(dict(file=path.name,animations=sorted(names),joints=18,weights_normalized=True,finite_sampled_poses=True,bounds=sampled))
(ROOT/'validation.json').write_text(json.dumps(reports,indent=2))
# Render representative animation poses from the editable source, no engine claim.
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'reference.blend'))
scene=bpy.context.scene; rig=bpy.data.objects['reference_rig']; obj=bpy.data.objects['reference']
scene.cycles.samples=12
for idx,(clip,frame) in enumerate([('Walk_Loop',9),('Sitting_Idle_Loop',1),('Punch_Jab',8),('Death01',46)]):
    rig.animation_data.action=bpy.data.actions[clip]
    if len(rig.animation_data.action.slots): rig.animation_data.action_slot=rig.animation_data.action.slots[0]
    scene.frame_set(frame)
    dg=bpy.context.evaluated_depsgraph_get(); evaluated=obj.evaluated_get(dg)
    mesh=evaluated.to_mesh()
    copy=bpy.data.objects.new('POSE_'+clip,bpy.data.meshes.new_from_object(evaluated))
    bpy.context.collection.objects.link(copy); copy.location.x=(idx-1.5)*.9
    evaluated.to_mesh_clear()
obj.hide_render=True
scene.camera.location=(3,-8,3)
scene.camera.rotation_euler=(Vector((0,0,.8))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
scene.camera.data.ortho_scale=4.7
scene.render.resolution_x=1600; scene.render.resolution_y=900
scene.render.filepath=str(ROOT/'poses.png'); bpy.ops.render.render(write_still=True)
print('VALIDATION_OK',[(r['file'],len(r['animations'])) for r in reports])
