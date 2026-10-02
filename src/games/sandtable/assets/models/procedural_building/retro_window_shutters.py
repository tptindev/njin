"""Working full-size wooden shutters, with real hinge bones and sloped louvers."""
import math
import bpy,bmesh
from mathutils import Vector,Matrix

def build_window_shutters(c,curvature=None):
    if any(o.get('shutter_rig') for o in c.objects):return
    wood=bpy.data.materials['PBK_Indochine_wood'];metal=bpy.data.materials['PBK_Indochine_metal']
    # Fit the clear opening of continuous_frame(), rather than overlaying its rim.
    gap=.004;left=.46+gap;right=1.54-gap;bottom=.91+gap;top=2.37-gap
    border=.055;hinge_y=-.164;panel_y=-.094;mid_z=(bottom+top)/2
    hinge_heights=(bottom+.14,mid_z,top-.14)
    def bend(p):
        x,y,z=p
        if not curvature:return Vector(p)
        r,a=curvature;t=-math.pi/2+x*a/2
        return Vector(((r-y)*math.cos(t),r+(r-y)*math.sin(t),z))
    data=bpy.data.armatures.new(c.name+'_Shutter_Bones')
    rig=bpy.data.objects.new(c.name+'_Shutter_Rig',data);c.objects.link(rig)
    bpy.context.scene.collection.objects.link(rig)
    rig['shutter_rig']=True;rig['open_angle_degrees']=115.;rig['clip']='Shutter_CloseOpen'
    rig['closed_frame']=24;rig['open_frame']=1;rig.show_in_front=True
    rig['louver_angle_degrees']=-25.;rig['louver_slope']='outward-down when closed'
    rig['closed_clearance_m']=gap;rig['center_gap_m']=.008
    rig['shutter_radius_m']=curvature[0] if curvature else 0.
    rig['shutter_arc_radians']=curvature[1] if curvature else 0.
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig
    bpy.ops.object.mode_set(mode='EDIT')
    for side,hinge in [('Left',left),('Right',right)]:
        bone=data.edit_bones.new('Shutter_'+side);bone.head=bend((hinge,hinge_y,bottom));bone.tail=bone.head+Vector((0,0,top-bottom))
    bpy.ops.object.mode_set(mode='OBJECT')
    for side,lo,hi,sign in [('Left',left,.996,-1),('Right',1.004,right,1)]:
        verts=[];faces=[];mats=[]
        outer=rounded_loop(lo,hi,bottom,top,.031,6)
        inner=rounded_loop(lo+border,hi-border,bottom+border,top-border,.02,6);n=len(outer)
        for y in (panel_y-.027,panel_y+.027):verts.extend((x,y,z) for x,z in outer+inner)
        for i in range(n):
            j=(i+1)%n
            faces.extend([(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),
                          (i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)])
        mats.extend([0]*len(faces))
        def cuboid(center,size,tilt=0,material=0):
            base=len(verts);rot=Matrix.Rotation(tilt,3,'X')
            for sx,sy,sz in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]:
                p=rot@Vector((sx*size[0]/2,sy*size[1]/2,sz*size[2]/2))+Vector(center);verts.append(tuple(p))
            faces.extend(tuple(base+j for j in f) for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
            mats.extend([material]*6)
        # Each blade is a tilted solid, seated into the side stiles. Gaps admit light.
        # Front is -Y: a negative X tilt puts the outer edge below the inner edge.
        blade_half=(.104*math.cos(math.radians(25))+.014*math.sin(math.radians(25)))/2
        first=bottom+border+blade_half;last=top-border-blade_half
        for i in range(13):cuboid(((lo+hi)/2,panel_y,first+(last-first)*i/12),(hi-lo-2*border+.012,.014,.104),math.radians(-25))
        cuboid(((lo+hi)/2,-.142,mid_z),(.12,.025,.025),material=1)
        hx=lo if side=='Left' else hi
        for z in hinge_heights:
            # Moving hinge straps join the panel to the fixed pivot barrel.
            cuboid((hx+(.022 if side=='Left' else -.022),-.132,z),(.036,.06,.035),material=1)
        mesh=bpy.data.meshes.new(c.name+'_Shutter_'+side);mesh.from_pydata(verts,[],faces)
        mesh.materials.append(wood);mesh.materials.append(metal)
        for p,idx in zip(mesh.polygons,mats):p.material_index=idx
        bm=bmesh.new();bm.from_mesh(mesh);bm.normal_update()
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        bmesh.ops.bevel(bm,geom=[e for e in bm.edges if len(e.link_faces)==2 and e.calc_face_angle(0)>.5],
                        offset=.004,segments=3,affect='EDGES',clamp_overlap=True)
        if curvature:
            for x in (j/32 for j in range(1,64)):
                bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=1e-6,
                    plane_co=(x,0,0),plane_no=(1,0,0),clear_inner=False,clear_outer=False)
        for v in bm.verts:v.co=bend(v.co)
        bm.normal_update()
        for f in bm.faces:f.smooth=True
        for e in bm.edges:e.smooth=len(e.link_faces)==2 and e.calc_face_angle(0)<.7
        bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update()
        uv=mesh.uv_layers.new(name='RetroUV')
        for p in mesh.polygons:
            for loop in p.loop_indices:
                v=mesh.vertices[mesh.loops[loop].vertex_index].co;uv.data[loop].uv=(v.x*4,v.z*4)
        mesh['retro_processed']=True
        ob=bpy.data.objects.new(mesh.name,mesh);c.objects.link(ob);ob.parent=rig
        ob['shutter_leaf']=side;ob['hinge_clearance_m']=.019;ob['closed_clearance_m']=gap
        group=ob.vertex_groups.new(name='Shutter_'+side);group.add(list(range(len(mesh.vertices))),1,'REPLACE')
        mod=ob.modifiers.new('Shutter_Hinge','ARMATURE');mod.object=rig
        bone=rig.pose.bones['Shutter_'+side];bone.rotation_mode='XYZ'
        for frame,amount in [(1,1),(24,0),(48,0),(72,1),(96,1)]:
            bone.rotation_euler.y=sign*math.radians(115)*amount
            bone.keyframe_insert(data_path='rotation_euler',frame=frame,group=bone.name)
        # Real barrel hinges at three heights, mounted on the outer wooden jamb.
        for z in hinge_heights:
            bm=bmesh.new();bmesh.ops.create_cone(bm,cap_ends=True,segments=12,radius1=.014,radius2=.014,depth=.085)
            for v in bm.verts:v.co+=Vector((hx,hinge_y,z))
            bracket=bmesh.ops.create_cube(bm,size=1)['verts']
            for v in bracket:v.co=Vector((v.co.x*.065+hx+(-.030 if side=='Left' else .030),v.co.y*.06-.155,v.co.z*.035+z))
            for v in bm.verts:v.co=bend(v.co)
            hd=bpy.data.meshes.new(c.name+'_Hinge');bm.to_mesh(hd);bm.free();hd.materials.append(metal)
            hd['retro_processed']=True
            ho=bpy.data.objects.new(hd.name,hd);c.objects.link(ho);ho['shutter_hardware']=True
    rig.animation_data.action.name=c.name+'_Shutter_CloseOpen'
    preview_action=rig.animation_data.action
    # Separate editable open/close clips in addition to the combined preview.
    for suffix,amounts in [('Open',(0,1)),('Close',(1,0))]:
        action_name=c.name+'_Shutter_'+suffix
        action=bpy.data.actions.get(action_name) or bpy.data.actions.new(action_name)
        for curve in list(action.fcurves):action.fcurves.remove(curve)
        action.use_fake_user=True;action['shutter_action']=True
        rig.animation_data.action=action
        for side,sign in [('Left',-1),('Right',1)]:
            bone=rig.pose.bones['Shutter_'+side]
            for frame,amount in zip((1,24),amounts):
                bone.rotation_euler.y=sign*math.radians(115)*amount
                bone.keyframe_insert(data_path='rotation_euler',frame=frame,group=bone.name)
        rig['action_'+suffix.lower()]=action_name
    rig.animation_data.action=preview_action
    bpy.context.scene.frame_set(1)
    bpy.context.scene.collection.objects.unlink(rig)

def ensure_window_shutters():
    for c in list(bpy.data.collections):
        if c.name=='PBK_Indochine_Window' or (c.asset_data and c.name.startswith('PBK_Indochine_ArcWindow_')):
            for ob in list(c.objects):
                if 'shutter' in ob.name.lower() and not (ob.get('shutter_leaf') or ob.get('shutter_rig') or ob.get('shutter_hardware')):
                    bpy.data.objects.remove(ob,do_unlink=True)
            curvature=(float(c['radius_m']),math.radians(float(c['angle_degrees']))) if '_Arc' in c.name else None
            build_window_shutters(c,curvature)
            dressing=bpy.data.collections.get(c.name+'_Dressing')
            if dressing:
                for ob in c.objects:
                    if (ob.get('shutter_leaf') or ob.get('shutter_rig') or ob.get('shutter_hardware')) and ob.name not in dressing.objects:
                        dressing.objects.link(ob)
