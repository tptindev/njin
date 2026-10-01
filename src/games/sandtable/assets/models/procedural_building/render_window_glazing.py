"""Render proof: open wooden shutters / closed see-through glass. No source save."""
from pathlib import Path
import sys,bpy
from mathutils import Vector
BASE=Path(__file__).resolve().parent;ROOT=BASE/'clay';sys.path.insert(0,str(BASE))
from export_modules import materialize
source=bpy.context.scene
scene=bpy.data.scenes.new('Glazing_Review');bpy.context.window.scene=scene;scene.world=source.world
for name,offset in [('PBK_Indochine_Window',0),('PBK_Modern_Window',2.6)]:
    c=bpy.data.collections.new(name+'_Review');scene.collection.children.link(c)
    materialize(bpy.data.collections[name],c)
    for ob in c.objects:
        if not ob.parent:ob.location.x+=offset
    # Review marker is behind the aperture, so visibility proves a real opening/transmission.
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16,radius=.30,location=(1+offset,.7,1.65))
    marker=bpy.context.object;marker.name='Interior_Visibility_Marker'
    mat=bpy.data.materials.new('Review_Marker');mat.use_nodes=True
    bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    bs.inputs['Base Color'].default_value=(.8,.07,.02,1)
    marker.data.materials.append(mat)
for ob in source.objects:
    if ob.type=='LIGHT':scene.collection.objects.link(ob)
camera=bpy.data.objects.new('Glazing_Review_Camera',source.camera.data.copy());scene.collection.objects.link(camera);scene.camera=camera
camera.location=(2.3,-8,2.15);camera.rotation_euler=(Vector((2.3,.05,1.5))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=5.6
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1200;scene.render.resolution_y=800;scene.render.resolution_percentage=100
formats=[i.identifier for i in scene.render.image_settings.bl_rna.properties['file_format'].enum_items]
assert 'PNG' in formats;scene.render.image_settings.file_format='PNG'
scene.frame_set(1);scene.render.filepath=str(ROOT/'window_glazing_review.png');bpy.ops.render.render(write_still=True)
