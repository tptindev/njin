"""Render real hinge animation frames, without saving or exporting GLB."""
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parent/'retro';FRAMES=ROOT/'shutter_animation_frames';FRAMES.mkdir(exist_ok=True)
source=bpy.data.scenes['PBK_Modular_Buildings'];scene=bpy.data.scenes.new('Shutter_Animation_Review')
bpy.context.window.scene=scene;scene.collection.children.link(bpy.data.collections['PBK_Indochine_Window']);scene.world=source.world
for ob in source.objects:
    if ob.type=='LIGHT':scene.collection.objects.link(ob)
camera=bpy.data.objects.new('Shutter_Animation_Camera',source.camera.data.copy());scene.collection.objects.link(camera);scene.camera=camera
camera.location=(1,-6,2.8);camera.rotation_euler=(Vector((1,-.12,1.65))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=2.6
scene.render.engine='CYCLES';scene.cycles.samples=8;scene.cycles.use_denoising=True
scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG'
times=[48+i*2.4 for i in range(11)]+[1+i*2.3 for i in range(1,11)]
for i,time in enumerate(times):
    scene.frame_set(int(time),subframe=time-int(time));scene.render.filepath=str(FRAMES/f'{i:03}.png')
    bpy.ops.render.render(write_still=True)
    print('SHUTTER_ANIMATION_FRAME',i,flush=True)
