"""Export edited .blend variants without rerunning the procedural generator."""
import bpy
import hashlib
import json
from pathlib import Path
root=Path(__file__).parent/'generated'
manifest=json.loads((root/'manifest.json').read_text())
for record in manifest['variants']:
    name=record['name']
    bpy.ops.wm.open_mainfile(filepath=str(root/(name+'.blend')))
    bpy.ops.object.select_all(action='DESELECT')
    obj=bpy.data.objects[name]; rig=bpy.data.objects[name+'_rig']
    obj.select_set(True); rig.select_set(True); bpy.context.view_layer.objects.active=rig
    path=root/record['file']
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
        export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True,
        export_frame_range=False,export_anim_slide_to_zero=True,export_yup=True,export_skins=True,export_def_bones=True)
    record['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    obj.data.calc_loop_triangles()
    record['vertices']=len(obj.data.vertices); record['triangles']=len(obj.data.loop_triangles)
    record['bones']=len(rig.data.bones)
manifest['clips']={n:[round(v[0]*30)/30,v[1]] for n,v in manifest['clips'].items()}
(root/'manifest.json').write_text(json.dumps(manifest,indent=2))
print('REEXPORT_COMPLETE')
