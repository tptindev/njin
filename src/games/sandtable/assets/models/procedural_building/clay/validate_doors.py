"""Validate evaluated armature geometry, hinge motion and open doorway clearance."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
scene=bpy.context.scene;saved_frame=scene.frame_current
rigs=[o for o in scene.objects if o.type=='ARMATURE' and o.get('door_rig')]
errors=[];max_error=0;checked=0;collisions=0
for frame in (1,12,24,36,48,60,72,84,96):
    scene.frame_set(frame);deps=bpy.context.evaluated_depsgraph_get()
    for rig in rigs:
        leaf=next(o for o in rig.children if o.type=='MESH')
        evaluated=leaf.evaluated_get(deps);data=evaluated.to_mesh()
        amount=rig['open_amount'];angle=amount*math.pi/2
        if not -.0001<=amount<=1.0001:errors.append('Animation overshoot '+rig.name)
        for src,dst in zip(leaf.data.vertices,data.vertices):
            x,y,z=src.co;dx=x-.525;dy=y-.1
            expected=Vector((.525+dx*math.cos(angle)-dy*math.sin(angle),
                .1+dx*math.sin(angle)+dy*math.cos(angle),z))
            error=(expected-dst.co).length;max_error=max(max_error,error)
            if error>1e-5:errors.append('Wrong hinge / skinning '+rig.name)
        c=next(c for c in rig.users_collection if c.get('spec_json'))
        wall=next(o for o in c.objects if 'Continuous_Wall' in o.name and o.get('storey')==0)
        tree=BVHTree.FromPolygons([wall.matrix_world@v.co for v in wall.data.vertices],
            [tuple(f.vertices) for f in wall.data.polygons])
        moving=BVHTree.FromPolygons([leaf.matrix_world@v.co for v in data.vertices],
            [tuple(f.vertices) for f in data.polygons])
        overlaps=tree.overlap(moving)
        if overlaps:collisions+=1;errors.append('Leaf intersects wall at frame '+str(frame)+': '+rig.name)
        evaluated.to_mesh_clear()
        if frame in (1,24,48,72,96):
            hit=evaluated.ray_cast(Vector((1,-.9,1.15)),Vector((0,1,0)),distance=2)[0]
            expected_hit=frame in (1,72,96)
            if hit!=expected_hit:errors.append('Doorway occupancy wrong: '+rig.name)
        checked+=1
scene.frame_set(saved_frame)
report=dict(passed=not errors,rigs=len(rigs),poses_checked=checked,max_vertex_error_m=max_error,
    wall_collisions=collisions,errors=errors)
(Path(__file__).resolve().parent/'door_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report))
if errors:raise AssertionError(str(errors))
