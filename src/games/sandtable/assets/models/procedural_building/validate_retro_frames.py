"""Check continuous frame topology, clear openings and moving leaf clearance."""
from pathlib import Path
import math,json
import bpy,bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parent/'retro'
scene=bpy.data.scenes['PBK_Modular_Buildings'];bpy.context.window.scene=scene
errors=[];results=[]
frames=[o for o in bpy.data.objects if o.type=='MESH' and o.get('continuous_frame')]
for ob in frames:
    bm=bmesh.new();bm.from_mesh(ob.data)
    pending=set(bm.verts);components=0
    while pending:
        components+=1;todo=[pending.pop()]
        while todo:
            for edge in todo.pop().link_edges:
                for v in edge.verts:
                    if v in pending:pending.remove(v);todo.append(v)
    manifold=all(e.is_manifold for e in bm.edges);volume=bm.calc_volume(signed=True)
    bm.free()
    if components!=1 or not manifold or volume<=0:errors.append('broken frame topology: '+ob.name)
    role=ob['frame_role'];width,height,sill={'Window':(1.18,1.5,.9),'Shopfront':(1.7,2.48,0),
        'Balcony':(1.6,2.4,0),'Door':(1.05,2.35,0)}[role]
    owners=[c for c in ob.users_collection if c.asset_data]
    curve=next((c for c in owners if c.get('radius_m')),None)
    point=Vector((1,0,sill+height*.5));outward=Vector((0,-1,0))
    if curve:
        r=curve['radius_m'];theta=-math.pi/2+math.radians(curve['angle_degrees'])/2
        point=Vector((r*math.cos(theta),r+r*math.sin(theta),sill+height*.5))
        outward=Vector((math.cos(theta),math.sin(theta),0))
    tree=BVHTree.FromPolygons([v.co for v in ob.data.vertices],[tuple(p.vertices) for p in ob.data.polygons])
    if tree.ray_cast(point+outward,outward*-1,2)[0] is not None:errors.append('frame fills opening: '+ob.name)
    results.append({'name':ob.name,'role':role,'components':components,'manifold':manifold,'volume_m3':volume})
cross=[o.name for o in bpy.data.objects if o.name.startswith('PBK_') and ('mullion' in o.name or 'transom' in o.name)]
if cross:errors.append('residual glass crossing bars')
rigs=[o for o in scene.objects if o.type=='ARMATURE' and o.get('door_rig')]
checked=0;collisions=0
for frame in (1,12,24,36,48,60,72,84,96):
    scene.frame_set(frame);deps=bpy.context.evaluated_depsgraph_get()
    for rig in rigs:
        prototype=next(o for o in bpy.data.collections['PBK_'+rig['style']+'_Door_Frame_Dressing'].objects if o.get('continuous_frame'))
        tree=BVHTree.FromPolygons([rig.matrix_world@v.co for v in prototype.data.vertices],
            [tuple(p.vertices) for p in prototype.data.polygons])
        leaf=next(o for o in rig.children if o.type=='MESH');ev=leaf.evaluated_get(deps);mesh=ev.to_mesh()
        moving=BVHTree.FromPolygons([ev.matrix_world@v.co for v in mesh.vertices],[tuple(p.vertices) for p in mesh.polygons])
        if tree.overlap(moving):collisions+=1;errors.append('door intersects frame: '+rig.name+'/'+str(frame))
        ev.to_mesh_clear();checked+=1
scene.frame_set(1)
if len(frames)!=24:errors.append('expected 24 continuous frame prototypes')
report={'passed':not errors,'continuous_frames':len(frames),'crossbars_remaining':cross,
        'door_poses_checked':checked,'frame_collisions':collisions,'results':results,'errors':errors,
        'glb_export_run':False}
(ROOT/'frame_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('FRAME_VALIDATION',report['passed'],len(frames),checked,collisions,errors,flush=True)
if errors:raise SystemExit(1)
