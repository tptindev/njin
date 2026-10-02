"""Render review images into --out (default TEMP); does not save or export models."""
from pathlib import Path
import argparse,os,sys,bpy
from mathutils import Vector
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
parser=argparse.ArgumentParser();parser.add_argument('--out',default=os.environ['TEMP']);out=Path(parser.parse_args(args).out)
out.mkdir(parents=True,exist_ok=True)

def instance(scene,collection,at):
    ob=bpy.data.objects.new('Preview_'+collection,None);ob.instance_type='COLLECTION';ob.instance_collection=bpy.data.collections[collection]
    ob.location=at;scene.collection.objects.link(ob)

def render(name,designs,stairs=False):
    scene=bpy.data.scenes.new(name);bpy.context.window.scene=scene
    for i,design in enumerate(designs):instance(scene,'RAIL_Modern_'+design,((i%4)*4,(i//4)*4.4,0))
    if stairs:
        instance(scene,'PBK_Modern_Stair',(4,0,0))
        instance(scene,'RAIL_Modern_StairSlope',(4+.36,0,0))
        instance(scene,'RAIL_Modern_StairSlope',(4+1.64,0,0))
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.015))
    mat=bpy.data.materials.new('Preview_Ground');mat.diffuse_color=(.35,.38,.4,1);bpy.context.object.data.materials.append(mat)
    target=Vector((6.5,4.5 if len(designs)>8 else 2.5,.6))
    cam=bpy.data.objects.new('Preview_Camera',bpy.data.cameras.new('Preview_Camera'));scene.collection.objects.link(cam)
    cam.location=target+Vector((9,-17,15));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
    cam.data.type='ORTHO';cam.data.ortho_scale=20;scene.camera=cam
    lamp=bpy.data.objects.new('Preview_Light',bpy.data.lights.new('Preview_Light','AREA'));scene.collection.objects.link(lamp)
    lamp.location=(6,-1,13);lamp.data.energy=3500;lamp.data.size=10
    scene.world=bpy.data.worlds.new('Preview_World');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.7,.75,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value=.7
    scene.render.engine='CYCLES';scene.cycles.samples=16;scene.cycles.use_denoising=True
    scene.render.resolution_x=1400;scene.render.resolution_y=950;scene.render.resolution_percentage=100
    scene.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True)

source=Path(__file__).resolve().parent.parent/'procedural_building/retro/modular_building_kit.blend'
with bpy.data.libraries.load(str(source),link=False) as (src,dst):dst.collections=['PBK_Modern_Stair']
render('retro_railings_patterns',['StraightVertical','StraightHorizontal','SquareGrid','SquareFrame',
    'DiagonalCross','DiagonalSingle','Wave','Arch','Diamond','WoodFence','BridgeHeavy','RiversideBars'])
render('retro_railings_routes',['Corner90','StairHandrail','ArcR2A45','ArcR4A30','WallHandrail','LakeLow','SolidParapet','TransitionPost'],True)
