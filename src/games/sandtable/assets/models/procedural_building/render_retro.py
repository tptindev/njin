"""Render the actual retro models in isolated, uncluttered views."""
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parent/'retro'
source=bpy.data.scenes['PBK_Modular_Buildings']
views=[('retro_corner_shop.png','PBK_11_Corner_Shop_Rounded',(41,20,14),(29,37,3),15,1200,1000),
       ('retro_joint_detail.png','PBK_01_ShopHouse',(6,-2,5),(3,8,2.5),5.8,1200,1000),
       ('retro_roof_detail.png','PBK_02_Detached',(23,-1,13),(13,11,5.5),9,1100,900),
       ('retro_red_wall_detail.png','PBK_02_Detached',(21,-2,8),(13,11,3),8,1100,1000),
       ('retro_full_kit.png',None,(94,-105,95),(24,-5,2),118,1900,1450)]
for filename,collection,location,target,scale,rx,ry in views:
    scene=bpy.data.scenes.new('Retro_Review') if collection else source
    bpy.context.window.scene=scene
    if collection:
        scene.collection.children.link(bpy.data.collections[collection])
        for ob in source.objects:
            if ob.type=='LIGHT' or (ob.type=='MESH' and any(m and m.name=='PBK_ground' for m in ob.data.materials)):
                scene.collection.objects.link(ob)
        scene.world=source.world
        camera=bpy.data.objects.new('Retro_Camera',source.camera.data.copy());scene.collection.objects.link(camera)
        scene.camera=camera
    camera=scene.camera;camera.location=location
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=scale
    scene.frame_set(1)
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
    scene.view_settings.view_transform='AgX'
    scene.render.resolution_x=rx;scene.render.resolution_y=ry;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.filepath=str(ROOT/filename)
    bpy.ops.render.render(write_still=True)
    print('RETRO_RENDER_FINAL',filename,flush=True)
    if collection:
        bpy.context.window.scene=source;bpy.data.scenes.remove(scene);bpy.data.objects.remove(camera,do_unlink=True)
