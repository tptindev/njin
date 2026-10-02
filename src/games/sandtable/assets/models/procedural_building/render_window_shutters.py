"""Actual source model, shown open and closed. No source save or export."""
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parent/'retro'
source=bpy.data.scenes['PBK_Modular_Buildings']
scene=bpy.data.scenes.new('Window_Shutter_Review');bpy.context.window.scene=scene
scene.collection.children.link(bpy.data.collections['PBK_Indochine_Window'])
scene.world=source.world
for ob in source.objects:
    if ob.type=='LIGHT':scene.collection.objects.link(ob)
camera=bpy.data.objects.new('Shutter_Review_Camera',source.camera.data.copy());scene.collection.objects.link(camera);scene.camera=camera
camera.location=(1,-6,2.8);camera.rotation_euler=(Vector((1,-.12,1.65))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=2.8
scene.render.engine='CYCLES';scene.cycles.samples=24;scene.cycles.use_denoising=True
scene.render.resolution_x=1000;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG'
for frame,name in [(1,'retro_window_open.png'),(24,'retro_window_closed.png')]:
    scene.frame_set(frame);scene.render.filepath=str(ROOT/name);bpy.ops.render.render(write_still=True)
    print('SHUTTER_RENDER',name,flush=True)
