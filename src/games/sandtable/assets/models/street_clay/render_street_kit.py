"""Render actual authored assets, not proxies; never exports or saves source."""
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
for name,filename in [('Street_Clay_Catalog','street_catalog.png'),('Street_Clay_Sidewalk','street_sidewalk.png')]:
    scene=bpy.data.scenes[name];bpy.context.window.scene=scene
    scene.render.filepath=str(ROOT/filename);bpy.ops.render.render(write_still=True)
    print('STREET_RENDER',filename,flush=True)
# A readable view of food preparation, cups and real chair geometry.
scene=bpy.data.scenes['Street_Clay_Sidewalk'];bpy.context.window.scene=scene
scene.camera.location=(-7,-7,6);scene.camera.rotation_euler=(Vector((-5.8,.20,1))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
scene.camera.data.ortho_scale=9.0;scene.render.resolution_x=1500;scene.render.resolution_y=1100
scene.render.filepath=str(ROOT/'street_food_detail.png');bpy.ops.render.render(write_still=True)
print('STREET_RENDER street_food_detail.png',flush=True)

