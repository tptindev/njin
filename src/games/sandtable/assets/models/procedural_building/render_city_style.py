"""City reference beside simplified sources; temp PNG only, no GLB or source save."""
from pathlib import Path
import os,sys
import bpy
from mathutils import Vector
BASE=Path(__file__).resolve().parent;sys.path.insert(0,str(BASE))
from export_modules import materialize
street='--street' in sys.argv
scene=bpy.data.scenes.new('City_Style_Review');bpy.context.window.scene=scene
names=['ST_StoneBenchBack','ST_FireHydrant','ST_HuTieuCart','ST_PlasticChairLow'] if street else ['PBK_Indochine_Window','PBK_Modern_Shopfront','PBK_Brick_ArcShopfront_R2_A45']
for i,name in enumerate(names):
    c=bpy.data.collections.new('Review_'+name);scene.collection.children.link(c)
    materialize(bpy.data.collections[name],c)
    for ob in c.objects:
        if not ob.parent:ob.location.x+=i*3
for i,name in enumerate(['Brick_Window_Square_Single','Trim_Window']):
    before=set(scene.objects)
    bpy.ops.import_scene.gltf(filepath=str(BASE.parent/'city'/(name+'.gltf')))
    for ob in set(scene.objects)-before:
        if not ob.parent:ob.location+=Vector((i*3+1,4,0))
world=bpy.data.worlds.new('Review_World');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.5,.55,.62,1);world.node_tree.nodes['Background'].inputs[1].default_value=.6;scene.world=world
for name,at,power in [('Key',(2,-5,10),1400),('Fill',(9,2,8),900)]:
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.size=7
    ob=bpy.data.objects.new(name,data);scene.collection.objects.link(ob);ob.location=at;ob.rotation_euler=(Vector((4,1,1))-ob.location).to_track_quat('-Z','Y').to_euler()
camera=bpy.data.objects.new('Review_Camera',bpy.data.cameras.new('Review_Camera'));scene.collection.objects.link(camera);scene.camera=camera
camera.location=(10,-16,10);camera.rotation_euler=(Vector((4,1,1.3))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=12.5
scene.render.engine='CYCLES';scene.cycles.samples=16;scene.cycles.use_denoising=True
scene.render.resolution_x=1100;scene.render.resolution_y=700;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
scene.frame_set(1);scene.render.filepath=str(Path(os.environ['TEMP'])/('city_style_street.png' if street else 'city_style_building.png'))
bpy.ops.render.render(write_still=True)
