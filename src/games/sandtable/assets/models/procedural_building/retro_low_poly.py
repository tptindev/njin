"""Retro architectural surfaces: no retro fillets, exterior walls have zero thickness."""
import json,math
import bpy,bmesh
from mathutils import Vector

RETRO='city-retro-v2-tight-joints'
APERTURE={'Window':(1.18,1.50,.90),'Door':(1.05,2.35,0),'Shopfront':(1.70,2.48,0),'Balcony':(1.60,2.40,0)}

def retro_mesh(name,verts,faces,mat):
    data=bpy.data.meshes.new(name);data.from_pydata(verts,[],faces)
    if mat:data.materials.append(mat)
    bm=bmesh.new();bm.from_mesh(data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
    bm.to_mesh(data);bm.free();data.update()
    for p in data.polygons:p.use_smooth=False
    data['retro_profile']=RETRO;data['city_low_poly']='city-low-poly-v2'
    return data

def mapped(x,y,z,curvature=None):
    if not curvature:return (x,y,z)
    r,a=curvature;t=-math.pi/2+x*a/2
    return ((r-y)*math.cos(t),r+(r-y)*math.sin(t),z)

def facade_surface(role,curvature=None):
    verts=[];faces=[]
    w,h,s=APERTURE.get(role,(0,0,0));left,right=1-w/2,1+w/2
    xs=sorted(set([0,2]+([left,right] if w else [])+([2*i/6 for i in range(1,6)] if curvature else [])))
    zs=sorted(set([0,3]+([s,s+h] if w else [])))
    for a,b in zip(xs,xs[1:]):
        for lo,hi in zip(zs,zs[1:]):
            if w and left<(a+b)/2<right and s<(lo+hi)/2<s+h:continue
            n=len(verts);verts.extend(mapped(x,0,z,curvature) for x,z in [(a,lo),(b,lo),(b,hi),(a,hi)])
            faces.append(tuple(n+i for i in range(4)))
    return verts,faces

def frame_surface(role,curvature=None):
    w,h,s=APERTURE[role]
    left,right=1-w/2-.09,1+w/2+.09;il,ir=1-w/2+.05,1+w/2-.05
    bottom,top=s-.07,s+h+.07;ib,it=s+.01,s+h-.03
    rects=[(left,il,bottom,top),(ir,right,bottom,top),(il,ir,it,top)]
    if role=='Window':rects.append((il,ir,bottom,ib))
    verts=[];faces=[]
    for a,b,lo,hi in rects:
        xs=[a,b] if not curvature else [a+(b-a)*i/6 for i in range(7)]
        for x0,x1 in zip(xs,xs[1:]):
            n=len(verts)
            verts.extend(mapped(x,y,z,curvature) for x,y,z in
                         [(x0,-.13,lo),(x1,-.13,lo),(x1,-.13,hi),(x0,-.13,hi),
                          (x0,0,lo),(x1,0,lo),(x1,0,hi),(x0,0,hi)])
            faces.extend(tuple(n+i for i in f) for f in [(0,1,2,3),(7,6,5,4),(4,5,1,0),(1,5,6,2),(2,6,7,3),(3,7,4,0)])
    if role=='Door':
        # Thin reveal surfaces bridge to the existing door leaf without moving
        # its rig or adding a solid jamb that would obstruct the hinge swing.
        for quad in [((il,0,0),(il,.078,0),(il,.078,it),(il,0,it)),
                     ((ir,.078,0),(ir,0,0),(ir,0,it),(ir,.078,it)),
                     ((il,0,it),(il,.078,it),(ir,.078,it),(ir,0,it))]:
            n=len(verts);verts.extend(mapped(x,y,z,curvature) for x,y,z in quad);faces.append(tuple(n+i for i in range(4)))
    return verts,faces

def fit_panes(c,role,curvature=None):
    if role not in ('Window','Shopfront','Balcony'):return
    w,h,s=APERTURE[role];left,right=1-w/2+.05,1+w/2-.05
    bottom=s+.01 if role=='Window' else 0.;top=s+h-.03
    for ob in c.objects:
        if ob.type!='MESH' or not ob.data.materials:continue
        glass=all(m and m.name.endswith('_glass') for m in ob.data.materials)
        base=bool(ob.get('shopfront_base_ratio'))
        if not (glass or base):continue
        if base:
            frame=next((o for o in c.objects if o.type=='MESH' and 'continuous_frame' in o.name.lower()),None)
            if frame:ob.data.materials.clear();ob.data.materials.append(frame.data.materials[0])
        if ob.data.get('retro_joint_fit')==RETRO:continue
        a,b,lo,hi,y=left,right,bottom,top,0.
        if role=='Shopfront':
            split=bottom+(top-bottom)*.2
            lo,hi=(bottom,split) if base else (split,top)
            ob['shopfront_original_z']=[bottom,top]
        elif role=='Balcony':
            center=(min(v.co.x for v in ob.data.vertices)+max(v.co.x for v in ob.data.vertices))/2
            leaf=w/2-.055;a,b=center-leaf/2+.03,center+leaf/2-.03
            lo,hi,y=.46+.065/2,h-.035-.065/2,.10
        xs=[a,b] if not curvature else [a+(b-a)*i/6 for i in range(7)]
        verts=[];faces=[]
        for x0,x1 in zip(xs,xs[1:]):
            n=len(verts);verts.extend(mapped(x,y,z,curvature) for x,z in [(x0,lo),(x1,lo),(x1,hi),(x0,hi)])
            faces.append(tuple(n+i for i in range(4)))
        ob.data=retro_mesh(ob.name+'_Fitted_Pane',verts,faces,ob.data.materials[0]);ob.data['retro_joint_fit']=RETRO

def replace_collection_surface(c,style,role,curvature=None):
    if c.get('retro_surface')==RETRO:return
    old=[o for o in c.objects if o.type=='MESH' and any(k in o.name.lower() for k in ('substrate','corner_a','corner_b'))]
    if not old:return
    verts,faces=facade_surface(role,curvature)
    if role in ('OuterCorner','InnerCorner','CornerWindow90','CornerShopfront90'):
        part={'CornerWindow90':'Window','CornerShopfront90':'Shopfront'}.get(role,'Wall')
        va,fa=facade_surface(part)
        # Outer corner branches along +X / +Y; inner corner is its mirrored counterpart.
        if role in ('OuterCorner','InnerCorner'):
            verts=va+[(y,x,z) for x,y,z in va]
            faces=fa+[tuple(len(va)+i for i in reversed(f)) for f in fa]
        else:
            verts=va+[(2-y,x,z) for x,y,z in va]
            faces=fa+[tuple(len(va)+i for i in f) for f in fa]
        if role=='InnerCorner':verts=[(2-x,2-y,z) for x,y,z in verts]
    mat=bpy.data.materials.get('PBK_'+style+'_wall')
    users={cc for o in old for cc in o.users_collection}
    for ob in old:bpy.data.objects.remove(ob,do_unlink=True)
    ob=bpy.data.objects.new(c.name+'_Exterior_Surface',retro_mesh(c.name+'_Exterior_Surface',verts,faces,mat))
    ob['facade_surface']=True;ob['render_wall_thickness_m']=0.;ob['collision_wall_thickness_m']=.2
    for cc in users:cc.objects.link(ob);cc['retro_surface']=RETRO

def point_inside(p,points):
    x,y=p;inside=False
    for a,b in zip(points,points[1:]+points[:1]):
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:inside=not inside
    return inside

def thin_generated_wall(ob):
    if ob.get('facade_surface'):return
    c=next((c for c in ob.users_collection if c.get('spec_json')),None)
    if not c:return
    spec=json.loads(c['spec_json']);cells={tuple(p) for p in spec.get('cells',[])}
    level=ob.get('storey',0)
    if spec.get('stepback') and level==spec['floors']-1 and spec['floors']>1:
        smaller={p for p in cells if p[1]<spec['depth_bays']-1}
        if smaller:cells=smaller
    footprint=spec.get('footprint_polygon')
    verts=[];faces=[]
    for p in ob.data.polygons:
        if abs(p.normal.z)>.05:continue
        center=sum((ob.data.vertices[i].co for i in p.vertices),Vector())/len(p.vertices)
        q=center+p.normal*.03
        inside=point_inside((q.x,q.y),footprint) if footprint else (math.floor(q.x/2),math.floor(q.y/2)) in cells
        if inside:continue
        n=len(verts);verts.extend(tuple(ob.data.vertices[i].co) for i in p.vertices)
        faces.append(tuple(n+i for i in range(len(p.vertices))))
    if faces:
        ob.data=retro_mesh(ob.name+'_Surface',verts,faces,ob.data.materials[0])
        ob['facade_surface']=True;ob['render_wall_thickness_m']=0.;ob['collision_wall_thickness_m']=.2

def add_window_cross(c,style,curvature=None):
    """Centered, continuous cross; same material as the frame, fitted to its pane."""
    if any(o.get('shutter_rig') for o in c.objects):return
    panes=[o for o in c.objects if o.type=='MESH' and o.data.materials and
           all(m and m.name.endswith('_glass') for m in o.data.materials)]
    if curvature:
        for ob in list(c.objects):
            if ob.get('window_crossbar'):bpy.data.objects.remove(ob,do_unlink=True)
        for pane in panes:
            if 'window_cross_version' in pane:del pane['window_cross_version']
        c['window_cross_pattern']='none'
        return
    for pane in panes:
        if pane.get('window_cross_version')=='balanced-plus-v1':continue
        left,right,bottom,top=.46,1.54,.91,2.37
        cx,cz=(left+right)/2,(bottom+top)/2;half=.02
        xs=sorted(set([left,cx-half,cx+half,right]+([left+(right-left)*i/6 for i in range(1,6)] if curvature else [])))
        zs=[bottom,cz-half,cz+half,top]
        cells={(i,j) for i in range(len(xs)-1) for j in range(3)
               if j==1 or cx-half-1e-8<=(xs[i]+xs[i+1])/2<=cx+half+1e-8}
        verts=[];faces=[];indices={}
        def vertex(xi,zj,side):
            key=(xi,zj,side)
            if key not in indices:
                indices[key]=len(verts);verts.append(mapped(xs[xi],-.035 if side==0 else 0.,zs[zj],curvature))
            return indices[key]
        for i,j in sorted(cells):
            corners=[(i,j),(i+1,j),(i+1,j+1),(i,j+1)]
            faces.append(tuple(vertex(x,z,0) for x,z in corners))
            faces.append(tuple(vertex(x,z,1) for x,z in reversed(corners)))
            for a,b,neighbor in [(corners[0],corners[1],(i,j-1)),(corners[1],corners[2],(i+1,j)),
                                 (corners[2],corners[3],(i,j+1)),(corners[3],corners[0],(i-1,j))]:
                if neighbor not in cells:faces.append((vertex(*a,0),vertex(*a,1),vertex(*b,1),vertex(*b,0)))
        frame=next((o for o in c.objects if o.type=='MESH' and 'continuous_frame' in o.name.lower()),None)
        mat=frame.data.materials[0] if frame else bpy.data.materials['PBK_'+style+'_wood']
        data=retro_mesh(c.name+'_Balanced_Window_Cross',verts,faces,mat)
        ob=bpy.data.objects.new(data.name,data);ob['window_crossbar']=True;ob['bar_width_m']=.04
        ob['window_cross_center']=[cx,cz];ob['window_cross_curvature']=list(curvature) if curvature else []
        # The canonical pane and its dressing share the same source object.
        # Link the cross to both so corner windows and generated houses inherit it.
        for target in pane.users_collection:target.objects.link(ob)
        pane['window_cross_version']='balanced-plus-v1'
        c['window_cross_pattern']='balanced_plus'

def simplify_balcony(c):
    """Replace individual rectangular details, never the entire balcony's bounds."""
    objects=[o for o in c.objects if o.type=='MESH']
    if 'balcony_optimization_baseline' not in c:
        c['balcony_optimization_baseline']=sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in objects)
    for ob in objects:
        if not any(k in ob.name.lower() for k in ('baluster','balcony_slab','door_threshold','door_handle')):continue
        if ob.vertex_groups or ob.data.get('balcony_detail_low_poly')=='box-v1':continue
        old=ob.data
        if sum(len(p.vertices)-2 for p in old.polygons)<=12:continue
        lo=[min(v.co[i] for v in old.vertices) for i in range(3)]
        hi=[max(v.co[i] for v in old.vertices) for i in range(3)]
        points=[(lo[0] if x==0 else hi[0],lo[1] if y==0 else hi[1],lo[2] if z==0 else hi[2]) for x,y,z in
                [(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)]]
        data=retro_mesh(ob.name+'_Retro_Box',points,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],old.materials[0])
        data['balcony_detail_low_poly']='box-v1'
        data['balcony_detail_bounds_json']=json.dumps({'min':lo,'max':hi})
        # Dressing and placed instances retain the same objects/transforms.
        old.user_remap(data)
    c['balcony_low_poly']='box-details-v1'
    c['balcony_triangles_after']=sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in objects)

def restore_stair(c,style):
    """One closed mesh with 15 real treads; retain the module's original sockets."""
    meshes=[o for o in c.objects if o.type=='MESH']
    if len(meshes)==1 and meshes[0].data.get('retro_stair_profile')=='15x200x250':return
    # Clockwise section in Y/Z, extruded across X. Side n-gons triangulate
    # without overlapping boxes or internal faces between adjacent steps.
    profile=[(0.,0.)]
    for i in range(15):
        profile.extend([(i*.25,(i+1)*.2),((i+1)*.25,(i+1)*.2)])
    profile.append((3.75,0.))
    n=len(profile)
    verts=[(x,y,z) for x in (.4,1.6) for y,z in profile]
    faces=[tuple(range(n)),tuple(range(2*n-1,n-1,-1))]
    faces.extend((i,i+n,(i+1)%n+n,(i+1)%n) for i in range(n))
    mat=meshes[0].data.materials[0] if meshes and meshes[0].data.materials else bpy.data.materials['PBK_'+style+'_concrete']
    data=retro_mesh(c.name+'_Stepped_Mesh',verts,faces,mat)
    data['retro_stair_profile']='15x200x250'
    if meshes:
        ob=meshes[0];ob.data=data;ob.matrix_world.identity()
        for other in meshes[1:]:bpy.data.objects.remove(other,do_unlink=True)
    else:
        ob=bpy.data.objects.new(c.name+'_Steps',data);c.objects.link(ob)
    ob['stair_steps']=15;c['retro_stair_profile']='15x200x250'

def apply_retro_style():
    walls=frames=0
    for c in list(bpy.data.collections):
        ident=c.get('module_id','')
        if '/' not in ident:continue
        style,role=ident.split('/',1)
        curvature=(float(c['radius_m']),math.radians(float(c['angle_degrees']))) if c.get('radius_m') and role.startswith('Arc') else None
        part=role.split('_')[0][3:] if curvature else role
        if part=='Stair':restore_stair(c,style)
        if part=='Balcony':simplify_balcony(c)
        if part in ('Wall','Window','Door','Shopfront','Balcony','OuterCorner','InnerCorner','CornerWindow90','CornerShopfront90','DoorRigged'):
            replace_collection_surface(c,style,'Door' if part=='DoorRigged' else part,curvature);walls+=1
        if part in APERTURE:
            for ob in list(c.objects):
                if ob.type=='MESH' and 'continuous_frame' in ob.name.lower() and ob.data.get('retro_profile')!=RETRO:
                    v,f=frame_surface(part,curvature);ob.data=retro_mesh(ob.name+'_Retro',v,f,ob.data.materials[0]);frames+=1
            fit_panes(c,part,curvature)
            if part=='Window':add_window_cross(c,style,curvature)
        if part=='RoofSlope' and not c.get('retro_roof'):
            for ob in list(c.objects):
                if ob.type=='MESH':bpy.data.objects.remove(ob,do_unlink=True)
            data=retro_mesh(c.name+'_Roof_Surface',[(0,0,0),(2,0,0),(2,2,1),(0,2,1)],[(0,1,2,3)],bpy.data.materials['PBK_'+style+'_roof'])
            c.objects.link(bpy.data.objects.new(data.name,data));c['retro_roof']=True
        if part=='Gable' and not c.get('retro_gable'):
            for ob in list(c.objects):
                if ob.type=='MESH':bpy.data.objects.remove(ob,do_unlink=True)
            data=retro_mesh(c.name+'_Gable_Surface',[(0,0,0),(4,0,0),(2,0,1)],[(1,2,0)],bpy.data.materials['PBK_'+style+'_wall'])
            c.objects.link(bpy.data.objects.new(data.name,data));c['retro_gable']=True
    for ob in list(bpy.data.objects):
        if ob.type!='MESH':continue
        if 'continuous_wall' in ob.name.lower():thin_generated_wall(ob)
        curved=ob.get('curved_dressing') or any('_Arc' in c.name for c in ob.users_collection)
        box_names=('shaft','capital','base_trim','door_kick_panel','door_stile','door_rail','front_rail','side_rail','rail_post','ridge','parapet','coping','step')
        if (not curved and not ob.vertex_groups and not ob.get('facade_surface') and
            not ob.data.get('retro_stair_profile') and
            any(k in ob.name.lower() for k in box_names) and ob.data.get('retro_profile')!=RETRO and ob.data.vertices):
            old=ob.data;lo=[min(v.co[i] for v in old.vertices) for i in range(3)];hi=[max(v.co[i] for v in old.vertices) for i in range(3)]
            points=[(lo[0] if x==0 else hi[0],lo[1] if y==0 else hi[1],lo[2] if z==0 else hi[2]) for x,y,z in
                    [(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)]]
            new=retro_mesh(ob.name+'_Block',points,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],old.materials[0] if old.materials else None)
            old.user_remap(new)
        for p in ob.data.polygons:p.use_smooth=False
    # A generated house uses one exterior wall mesh across its storeys. The
    # storey face attribute keeps cutaway/LOD authoring information available.
    for c in list(bpy.data.collections):
        if not c.get('spec_json'):continue
        shells=[o for o in c.objects if o.type=='MESH' and o.get('facade_surface')]
        if len(shells)<2:continue
        verts=[];faces=[];levels=[];mat=shells[0].data.materials[0]
        for ob in shells:
            n=len(verts);verts.extend(tuple(ob.matrix_local@v.co) for v in ob.data.vertices)
            for p in ob.data.polygons:
                faces.append(tuple(n+i for i in p.vertices));levels.append(ob.get('storey',0))
        data=retro_mesh(c.name+'_Exterior_Surface',verts,faces,mat)
        attr=data.attributes.new('storey','INT','FACE')
        for i,level in enumerate(levels):attr.data[i].value=level
        for ob in shells:bpy.data.objects.remove(ob,do_unlink=True)
        ob=bpy.data.objects.new(data.name,data);ob['facade_surface']=True;ob['render_wall_thickness_m']=0.
        ob['collision_wall_thickness_m']=.2;c.objects.link(ob)
        c['exterior_wall_meshes']=1;c['render_wall_thickness_m']=0.
    for mat in bpy.data.materials:
        if mat.name.startswith('PBK_') and mat.name.endswith('_wall'):mat.use_backface_culling=True
        if mat.use_nodes:
            bs=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
            if bs:
                bs.inputs['Roughness'].default_value=.8
                if 'Specular IOR Level' in bs.inputs:bs.inputs['Specular IOR Level'].default_value=.1
        mat['retro_profile']=RETRO
    return {'profile':RETRO,'surface_collections':walls,'frames_rebuilt':frames}
