"""Reopen, materialize and check physical shutter coverage and motion. No export."""
from pathlib import Path
import json,sys,math
import bpy
from mathutils.bvhtree import BVHTree
BASE=Path(__file__).resolve().parent;ROOT=BASE/'retro';sys.path.insert(0,str(BASE))
from export_modules import materialize,bounds
original=bpy.context.scene;errors=[];cases=[]
def bvh(ob,deps):
    ev=ob.evaluated_get(deps);mesh=ev.to_mesh()
    verts=[ev.matrix_world@v.co for v in mesh.vertices];faces=[tuple(p.vertices) for p in mesh.polygons]
    result=BVHTree.FromPolygons(verts,faces,epsilon=0);ev.to_mesh_clear();return result
names=['PBK_Indochine_Window','PBK_Indochine_ArcWindow_R2_A45','PBK_Indochine_ArcWindow_R4_A30','PBK_Indochine_CornerWindow90']
for name in names:
    scene=bpy.data.scenes.new('Shutter_Check');bpy.context.window.scene=scene
    materialize(bpy.data.collections[name],scene.collection)
    rigs=[o for o in scene.objects if o.type=='ARMATURE' and o.get('shutter_rig')]
    leaves=[o for o in scene.objects if o.type=='MESH' and o.get('shutter_leaf')]
    if len(leaves)!=2*len(rigs) or not rigs:errors.append('missing pair: '+name)
    for ob in leaves:
        if any(len(v.groups)!=1 or abs(v.groups[0].weight-1)>1e-6 for v in ob.data.vertices):errors.append('invalid rigid weights '+ob.name)
        if ob.modifiers[0].object not in rigs:errors.append('missing armature linkage')
    collisions=0
    for frame in (1,8,16,24,36,48,56,64,72,84,96):
        scene.frame_set(frame);bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
        fixed=[o for o in scene.objects if o.type=='MESH' and not o.get('shutter_leaf') and not o.get('shutter_hardware')]
        for leaf in leaves:
            tree=bvh(leaf,deps)
            for ob in fixed:
                if tree.overlap(bvh(ob,deps)):
                    collisions+=1;errors.append('shutter intersects '+ob.name+' at '+str(frame)+' in '+name)
    scene.frame_set(24);bpy.context.view_layer.update()
    # Every wooden vertex must fit within the rounded reveal, with a 4 mm gap.
    # Undo rigid placement and curved-facade mapping before measuring the fit.
    fit_violations=0
    for rig in rigs:
        for leaf in [o for o in leaves if o.parent==rig]:
            ev=leaf.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=ev.to_mesh()
            wood_verts={v for p in mesh.polygons if p.material_index==0 for v in p.vertices}
            transform=rig.matrix_world.inverted()@ev.matrix_world
            for index in wood_verts:
                p=transform@mesh.vertices[index].co;x,z=p.x,p.z
                r=rig.get('shutter_radius_m',0);a=rig.get('shutter_arc_radians',0)
                if r:x=(math.atan2(p.y-r,p.x)+math.pi/2)*2/a
                qx=abs(x-1)-(.54-.035);qz=abs(z-1.64)-(.73-.035)
                distance=min(max(qx,qz),0)+math.hypot(max(qx,0),max(qz,0))-.035
                if distance>-.0038:fit_violations+=1
            ev.to_mesh_clear()
        # Standalone actions must be real bone keyframes, not metadata-only clips.
        for kind,expected in [('open',(0,115)),('close',(115,0))]:
            action=bpy.data.actions.get(rig.get('action_'+kind,''))
            if not action:errors.append('missing '+kind+' clip');continue
            curve=next((fc for fc in action.fcurves if fc.data_path=='pose.bones["Shutter_Left"].rotation_euler' and fc.array_index==1),None)
            if not curve or any(abs(abs(math.degrees(curve.evaluate(frame)))-angle)>.01 for frame,angle in zip((1,24),expected)):
                errors.append('incorrect '+kind+' bone keys')
    if fit_violations:errors.append('wooden panels extend outside rounded reveal '+name)
    if name=='PBK_Indochine_Window':
        # On closed louvers, outward-facing broad surfaces must face upward.
        # This checks actual mesh normals rather than the angle metadata.
        for leaf in leaves:
            ev=leaf.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=ev.to_mesh()
            front_slopes=[p.normal.z for p in mesh.polygons if p.area>.001 and p.normal.y<-.75 and .25<abs(p.normal.z)<.6]
            if not front_slopes or any(z<0 for z in front_slopes):errors.append('louvers slope inward rather than outward-down')
            ev.to_mesh_clear()
        ext=bounds(leaves,bpy.context.evaluated_depsgraph_get())
        if ext['min'][0]>.465 or ext['max'][0]<1.535 or ext['min'][2]>.915 or ext['max'][2]<2.365:
            errors.append('closed leaves too small for clear opening')
    for rig in rigs:
        for bone in ('Shutter_Left','Shutter_Right'):
            scene.frame_set(24);a=rig.pose.bones[bone].matrix_basis.to_quaternion().copy()
            scene.frame_set(1);b=rig.pose.bones[bone].matrix_basis.to_quaternion().copy()
            if abs(math.degrees(a.rotation_difference(b).angle)-115)>.01:errors.append('incorrect angle')
    cases.append({'module':name,'rigs':len(rigs),'leaves':len(leaves),'poses_checked':11,'collisions':collisions,'closed_fit_violations':fit_violations})
    bpy.context.window.scene=original;obs=list(scene.objects);bpy.data.scenes.remove(scene)
    for ob in obs:bpy.data.objects.remove(ob,do_unlink=True)
ns={'__name__':'shutter_generator_check','__file__':str(ROOT/'generate.py')}
exec(bpy.data.texts['PBK_Generator.py'].as_string(),ns);ns['load_library']()
ns['generate_building'](original.collection,name='Shutter_Generated_House',shape='CornerShopHouseRounded',width=5,depth=5,floors=2,style='Indochine',seed=312,offset=(0,65,0))
if len([c for c in bpy.data.collections if c.asset_data])!=105:errors.append('catalog count changed')
report={'passed':not errors,'cases':cases,'generator_reopened':True,'errors':list(set(errors)),'export_run':False}
(ROOT/'shutter_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('SHUTTER_VALIDATION',json.dumps(report),flush=True)
if errors:raise SystemExit(1)
