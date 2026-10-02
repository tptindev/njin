"""Run in Blender 4.4: exec(compile(open(PATH, encoding='utf8').read(), PATH, 'exec')).
Creates an independent review scene and reusable local module library. No downloads.
"""
import bpy
import math
import json
import random
from pathlib import Path
from mathutils import Vector

OUT = Path(__file__).resolve().parent if '__file__' in globals() else Path(r'D:/projects/njin/src/games/sandtable/assets/models/procedural_building/retro')
BAY, HEIGHT, THICK = 2.0, 3.0, 0.2
PREFIX = 'PBK_'
LIBRARY = {}
DRESSING = {}
CATALOG_REVISION = 'unified-kit-v3-retro-v18-low-poly-balcony'
BASE_ROLES = ('Floor','Wall','Window','Door','Shopfront','Balcony','OuterCorner','InnerCorner',
              'Column','Stair','Parapet','RoofSlope','Ridge','Gable')
APERTURES = {'Window':(1.18,1.50,.90),'Door':(1.05,2.35,0),
             'Shopfront':(1.70,2.48,0),'Balcony':(1.60,2.40,0)}
OPENINGS = []
PALETTES = {
    'Indochine': ((0.72,0.55,0.30,1),(0.93,0.83,0.63,1),(0.10,0.24,0.19,1),(0.34,0.10,0.055,1)),
    'Modern': ((0.69,0.73,0.73,1),(0.92,0.94,0.92,1),(0.09,0.14,0.18,1),(0.12,0.20,0.26,1)),
    'Brick': ((0.43,0.18,0.10,1),(0.74,0.66,0.52,1),(0.13,0.12,0.10,1),(0.23,0.12,0.085,1)),
}

def enum_set(owner, prop, wanted):
    values = {i.identifier for i in owner.bl_rna.properties[prop].enum_items}
    if wanted not in values:
        raise ValueError(f'{prop}: {wanted} not in {values}')
    setattr(owner, prop, wanted)

def collection(name, parent=None):
    c = bpy.data.collections.new(name)
    if parent:
        parent.children.link(c)
    return c

def material(name, rgba, metallic=0):
    m = bpy.data.materials.new(PREFIX + name)
    m.use_nodes = True
    m.diffuse_color = rgba
    shader = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    shader.inputs['Base Color'].default_value = rgba
    shader.inputs['Roughness'].default_value = 0.65
    shader.inputs['Metallic'].default_value = metallic
    return m

def mesh(c, name, verts, faces, mat):
    data = bpy.data.meshes.new(PREFIX + name)
    data.from_pydata(verts, [], faces)
    data.update()
    ob = bpy.data.objects.new(PREFIX + name, data)
    c.objects.link(ob)
    if mat:
        data.materials.append(mat)
    return ob

def box(c, name, at, size, mat):
    x,y,z = at
    a,b,d = (v/2 for v in size)
    return mesh(c, name, [(x+sx*a,y+sy*b,z+sz*d) for sx,sy,sz in
        [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]],
        [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],mat)

def opening_wall(c, style, role, mats, width, height, sill):
    """Apply an Exact Boolean aperture to the structural wall before dressing."""
    wall = box(c, 'substrate', (1,.1,1.5),(2,.2,3),mats['wall'])
    cutter = box(c,'temporary_cutter',(1,.1,sill+height/2),(width,.8,height),None)
    mod = wall.modifiers.new('Aperture', 'BOOLEAN')
    enum_set(mod, 'operation', 'DIFFERENCE')
    enum_set(mod, 'solver', 'EXACT')
    mod.object = cutter
    bpy.context.view_layer.objects.active = wall
    wall.select_set(True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    wall.select_set(False)
    bpy.data.objects.remove(cutter, do_unlink=True)
    wall['opening_id'] = f'{style}/{role}'
    wall['aperture_applied'] = True
    OPENINGS.append({'id':f'{style}/{role}','type':'window' if sill else 'door',
                     'width':width,'height':height,'sill':sill,'wall_depth':.2,'destination':'interior'})
    for x in (1-width/2,1+width/2):
        box(c,'jamb',(x,-.035,sill+height/2),(.10,.13,height+.14),mats['trim'])
    for z in ((sill+height,) if role=='Balcony' else (sill,sill+height)):
        box(c,'lintel_sill',(1,-.055,z),(width+.18,.18,.10),mats['trim'])
    if role=='Balcony':
        # Two full-height French door leaves, with a low kick panel and handles.
        box(c,'door_threshold',(1,.10,.012),(width,.24,.024),mats['trim'])
        for side in (-1,1):
            center=1+side*width/4
            leaf_width=width/2-.055
            box(c,'balcony_door_glass',(center,.10,1.40),(leaf_width-.08,.045,1.84),mats['glass'])
            box(c,'door_kick_panel',(center,.085,.24),(leaf_width,.06,.42),mats['frame'])
            for x in (center-leaf_width/2,center+leaf_width/2):
                box(c,'door_stile',(x,.045,height/2),(.06,.11,height-.035),mats['frame'])
            for z in (.04,.46,height-.035):
                box(c,'door_rail',(center,.045,z),(leaf_width,.11,.065),mats['frame'])
            box(c,'door_handle',(1+side*.095,-.035,1.03),(.045,.065,.19),mats['metal'])
        wall['balcony_access']=True
        wall['clear_width']=width-.10
        wall['threshold_m']=.024
        OPENINGS[-1].update(destination='balcony',door_style='double_full_height',clear_width=width-.10,threshold_m=.024)
        return
    # Inset glass or door leaf: not a painted rectangle over a solid wall.
    box(c,'glass' if role != 'Door' else 'door_leaf',(1,.10,sill+height/2),
        (width-.10,.045,height-.06),mats['glass'] if role != 'Door' else mats['frame'])
    if role != 'Door':
        box(c,'mullion',(1,.015,sill+height/2),(.045,.08,height),mats['frame'])
        box(c,'transom',(1,.015,sill+height*.60),(width,.08,.045),mats['frame'])
    else:
        box(c,'handle',(1+width*.30,-.04,1.05),(.04,.07,.16),mats['metal'])

def rails(c, mats):
    box(c,'balcony_slab',(1,-.48,-.08),(1.8,1.12,.16),mats['trim'])
    for z in (.14,1.10):
        box(c,'front_rail',(1,-1.01,z),(1.8,.055,.055),mats['metal'])
        for x in (.13,1.87):
            box(c,'side_rail',(x,-.52,z),(.055,1.03,.055),mats['metal'])
    for i in range(10):
        box(c,'baluster',(.15+i*1.7/9,-1.01,.62),(.035,.035,.96),mats['metal'])
    for x in (.13,1.87):
        for y in (-.15,-.55):
            box(c,'side_baluster',(x,y,.62),(.035,.035,.96),mats['metal'])

def shop_awning(c,mats):
    # Author the canopy in its mounting coordinates: rear enters the wall,
    # and even the lowest front support clears the 2.48m opening and lintel.
    front_y,rear_y=-.85,.015
    front_z,rear_z=2.60,2.74
    for i in range(8):
        x0=.06+i*.235;x1=x0+.235
        mesh(c,'awning_stripe',[(x0,front_y,front_z-.04),(x1,front_y,front_z-.04),
             (x1,rear_y,rear_z-.04),(x0,rear_y,rear_z-.04),
             (x0,front_y,front_z),(x1,front_y,front_z),
             (x1,rear_y,rear_z),(x0,rear_y,rear_z)],
             [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],
             mats['trim'] if i%2 else mats['roof'])
    box(c,'awning_wall_mount',(1,.005,2.72),(1.92,.12,.085),mats['frame'])
    box(c,'awning_front_rail',(1,-.82,2.57),(1.88,.04,.045),mats['metal'])
    for x in (.15,1.85):
        box(c,'awning_anchor',(x,.005,2.64),(.10,.05,.17),mats['metal'])
        rear=Vector((x,-.015,2.69));front=Vector((x,-.82,2.57))
        beam=box(c,'awning_support',(0,0,0),(.045,(rear-front).length,.045),mats['metal'])
        beam.location=(rear+front)/2
        beam.rotation_euler.x=math.atan2(rear.z-front.z,rear.y-front.y)

def register_module(style, role, mats):
    c = collection(PREFIX+style+'_'+role,bpy.context.scene.collection)
    bpy.context.view_layer.update()
    LIBRARY[(style,role)] = c
    c['module_id'] = f'{style}/{role}'
    c['nominal_size'] = [2,2,3] if role in ('OuterCorner','InnerCorner') else [2,.2,3]
    c['origin'] = 'bottom-left; facade front=-Y; up=+Z'
    if role in ('Wall','Window','Door','Shopfront','Balcony'):
        if role == 'Wall':
            box(c,'substrate',(1,.1,1.5),(2,.2,3),mats['wall'])
        else:
            w,h,s = APERTURES[role]
            opening_wall(c,style,role,mats,w,h,s)
        box(c,'base_trim',(1,-.015,.10),(2,.06,.20),mats['trim']) if role in ('Wall','Window') else None
        box(c,'floor_band',(1,-.025,2.92),(2,.08,.16),mats['trim'])
        if role == 'Shopfront':
            box(c,'signboard',(1,-.10,2.84),(1.75,.18,.16),mats['frame'])
            shop_awning(c,mats)
        if role == 'Balcony': rails(c,mats)
    elif role == 'Floor':
        box(c,'slab',(1,1,-.1),(2,2,.2),mats['floor'])
        c['nominal_size']=[2,2,.2]
    elif role in ('OuterCorner','InnerCorner'):
        # Connected L substrate: union so the corner itself is manifold.
        a=box(c,'corner_a',(1,.1,1.5),(2,.2,3),mats['wall'])
        b=box(c,'corner_b',(.1,1,1.5),(.2,2,3),mats['wall'])
        mod=a.modifiers.new('CornerUnion','BOOLEAN')
        enum_set(mod,'operation','UNION'); enum_set(mod,'solver','EXACT'); mod.object=b
        bpy.context.view_layer.objects.active=a
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(b,do_unlink=True)
        if role == 'InnerCorner':
            for ob in c.objects:
                for v in ob.data.vertices: v.co.x=2-v.co.x; v.co.y=2-v.co.y
    elif role == 'Column':
        box(c,'shaft',(0,0,1.5),(.24,.24,3),mats['trim'])
        for z in (.12,2.88): box(c,'capital',(0,0,z),(.34,.34,.24),mats['trim'])
        c['nominal_size']=[.34,.34,3]
    elif role == 'Stair':
        for i in range(15):
            h=(i+1)*.2
            box(c,'step',(1,(i+.5)*.25,h/2),(1.2,.25,h),mats['floor'])
        c['nominal_size']=[1.2,3.75,3]
    elif role == 'Parapet':
        box(c,'parapet',(1,.10,.38),(2,.2,.76),mats['wall'])
        box(c,'coping',(1,.1,.80),(2,.28,.08),mats['trim'])
        c['nominal_size']=[2,.28,.84]
    elif role == 'RoofSlope':
        # 2m wide, 2m run, 1m rise. Closed prism, authored normals outward.
        mesh(c,'slope',[(0,0,0),(2,0,0),(2,2,1),(0,2,1),
                        (0,0,-.10),(2,0,-.10),(2,2,.90),(0,2,.90)],
             [(0,1,2,3),(7,6,5,4),(4,5,1,0),(5,6,2,1),(6,7,3,2),(7,4,0,3)],mats['roof'])
        for i in range(8):
            y=(i+.5)/4
            # Rotate about the rib's own center, then mount to the roof plane.
            # Putting the center into mesh vertices rotates its position about (0,0,0).
            ob=box(c,'roof_rib',(0,0,0),(2,.028,.03),mats['roof'])
            ob.rotation_euler.x=math.atan(.5)
            ob.location=Vector((1,y,y/2))+Vector((0,-.5,1)).normalized()*.014
            ob['roof_mount']='parallel to z=0.5*y; bottom inset 1mm'
        c['nominal_size']=[2,2,1.1]
    elif role == 'Ridge':
        box(c,'ridge',(1,0,.04),(2,.20,.10),mats['roof'])
        c['nominal_size']=[2,.2,.1]
    elif role == 'Gable':
        mesh(c,'gable',[(0,0,0),(4,0,0),(2,0,1),(0,.2,0),(4,.2,0),(2,.2,1)],
             [(1,2,0),(5,4,3),(3,4,1,0),(4,5,2,1),(5,3,0,2)],mats['wall'])
        c['nominal_size']=[4,.2,1]
    bpy.context.scene.collection.children.unlink(c)
    c.asset_mark()
    c.asset_data.description = f'2m grid / 3m storey. {style} {role}. Local origin and sockets in manifest.'
    return c

def instance(c, style, role, at, angle=0, scale=(1,1,1)):
    ob = bpy.data.objects.new(PREFIX+role,None)
    c.objects.link(ob)
    enum_set(ob,'instance_type','COLLECTION')
    ob.instance_collection=LIBRARY[(style,role)]
    ob.location=at; ob.rotation_euler.z=angle; ob.scale=scale
    ob['module_id']=f'{style}/{role}'
    ob['canonical_module_id']=f'{style}/{role}';ob['catalog_revision']=CATALOG_REVISION
    return ob

def footprint(shape,w,d):
    cells={(x,y) for x in range(w) for y in range(d)}
    if shape=='L': cells={p for p in cells if p[0]<max(1,w//2) or p[1]<max(1,d//2)}
    if shape=='U': cells={p for p in cells if p[0]==0 or p[0]==w-1 or p[1]==0}
    if shape=='T': cells={p for p in cells if p[1]==d-1 or p[0]==w//2}
    if shape=='Courtyard': cells={p for p in cells if p[0] in (0,w-1) or p[1] in (0,d-1)}
    if shape=='Corner': cells.discard((w-1,d-1))
    return cells

def edges(cells):
    for x,y in sorted(cells):
        # CCW boundary; +Y is always the building side of a facade bay.
        for neighbor,start,angle in [((x,y-1),(x*2,y*2),0),((x+1,y),((x+1)*2,y*2),math.pi/2),
                                     ((x,y+1),((x+1)*2,(y+1)*2),math.pi),((x-1,y),(x*2,(y+1)*2),-math.pi/2)]:
            if neighbor not in cells: yield start,angle,neighbor

def boundary_ring(c,name,cells,z,height,mat,outer=0,inner=THICK):
    """One welded wall ring, including courtyard loops, with mitered corners."""
    segments=[];incoming={};outgoing={}
    for p,a,n in edges(cells):
        q=(round(p[0]+2*math.cos(a),6),round(p[1]+2*math.sin(a),6))
        p=tuple(float(v) for v in p)
        normal=Vector((-math.sin(a),math.cos(a)))
        if p in outgoing or q in incoming:
            raise ValueError('Footprint has a pinched vertex; separate diagonally touching boundary loops')
        incoming[q]=normal;outgoing[p]=normal;segments.append((p,q))
    vertices=[];indices={};faces=[]
    def vertex(p,offset,top):
        n0=incoming[p];n1=outgoing[p]
        miter=(n0+n1)/(1+n0.dot(n1))
        xy=Vector(p)+offset*miter
        key=(round(xy.x,6),round(xy.y,6),round(z+top*height,6))
        if key not in indices: indices[key]=len(vertices);vertices.append(key)
        return indices[key]
    for p,q in segments:
        a,b=vertex(p,outer,0),vertex(q,outer,0)
        d,e=vertex(p,inner,0),vertex(q,inner,0)
        A,B=vertex(p,outer,1),vertex(q,outer,1)
        D,E=vertex(p,inner,1),vertex(q,inner,1)
        faces.extend([(a,b,B,A),(e,d,D,E),(a,d,e,b),(A,B,E,D)])
    ob=mesh(c,name,vertices,faces,mat)
    ob['continuous_boundary']=True
    return ob

def facade_dressing(style,role):
    key=(style,role)
    if key not in DRESSING:
        name=PREFIX+style+'_'+role+'_Dressing'
        c=bpy.data.collections.get(name) or collection(name)
        for ob in LIBRARY[key].objects:
            if 'substrate' in ob.name or 'floor_band' in ob.name: continue
            if ob.name not in c.objects: c.objects.link(ob)
        DRESSING[key]=c
    return DRESSING[key]

def continuous_storey(c,style,cells,facades,level,offset):
    wallmat=bpy.data.materials[PREFIX+style+'_wall']
    trim=bpy.data.materials[PREFIX+style+'_trim']
    wall=boundary_ring(c,'Continuous_Wall',cells,0,3,wallmat)
    wall.location=(offset[0],offset[1],offset[2]+level*3)
    wall['storey']=level;wall['opening_count']=sum(f['role']!='Wall' for f in facades)
    cutters=[]
    dimensions=APERTURES
    for f in facades:
        if f['role']=='Wall': continue
        w,h,s=dimensions[f['role']];x,y,z=f['local_position'];a=f['rotation_z']
        bottom=s if s else -.025
        cut=box(c,'Aperture_Cutter',(0,0,0),(w,THICK+.06,s+h-bottom),None)
        cut.rotation_euler.z=a
        cut.location=(offset[0]+x+math.cos(a)-.1*math.sin(a),
                      offset[1]+y+math.sin(a)+.1*math.cos(a),offset[2]+level*3+(bottom+s+h)/2)
        cutters.append(cut)
    if cutters:
        # One Boolean across disjoint closed cutters; no bay substrate overlaps.
        verts=[];polys=[]
        for ob in cutters:
            ob.update_tag()
        bpy.context.view_layer.update()
        for ob in cutters:
            base=len(verts);verts.extend([ob.matrix_world @ v.co for v in ob.data.vertices])
            polys.extend([tuple(base+i for i in p.vertices) for p in ob.data.polygons])
        cutter=mesh(c,'All_Aperture_Cutters',verts,polys,None)
        bpy.context.view_layer.update()
        mod=wall.modifiers.new('All_Apertures','BOOLEAN')
        enum_set(mod,'operation','DIFFERENCE');enum_set(mod,'solver','EXACT');mod.object=cutter
        mod.use_self=True
        bpy.context.view_layer.objects.active=wall
        bpy.ops.object.modifier_apply(modifier=mod.name)
        for ob in cutters+[cutter]: bpy.data.objects.remove(ob,do_unlink=True)
    wall['apertures_applied']=True
    band=boundary_ring(c,'Continuous_Floor_Band',cells,2.84,.16,trim,outer=-.065,inner=.025)
    band.location=wall.location.copy();band['storey']=level
    return wall

def rig_entrance(c, facade, style):
    """Independent rigid one-bone door. Frame stays fixed; leaf and handle move together."""
    if facade.get('door_rig'): return bpy.data.objects.get(facade['door_rig'])
    source=LIBRARY[(style,'Door')]
    frame_name=PREFIX+style+'_Door_Frame_Dressing'
    frame=bpy.data.collections.get(frame_name) or collection(frame_name)
    for ob in facade_dressing(style,'Door').objects:
        if 'door_leaf' in ob.name or 'handle' in ob.name: continue
        if ob.name not in frame.objects:frame.objects.link(ob)
    facade.instance_collection=frame
    facade['canonical_module_id']=f'{style}/DoorRigged'
    data=bpy.data.armatures.new(PREFIX+'Entrance_Rig')
    rig=bpy.data.objects.new(PREFIX+'Entrance_Rig',data);c.objects.link(rig)
    rig.location=facade.location;rig.rotation_euler=facade.rotation_euler;rig.scale=facade.scale
    rig.show_in_front=True;enum_set(data,'display_type','WIRE')
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.object.mode_set(mode='EDIT')
    bone=data.edit_bones.new('Door_Hinge');bone.head=(.525,.10,0);bone.tail=(.525,.10,2.35)
    bpy.ops.object.mode_set(mode='OBJECT')
    template=LIBRARY.get((style,'DoorRigged'))
    template_leaf=next((o for o in template.objects if o.type=='MESH' and o.get('door_rig')),None) if template else None
    verts=[];faces=[]
    for ob in source.objects:
        if 'door_leaf' not in ob.name and 'handle' not in ob.name:continue
        base=len(verts);verts.extend(ob.matrix_local@v.co for v in ob.data.vertices)
        faces.extend(tuple(base+i for i in f.vertices) for f in ob.data.polygons)
    # Add a stem between the previously offset handle and the leaf surface.
    stem=box(c,'Handle_Stem',(1.315,.035,1.05),(.045,.12,.045),bpy.data.materials[PREFIX+style+'_metal'])
    base=len(verts);verts.extend(v.co.copy() for v in stem.data.vertices)
    faces.extend(tuple(base+i for i in f.vertices) for f in stem.data.polygons)
    leaf=mesh(c,'Animated_Door_Leaf',verts,faces,bpy.data.materials[PREFIX+style+'_frame'])
    leaf.data.materials.append(bpy.data.materials[PREFIX+style+'_metal'])
    leaf_faces=6
    for f in leaf.data.polygons:
        if f.index>=leaf_faces:f.material_index=1
    bpy.data.objects.remove(stem,do_unlink=True)
    if template_leaf:
        old_data=leaf.data;leaf.data=template_leaf.data
        if old_data.users==0:bpy.data.meshes.remove(old_data)
    leaf.parent=rig
    group=leaf.vertex_groups.get('Door_Hinge') or leaf.vertex_groups.new(name='Door_Hinge');group.add(list(range(len(verts))),1,'REPLACE')
    deform=leaf.modifiers.new('Rigid_Door_Armature','ARMATURE');deform.object=rig
    rig['open_amount']=0.0
    rig.id_properties_ui('open_amount').update(min=0.0,max=1.0,soft_min=0.0,soft_max=1.0,
        description='0 = closed; 1 = 90 degrees inward. Keyframed open / hold / close loop.')
    rig['door_rig']=True;rig['style']=style;rig['hinge_local']=[.525,.1,0]
    rig['opening_angle_degrees']=90;rig['animation_frames']='1 closed / 24 open / 48 open / 72 closed / 96 closed'
    rig['facade_object']=facade.name;facade['door_rig']=rig.name;leaf['door_rig']=rig.name
    pose=rig.pose.bones['Door_Hinge'];pose.rotation_mode='XYZ'
    driver=pose.driver_add('rotation_euler',1).driver;driver.type='SCRIPTED';driver.expression='open_amount * 1.5707963267948966'
    var=driver.variables.new();var.name='open_amount';var.type='SINGLE_PROP';var.targets[0].id=rig;var.targets[0].data_path='["open_amount"]'
    for frame_number,value in [(1,0.0),(24,1.0),(48,1.0),(72,0.0),(96,0.0)]:
        rig['open_amount']=value;rig.keyframe_insert(data_path='["open_amount"]',frame=frame_number)
    action=rig.animation_data.action;action.name=PREFIX+'Door_OpenClose_'+c.name
    for curve in action.fcurves:
        for key in curve.keyframe_points:key.interpolation='BEZIER';key.handle_left_type='AUTO_CLAMPED';key.handle_right_type='AUTO_CLAMPED'
        curve.modifiers.new('CYCLES')
    rig['open_amount']=0.0
    return rig

def continuous_parapet(c,style,cells,at):
    for role,z,h,mat,outer,inner in [('Parapet',0,.76,'wall',0,.2),('Coping',.76,.08,'trim',-.04,.24)]:
        ob=boundary_ring(c,'Continuous_'+role,cells,z,h,bpy.data.materials[PREFIX+style+'_'+mat],outer,inner)
        ob.location=at

def polygon_ring(c,name,points,z,height,mat,outer=0,inner=THICK):
    """Closed mitered ring on an arbitrary CCW footprint, including rounded corners."""
    verts=[];n=len(points)
    for i,p in enumerate(points):
        prev=Vector(points[i-1]);here=Vector(p);nxt=Vector(points[(i+1)%n])
        a=(here-prev).normalized();b=(nxt-here).normalized()
        na=Vector((-a.y,a.x));nb=Vector((-b.y,b.x));m=(na+nb)/(1+na.dot(nb))
        for t in (outer,inner):
            q=here+m*t
            verts.extend([(q.x,q.y,z),(q.x,q.y,z+height)])
    faces=[]
    for i in range(n):
        a=i*4;b=((i+1)%n)*4
        faces.extend([(a,b,b+1,a+1),(a+2,a+3,b+3,b+2),
                      (a,a+2,b+2,b),(a+1,b+1,b+3,a+3)])
    ob=mesh(c,name,verts,faces,mat);ob['continuous_boundary']=True
    return ob

def polygon_slab(c,points,z,mat):
    n=len(points);verts=[(x,y,z+t) for t in (-.20,0) for x,y in points]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
    return mesh(c,'Rounded_Floor',verts,faces,mat)

def rounded_corner_building(parent,name,width,depth,floors,style,seed,offset,window_density):
    """South / curved corner / east storefront zones form one continuous frontage."""
    w,d=width*BAY,depth*BAY
    radius=min(4.0,w-2,d-2);cx,cy=w-radius,radius
    arc_steps=48
    points=[(0,0),(cx,0)]
    points.extend((cx+radius*math.cos(-math.pi/2+i*math.pi/2/arc_steps),
                   cy+radius*math.sin(-math.pi/2+i*math.pi/2/arc_steps)) for i in range(1,arc_steps+1))
    points.extend([(w,d),(0,d)])
    c=collection(PREFIX+name,parent);rng=random.Random(seed)
    mats={k:bpy.data.materials[PREFIX+style+'_'+k] for k in ('wall','trim','frame','glass','metal')}
    def shift(ob,level=0): ob.location=(offset[0],offset[1],offset[2]+level*HEIGHT)
    bays=[]
    for axis,length in [('south',cx),('east',d-cy)]:
        count=max(1,round(length/BAY));span=length/count
        for i in range(count):
            at=(i*span,0) if axis=='south' else (w,cy+i*span)
            bays.append(dict(zone=axis,at=at,a=0 if axis=='south' else math.pi/2,scale=span/2))
    count=max(1,round(radius*math.pi/2/BAY));span=math.pi/2/count
    for i in range(count):
        theta=-math.pi/2+(i+.5)*span;a=theta+math.pi/2
        center=Vector((cx+radius*math.cos(theta),cy+radius*math.sin(theta)))
        bays.append(dict(zone='corner',at=tuple(center-Vector((math.cos(a),math.sin(a)))),
                         a=a,scale=1,theta=theta,span=span,index=i))
    placements=[];opening_checks=[]
    for level in range(floors):
        shift(polygon_slab(c,points,0,mats['trim']),level)
        wall=polygon_ring(c,'Rounded_Continuous_Wall',points,0,HEIGHT,mats['wall']);shift(wall,level)
        wall['storey']=level
        cutters=[]
        for b in bays:
            role='Door' if level==0 and b['zone']=='corner' and b['index']==count//2 else ('Shopfront' if level==0 else 'Window')
            if level>0 and rng.random()>window_density: role='Wall'
            if role=='Wall': continue
            a=b['a'];at=b['at'];curved=b['zone']=='corner' and role!='Door'
            if curved:
                arc_role=arc_id(role,radius,b['span'])
                theta0=b['theta']-b['span']/2
                start=(offset[0]+cx+radius*math.cos(theta0),offset[1]+cy+radius*math.sin(theta0),offset[2]+level*3)
                ob=instance(c,style,arc_role,start,theta0+math.pi/2)
                ob.instance_collection=facade_dressing(style,arc_role)
                ob['curved_dressing']=True
            else:
                # Doors stay planar and operable at the tangent of the curved entrance bay.
                ob=instance(c,style,role,(offset[0]+at[0],offset[1]+at[1],offset[2]+level*3),a,(b['scale'],1,1))
                ob.instance_collection=facade_dressing(style,role)
                if role=='Door':rig_entrance(c,ob,style)
            ow,oh,sill=APERTURES[role]
            scale=b['span']*radius/2 if b['zone']=='corner' else b['scale']
            center=Vector((at[0]+math.cos(a),at[1]+math.sin(a))) if b['zone']=='corner' else Vector((at[0]+math.cos(a)*b['scale'],at[1]+math.sin(a)*b['scale']))
            inward=Vector((-math.sin(a),math.cos(a)))
            cut=box(c,'Rounded_Cutter',(0,0,0),(ow*scale,1.2,oh+.05),None)
            cut.rotation_euler.z=a;cut.location=(offset[0]+center.x+.1*inward.x,offset[1]+center.y+.1*inward.y,offset[2]+level*3+sill+oh/2-.015)
            cutters.append(cut)
            placements.append(dict(level=level,role=role,zone=b['zone'],rotation_z=a,local_position=[at[0],at[1],level*3]))
            opening_checks.append((wall,Vector((center.x-inward.x,center.y-inward.y,sill+oh/2)),Vector((inward.x,inward.y,0))))
        # Join aperture tools into one Exact Boolean against the welded rounded shell.
        bpy.context.view_layer.update();verts=[];faces=[]
        for ob in cutters:
            base=len(verts);verts.extend(ob.matrix_world@v.co for v in ob.data.vertices)
            faces.extend(tuple(base+i for i in f.vertices) for f in ob.data.polygons)
        tool=mesh(c,'Rounded_Aperture_Tool',verts,faces,None);bpy.context.view_layer.update()
        mod=wall.modifiers.new('Rounded_Apertures','BOOLEAN');enum_set(mod,'operation','DIFFERENCE');enum_set(mod,'solver','EXACT');mod.object=tool;mod.use_self=True
        bpy.context.view_layer.objects.active=wall;bpy.ops.object.modifier_apply(modifier=mod.name)
        for ob in cutters+[tool]: bpy.data.objects.remove(ob,do_unlink=True)
        wall['apertures_applied']=True
        shift(polygon_ring(c,'Rounded_Floor_Band',points,2.84,.16,mats['trim'],-.065,.025),level)
    shift(polygon_slab(c,points,floors*3,mats['trim']))
    for name_part,z,h,key,o,i in [('Parapet',floors*3,.55,'wall',0,.2),('Coping',floors*3+.55,.10,'trim',-.06,.25)]:
        shift(polygon_ring(c,'Rounded_'+name_part,points,z,h,mats[key],o,i))
    checks=[]
    for wall,p,n in opening_checks:
        clear=not wall.ray_cast(p,n,distance=1.7)[0];checks.append(clear)
    if not all(checks): raise ValueError('Rounded facade aperture blocked')
    spec=dict(name=name,shape='CornerShopHouseRounded',building_type='corner_shop_house_rounded',width_bays=width,
        depth_bays=depth,floors=floors,style=style,seed=seed,offset=offset,roof='Flat',shop=True,
        window_density=window_density,corner_radius_m=radius,arc_segments=arc_steps,footprint_polygon=points,
        road_frontages=['south','rounded_corner','east'],facades=placements,apertures_clear=len(checks),
        entrance={'zone':'rounded_corner','flat_operable_door':True},rear_facades=['north','west'])
    spec['entrance_animation']=dict(rig='armature / Door_Hinge',control='open_amount',
        open_angle_degrees=90,frames=[1,24,48,72,96],direction='inward',independent_per_building=True)
    c['spec_json']=json.dumps(spec);return c,spec

def generate_building(parent,name='Building',shape='Rectangle',width=3,depth=3,floors=3,
                      style='Indochine',seed=1,window_density=.8,balcony_chance=.25,
                      roof='Flat',offset=(0,0,0),stepback=False,shop=True,footprint_tiles=None):
    if width<2 or depth<2 or not 1<=floors<=8: raise ValueError('width/depth >=2; floors 1..8')
    if shape=='CornerShopHouseRounded':
        if roof!='Flat' or stepback or footprint_tiles is not None:
            raise ValueError('Rounded corner requires flat roof, no stepback or custom tiles')
        return rounded_corner_building(parent,name,width,depth,floors,style,seed,offset,window_density)
    if shape=='Courtyard' and min(width,depth)<3: raise ValueError('Courtyard needs >=3 x 3 bays')
    if shape=='T' and width%2==0: raise ValueError('T footprint needs odd width for centered stem')
    three_fronts=shape=='CornerShopHouse3Fronts'
    corner_shop=shape in ('CornerShopHouse','CornerShopHouse3Fronts')
    if corner_shop:
        if footprint_tiles is not None: raise ValueError('Corner Shop House requires its rectangular street-facing footprint')
        shop=True
    if roof=='Gable' and (shape not in ('Rectangle','CornerShopHouse','CornerShopHouse3Fronts') or stepback): raise ValueError('Gable requires rectangular footprint without stepback')
    cells=footprint(shape,width,depth) if footprint_tiles is None else {tuple(p) for p in footprint_tiles}
    if not cells or any(len(p)!=2 or any(type(v)!=int for v in p) or not(0<=p[0]<width and 0<=p[1]<depth) for p in cells):
        raise ValueError('Footprint tiles must be integer (x,y) within width/depth bounds')
    reached={next(iter(cells))};pending=list(reached)
    while pending:
        x,y=pending.pop()
        for p in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            if p in cells and p not in reached: reached.add(p);pending.append(p)
    if reached!=cells: raise ValueError('Footprint must be connected')
    if not any(y==0 for x,y in cells): raise ValueError('Footprint needs a bay facing the south road at y=0')
    if footprint_tiles is not None:
        if roof!='Flat': raise ValueError('Custom footprints currently require flat roof')
        shape='Custom'
    rng=random.Random(seed)
    c=collection(PREFIX+name,parent)
    def pos(x,y,z): return (offset[0]+x,offset[1]+y,offset[2]+z)
    boundary=list(edges(cells))
    road_edges=[e for e in boundary if e[1]==0 and e[0][1]==0]
    entrance=road_edges[len(road_edges)//2][0]
    placements=[]
    for level in range(floors):
        active=cells
        if stepback and level==floors-1 and floors>1:
            smaller={p for p in cells if p[1]<depth-1}
            if smaller: active=smaller
            # Roof the exposed terrace; parapet only along its outer boundary.
            for x,y in sorted(cells-active): instance(c,style,'Floor',pos(x*2,y*2,level*3))
            for at,a,n in edges(cells-active):
                if n not in cells:
                    instance(c,style,'Parapet',pos(*at,level*3),a)
        for x,y in sorted(active): instance(c,style,'Floor',pos(x*2,y*2,level*3))
        level_facades=[]
        for at,a,n in edges(active):
            south_road=a==0 and at[1]==0
            east_road=corner_shop and abs(a-math.pi/2)<1e-6 and at[0]==width*2
            west_road=three_fronts and abs(a+math.pi/2)<1e-6 and at[0]==0
            is_road=south_road or east_road or west_road
            corner_bay=corner_shop and ((south_road and at[0]==(width-1)*2) or (east_road and at[1]==0))
            corner_bay=corner_bay or (three_fronts and ((south_road and at[0]==0) or (west_road and at[1]==2)))
            if level==0 and at==entrance and is_road: role='Door'
            elif level==0 and is_road and shop: role='Shopfront'
            elif level>0 and corner_bay: role='Window'
            elif level>0 and is_road and rng.random()<balcony_chance: role='Balcony'
            elif three_fronts and abs(a-math.pi)<1e-6: role='Wall'
            else: role='Window' if rng.random()<window_density else 'Wall'
            ob=instance(c,style,role,pos(*at,level*3),a)
            ob.instance_collection=facade_dressing(style,role)
            if role=='Door':rig_entrance(c,ob,style)
            f={'level':level,'role':role,'local_position':[*at,level*3],'rotation_z':a}
            placements.append(f);level_facades.append(f)
        continuous_storey(c,style,active,level_facades,level,offset)
        if corner_shop:
            instance(c,style,'ShopCornerPost90',pos(width*2,0,level*3))
            if three_fronts:
                instance(c,style,'ShopCornerPost90',pos(0,0,level*3),-math.pi/2)
        if level==floors-1:
            top=3*floors
            for x,y in sorted(active): instance(c,style,'Floor',pos(x*2,y*2,top))
            if roof=='Flat':
                continuous_parapet(c,style,active,pos(0,0,top))
            else:
                # Parametric full gable from reusable 2x2 slopes; span adapts to depth.
                for x in range(width):
                    instance(c,style,'RoofSlope',pos(x*2,0,top),scale=(1,depth/2,depth/2))
                    instance(c,style,'RoofSlope',pos((x+1)*2,depth*2,top),math.pi,scale=(1,depth/2,depth/2))
                    instance(c,style,'Ridge',pos(x*2,depth,top+depth/2))
                # Gables run along Y: local +Y inward, both closing triangles.
                instance(c,style,'Gable',pos(0,depth*2,top),-math.pi/2,(depth/2,1,depth/2))
                instance(c,style,'Gable',pos(width*2,0,top),math.pi/2,(depth/2,1,depth/2))
    spec={'name':name,'shape':shape,'width_bays':width,'depth_bays':depth,'floors':floors,'style':style,
          'seed':seed,'window_density':window_density,'balcony_chance':balcony_chance,'roof':roof,
          'stepback':stepback,'shop':shop,'offset':offset,'cells':sorted(cells),'facades':placements,
          'entrance':{'position':[*entrance,0],'road':'south/-Y','width':1.05}}
    if corner_shop:
        spec['building_type']='corner_shop_house_3_fronts' if three_fronts else 'corner_shop_house'
        spec['road_frontages']=['south/-Y','east/+X']+(['west/-X'] if three_fronts else [])
        spec['shop_corner']={'position':[width*2,0,0],'ground_glazing_on_both_faces':True}
        if three_fronts:
            spec['rear_facade']='north/+Y; blank wall'
            spec['shop_corners']=[[0,0,0],[width*2,0,0]]
    spec['entrance_animation']=dict(rig='armature / Door_Hinge',control='open_amount',
        open_angle_degrees=90,frames=[1,24,48,72,96],direction='inward',independent_per_building=True)
    c['spec_json']=json.dumps(spec)
    return c,spec

def text(c,body,at,size=.45):
    data=bpy.data.curves.new(PREFIX+'Label','FONT'); data.body=body; data.size=size; data.extrude=0
    ob=bpy.data.objects.new(PREFIX+'Label',data);c.objects.link(ob); ob.location=at
    data.materials.append(LABEL_MAT)
    return ob

def build():
    global LIBRARY, LABEL_MAT
    if bpy.data.scenes.get('PBK_Modular_Buildings'):
        raise RuntimeError('PBK review scene already exists. Use panel to generate variants; do not rebuild over it.')
    OUT.mkdir(parents=True,exist_ok=True)
    scene=bpy.data.scenes.new('PBK_Modular_Buildings')
    bpy.context.window.scene=scene
    enum_set(scene.unit_settings,'system','METRIC');scene.unit_settings.scale_length=1
    scene.frame_start=1;scene.frame_end=96;scene.render.fps=24
    review=collection(PREFIX+'Review',scene.collection)
    modules=collection(PREFIX+'Module_Display',review)
    samples=collection(PREFIX+'Layout_Examples',review)
    mats_by_style={}
    for style,colors in PALETTES.items():
        mats={k:material(style+'_'+k,rgba) for k,rgba in zip(('wall','trim','frame','roof'),colors)}
        mats['floor']=material(style+'_concrete',(.34,.37,.36,1))
        mats['glass']=material(style+'_glass',(.07,.18,.23,1),.25)
        mats['metal']=material(style+'_metal',(.10,.12,.13,1),.55)
        mats_by_style[style]=mats
        for role in ('Floor','Wall','Window','Door','Shopfront','Balcony','OuterCorner','InnerCorner',
                     'Column','Stair','Parapet','RoofSlope','Ridge','Gable'):
            register_module(style,role,mats)
    ensure_catalog()
    LABEL_MAT=material('label',(.035,.065,.09,1))
    ground=material('ground',(.63,.70,.70,1))
    box(review,'review_plinth',(24,-5,-.42),(54,95,.4),ground)
    text(review,'MODULAR BUILDING LAB',(0,-51,-.20),1.0)
    text(review,'unified-kit-v3 / 105 assets / 2m bay / 3m storey',(0,-52,-.20),.45)
    display_catalog(modules)
    specs=[]
    examples=[('01_ShopHouse','Rectangle',3,3,3,'Indochine',11,'Flat',(0,8,0),False),
              ('02_Detached','Rectangle',3,3,2,'Brick',22,'Gable',(10,8,0),False),
              ('03_L_Block','L',4,4,3,'Modern',33,'Flat',(20,8,0),False),
              ('04_Courtyard','Courtyard',4,4,2,'Indochine',44,'Flat',(32,8,0),False),
              ('05_Terraced','Rectangle',3,4,4,'Modern',55,'Flat',(44,8,0),True),
              ('06_U_Block','U',4,4,2,'Brick',66,'Flat',(0,21,0),False),
              ('07_T_Block','T',5,4,2,'Modern',77,'Flat',(13,21,0),False),
              ('08_Corner','Corner',3,3,3,'Indochine',88,'Flat',(28,21,0),False),
              ('09_Corner_Shop_House','CornerShopHouse',4,4,3,'Indochine',99,'Flat',(40,21,0),False),
              ('10_Corner_Shop_3_Fronts','CornerShopHouse3Fronts',4,3,3,'Modern',109,'Flat',(40,34,0),False),
              ('11_Corner_Shop_Rounded','CornerShopHouseRounded',4,4,2,'Indochine',119,'Flat',(25,34,0),False)]
    for name,shape,w,d,n,style,seed,roof,at,step in examples:
        c,spec=generate_building(samples,name,shape,w,d,n,style,seed,1.0 if shape=='CornerShopHouseRounded' else .8,.35,roof,at,step)
        specs.append(spec)
        text(samples,name[3:].replace('_',' ').upper(),(at[0],at[1]-1,-.19),.42)
    camera_data=bpy.data.cameras.new(PREFIX+'Camera')
    cam=bpy.data.objects.new(PREFIX+'Camera',camera_data);review.objects.link(cam)
    cam.location=(94,-105,95);target=Vector((24,-5,2))
    cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
    enum_set(camera_data,'type','ORTHO');camera_data.ortho_scale=118;scene.camera=cam
    for name,at,power,size in [('Key',(10,-15,45),5000,30),('Fill',(40,20,30),3500,25)]:
        data=bpy.data.lights.new(PREFIX+name,'AREA');data.energy=power;enum_set(data,'shape','DISK');data.size=size
        ob=bpy.data.objects.new(PREFIX+name,data);review.objects.link(ob);ob.location=at
        ob.rotation_euler=(target-ob.location).to_track_quat('-Z','Y').to_euler()
    sun=bpy.data.lights.new(PREFIX+'Sun','SUN');sun.energy=2;sun.angle=.2
    ob=bpy.data.objects.new(PREFIX+'Sun',sun);review.objects.link(ob);ob.rotation_euler=(.4,-.5,-.3)
    scene.world=bpy.data.worlds.new(PREFIX+'World');scene.world.use_nodes=True
    bg=next(n for n in scene.world.node_tree.nodes if n.type=='BACKGROUND')
    bg.inputs['Color'].default_value=(.55,.65,.75,1);bg.inputs['Strength'].default_value=.45
    scene.render.resolution_x=1800;scene.render.resolution_y=1400;scene.render.resolution_percentage=100
    enum_set(scene.render.image_settings,'file_format','PNG')
    try: scene.render.engine='CYCLES';scene.cycles.samples=24
    except TypeError: pass
    # Source text is self-contained and can be run again to register UI after reopening.
    source=bpy.data.texts.new('PBK_Generator.py')
    source.write(Path(OUT/'generate.py').read_text(encoding='utf8'))
    source['usage']='Run Script to register N-panel; existing scene is not rebuilt.'
    manifest={'schema':'sandtable.modular-building-kit.v1','bay_m':2,'storey_m':3,'wall_m':.2,
        'revision':'balcony-access-continuous-walls-v2',
        'wall_assembly':'single welded mitered shell per storey; shared dressing; continuous floor bands and roof parapets',
        'blender_axes':{'up':'+Z','facade_front':'-Y','width':'+X','inside':'+Y'},
        'gltf_axes':{'up':'+Y','facade_front':'+Z','width':'+X'},
        'provenance':{'authoring':'original procedural mesh; no third-party source assets','reference':'user supplied building grammar diagram'},
        'modules':[], 'examples':specs,'openings':OPENINGS,
        'runtime_status':'not integrated or exported; authoring review kit'}
    for (style,role),c in LIBRARY.items():
        size=list(c['nominal_size'])
        sockets={'origin':[0,0,0]}
        if role in ('Wall','Window','Door','Shopfront','Balcony'):
            sockets.update(left=[0,0,0],right=[2,0,0],top=[0,0,3],bottom=[0,0,0])
        elif role=='Floor': sockets.update(east=[2,0,0],north=[0,2,0],stack=[0,0,3])
        points=[ob.matrix_local @ v.co for ob in c.objects if ob.type=='MESH' for v in ob.data.vertices]
        c['sockets_json']=json.dumps(sockets)
        manifest['modules'].append({'id':f'{style}/{role}','collection':c.name,'nominal_size_m':size,
            'bounds_m':{'min':[round(min(p[i] for p in points),5) for i in range(3)],
                        'max':[round(max(p[i] for p in points),5) for i in range(3)]},
            'sockets':sockets,'meshes':sum(o.type=='MESH' for o in c.objects),'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in c.objects if o.type=='MESH'),
            'collision':'structural substrate; balcony includes overhang; stairs are filled steps'})
    (OUT/'kit_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
    write_catalog_manifest()
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
                area.spaces.active.region_3d.view_distance=70
                area.spaces.active.region_3d.view_location=target
                enum_set(area.spaces.active.shading,'color_type','MATERIAL')
    return scene

def load_library():
    LIBRARY.clear()
    for style in PALETTES:
        for role in ('Floor','Wall','Window','Door','Shopfront','Balcony','OuterCorner','InnerCorner','Column','Stair','Parapet','RoofSlope','Ridge','Gable'):
            c=bpy.data.collections.get(PREFIX+style+'_'+role)
            if c: LIBRARY[(style,role)]=c
    if LIBRARY:ensure_catalog()

def arc_id(role,radius,angle):
    return f'Arc{role}_R{radius:g}_A{math.degrees(angle):g}'

def arc_strip(c,name,radius,angle,z,height,mat,outer=0,inner=.2):
    steps=max(8,round(math.degrees(angle)/1.875));verts=[];faces=[]
    for i in range(steps+1):
        theta=-math.pi/2+angle*i/steps
        for inset in (outer,inner):
            x=(radius-inset)*math.cos(theta);y=radius+(radius-inset)*math.sin(theta)
            verts.extend([(x,y,z),(x,y,z+height)])
    for i in range(steps):
        a=i*4;b=a+4
        faces.extend([(a,b,b+1,a+1),(a+2,a+3,b+3,b+2),(a,a+2,b+2,b),(a+1,b+1,b+3,a+3)])
    a=steps*4;faces.extend([(0,1,3,2),(a,a+2,a+3,a+1)])
    return mesh(c,name,verts,faces,mat)

def bend_dressing(c,style,role,radius,angle):
    import bmesh
    for src in facade_dressing(style,role).objects:
        if src.type!='MESH':continue
        data=src.data.copy();ob=bpy.data.objects.new(PREFIX+'Arc_'+src.name,data);c.objects.link(ob)
        for key in src.keys():ob[key]=src[key]
        for v in data.vertices:v.co=src.matrix_local@v.co
        bm=bmesh.new();bm.from_mesh(data)
        for x in (j/16 for j in range(1,32)):
            bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
                dist=1e-6,plane_co=(x,0,0),plane_no=(1,0,0),clear_inner=False,clear_outer=False)
        bm.to_mesh(data);bm.free()
        for v in data.vertices:
            x,y,z=v.co;theta=-math.pi/2+x*angle/2
            v.co=((radius-y)*math.cos(theta),radius+(radius-y)*math.sin(theta),z)
        data.update();ob['curved_dressing']=True

def apply_cutters(c,wall,cutters):
    bpy.context.view_layer.update();verts=[];faces=[]
    for ob in cutters:
        base=len(verts);verts.extend(ob.matrix_world@v.co for v in ob.data.vertices)
        faces.extend(tuple(base+i for i in p.vertices) for p in ob.data.polygons)
    tool=mesh(c,'Catalog_Cutter',verts,faces,None);bpy.context.view_layer.update()
    mod=wall.modifiers.new('Catalog_Aperture','BOOLEAN');enum_set(mod,'operation','DIFFERENCE');enum_set(mod,'solver','EXACT');mod.object=tool;mod.use_self=True
    bpy.context.view_layer.objects.active=wall;bpy.ops.object.modifier_apply(modifier=mod.name)
    for ob in cutters+[tool]:bpy.data.objects.remove(ob,do_unlink=True)

def catalog_asset(c,style,role,size,**metadata):
    c['module_id']=f'{style}/{role}';c['nominal_size']=size;c['catalog_revision']=CATALOG_REVISION
    c['origin']='bottom-left boundary; tangent +X, inside +Y, up +Z'
    for key,value in metadata.items():c[key]=value
    c.asset_mark();c.asset_data.description=f'{CATALOG_REVISION} / {style} / {role}; metric sockets in kit_manifest.json'
    LIBRARY[(style,role)]=c
    if c.name in bpy.context.scene.collection.children:bpy.context.scene.collection.children.unlink(c)

def ensure_catalog():
    """Build missing canonical assets once; all styles and curved variants share one catalog."""
    for style in PALETTES:
        mats={k:bpy.data.materials[PREFIX+style+'_'+('concrete' if k=='floor' else k)]
              for k in ('wall','trim','frame','floor')}
        for role in BASE_ROLES:
            c=LIBRARY[(style,role)];c['catalog_revision']=CATALOG_REVISION
            c['assembly_mode']='internal_door_source' if role=='Door' else 'full_module'
            c['origin']={'Column':'center of column foot', 'Floor':'lower-left / top of floor at Z=0',
                'Stair':'front-left grid anchor; center line X=1',
                'RoofSlope':'left eave; ridge at Y=2, Z=1',
                'OuterCorner':'structural corner; branches +X / +Y',
                'InnerCorner':'structural reentrant corner; branches +X / +Y'}.get(role,
                'bottom-left boundary; tangent +X, inside +Y, up +Z')
            if role=='Door':
                c.asset_clear();c['replacement_module']=f'{style}/DoorRigged'
        extra=['DoorRigged','FloorBand','Coping','ShopCornerPost90','CornerShopfront90','CornerWindow90']
        extra += [arc_id(role,r,a) for r,a in [(4,math.pi/6),(2,math.pi/4)]
                  for role in ('Wall','Window','Shopfront','Door','Floor','Band','Parapet','Coping')]
        for role in extra:
            name=PREFIX+style+'_'+role;existing=bpy.data.collections.get(name)
            if existing and existing.get('catalog_revision')==CATALOG_REVISION:
                LIBRARY[(style,role)]=existing;continue
            c=collection(name,bpy.context.scene.collection)
            size=[2,.2,3];meta={'assembly_mode':'full_module'}
            if role=='DoorRigged':
                facade=instance(c,style,'Door',(0,0,0));rig_entrance(c,facade,style)
                for ob in LIBRARY[(style,'Door')].objects:
                    if 'substrate' in ob.name or 'floor_band' in ob.name:c.objects.link(ob)
                meta.update(animated=True,hinge_local=[.525,.1,0],control='open_amount',frames=[1,24,48,72,96])
            elif role in ('FloorBand','Coping','ShopCornerPost90'):
                at,sizes,key={'FloorBand':((1,-.02,2.92),(2,.09,.16),'trim'),
                    'Coping':((1,.10,.04),(2,.28,.08),'trim'),
                    'ShopCornerPost90':((-.025,.025,1.42),(.10,.10,2.84),'frame')}[role]
                box(c,role,at,sizes,mats[key]);size=list(sizes)
                meta['assembly_mode']='dressing_only'
            elif role.startswith('Corner'):
                part='Shopfront' if role=='CornerShopfront90' else 'Window'
                wall=box(c,'substrate',(1,.1,1.5),(2,.2,3),mats['wall'])
                east=box(c,'East_Wall',(1.9,1,1.5),(.2,2,3),mats['wall'])
                mod=wall.modifiers.new('Corner_Weld','BOOLEAN');enum_set(mod,'operation','UNION');enum_set(mod,'solver','EXACT');mod.object=east
                bpy.context.view_layer.objects.active=wall;bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(east,do_unlink=True)
                ow,oh,sill=APERTURES[part]
                cuts=[]
                for at,a in [((0,0),0),((2,0),math.pi/2)]:
                    ob=instance(c,style,part,(*at,0),a);ob.instance_collection=facade_dressing(style,part)
                    cut=box(c,'Corner_Cut',(0,0,0),(ow,.6,oh+.025),None);cut.rotation_euler.z=a
                    cut.location=(at[0]+math.cos(a)-.1*math.sin(a),at[1]+math.sin(a)+.1*math.cos(a),sill+oh/2-.0125);cuts.append(cut)
                apply_cutters(c,wall,cuts)
                instance(c,style,'ShopCornerPost90',(2,0,0))
                band=box(c,'floor_band',(1,-.02,2.92),(2,.09,.16),mats['trim'])
                side=box(c,'Side_Band',(2.02,1,2.92),(.09,2,.16),mats['trim'])
                mod=band.modifiers.new('Band_Weld','BOOLEAN');enum_set(mod,'operation','UNION');enum_set(mod,'solver','EXACT');mod.object=side
                bpy.context.view_layer.objects.active=band;bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(side,do_unlink=True)
                size=[2,2,3];meta['turn_degrees']=90
            else:
                bits=role.split('_');part=bits[0][3:];radius=float(bits[1][1:]);angle=math.radians(float(bits[2][1:]))
                end=[radius*math.sin(angle),radius*(1-math.cos(angle)),0]
                meta.update(radius_m=radius,angle_degrees=math.degrees(angle),curve_end=end,
                    turn_degrees=math.degrees(angle),curve_family=part)
                size=[end[0],end[1]+.2,3]
                if part=='Floor':
                    pts=[(0,radius)]+[(radius*math.cos(-math.pi/2+angle*i/24),radius+radius*math.sin(-math.pi/2+angle*i/24)) for i in range(25)]
                    polygon_slab(c,pts,0,mats['floor']);size=[end[0],radius,.2]
                elif part in ('Band','Parapet','Coping'):
                    z,h,key,o,i={'Band':(2.84,.16,'trim',-.065,.025),
                        'Parapet':(0,.55,'wall',0,.2),'Coping':(.55,.1,'trim',-.06,.25)}[part]
                    arc_strip(c,part,radius,angle,z,h,mats[key],o,i);size=[end[0],end[1]+.25,z+h]
                else:
                    wall=arc_strip(c,'substrate',radius,angle,0,3,mats['wall'])
                    if part!='Wall':
                        ow,oh,sill=APERTURES[part]
                        theta=-math.pi/2+angle/2;a=angle/2;center=Vector((radius*math.cos(theta),radius+radius*math.sin(theta)))
                        cut=box(c,'Arc_Cut',(0,0,0),(ow*(1 if part=='Door' else radius*angle/2),1.2,oh+.05),None)
                        cut.rotation_euler.z=a;cut.location=(center.x-.1*math.sin(a),center.y+.1*math.cos(a),sill+oh/2-.015)
                        apply_cutters(c,wall,[cut])
                        if part=='Door':
                            ob=instance(c,style,'Door',(center.x-math.cos(a),center.y-math.sin(a),0),a);rig_entrance(c,ob,style)
                            meta['animated']=True
                        else:bend_dressing(c,style,part,radius,angle)
                    arc_strip(c,'floor_band',radius,angle,2.84,.16,mats['trim'],-.065,.025)
            catalog_asset(c,style,role,size,**meta)

def catalog_points(c,matrix=None):
    from mathutils import Matrix
    matrix=matrix or Matrix.Identity(4);points=[]
    def local(ob):return local(ob.parent)@ob.matrix_local if ob.parent and ob.parent.name in c.objects else ob.matrix_local
    for ob in c.objects:
        transform=matrix@local(ob)
        if ob.type=='MESH':points.extend(transform@v.co for v in ob.data.vertices)
        elif ob.type=='EMPTY' and ob.instance_collection:points.extend(catalog_points(ob.instance_collection,transform))
    return points

def write_catalog_manifest():
    manifest=json.loads((OUT/'kit_manifest.json').read_text(encoding='utf8'))
    entries=[]
    for (style,role),c in sorted(LIBRARY.items()):
        points=catalog_points(c);size=list(c['nominal_size'])
        end=list(c.get('curve_end',[2,0,0]));angle=math.radians(c.get('turn_degrees',0))
        sockets={'origin':[0,0,0]}
        if role in ('Wall','Window','Door','DoorRigged','Shopfront','Balcony','FloorBand','Coping') or role.startswith('Arc'):
            sockets.update(left=[0,0,0],right=end,top=[0,0,3],bottom=[0,0,0])
        if role=='Floor':sockets.update(east=[2,0,0],north=[0,2,0],stack=[0,0,3])
        if role=='Floor':sockets.update(top=[0,0,0],bottom=[0,0,-.2])
        if role.startswith('ArcFloor'):sockets.update(radial_center=[0,c['radius_m'],0],stack=[0,0,3])
        if role.startswith('Arc'):
            part=c['curve_family']
            bottom,top={'Floor':(-.2,0),'Parapet':(0,.55),'Coping':(.55,.65),'Band':(2.84,3)}.get(part,(0,3))
            sockets.update(bottom=[0,0,bottom],top=[0,0,top])
        if role=='FloorBand':sockets.update(bottom=[0,0,2.84],top=[0,0,3])
        if role=='Coping':sockets.update(bottom=[0,0,0],top=[0,0,.08])
        if role in ('Parapet','Ridge'):sockets.update(left=[0,0,0],right=[2,0,0],top=[0,0,size[2]])
        if role=='Ridge':sockets.update(bottom=[0,0,-.01],top=[0,0,.09])
        if role=='RoofSlope':sockets.update(eave=[1,0,0],ridge=[1,2,1],left=[0,0,0],right=[2,0,0])
        if role=='Gable':sockets.update(left=[0,0,0],right=[4,0,0],peak=[2,0,1])
        if role=='Column':sockets.update(base=[0,0,0],top=[0,0,3])
        if role=='Stair':sockets.update(entry=[1,0,0],exit=[1,3.75,3])
        if role in ('OuterCorner','InnerCorner'):sockets.update(x_end=[2,0,0],y_end=[0,2,0],top=[0,0,3])
        if role=='ShopCornerPost90':sockets.update(base=[0,0,0],top=[0,0,2.84])
        if role.startswith('Corner'):sockets.update(left=[0,0,0],right=[2,2,0],top=[0,0,3])
        if role in ('Door','DoorRigged'):sockets['hinge']=[.525,.1,0]
        c['sockets_json']=json.dumps(sockets)
        entries.append(dict(id=f'{style}/{role}',collection=c.name,revision=CATALOG_REVISION,
            asset=bool(c.asset_data),assembly_mode=c.get('assembly_mode','full_module'),nominal_size_m=size,
            bounds_m={'min':[round(min(p[i] for p in points),6) for i in range(3)],
                'max':[round(max(p[i] for p in points),6) for i in range(3)]},sockets=sockets,
            connection_orientation={'left_tangent_degrees':0,'right_tangent_degrees':math.degrees(angle)},
            radius_m=c.get('radius_m'),angle_degrees=c.get('angle_degrees'),
            meshes=sum(o.type=='MESH' for o in c.objects),triangles=sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in c.objects if o.type=='MESH'),
            animated=bool(c.get('animated',False)),replacement_module=c.get('replacement_module'),
            generator_usage='internal leaf/frame source' if role=='Door' else 'canonical assembly; facade dressing excludes substrate and floor_band',
            collision='wall substrate and floor solids; doors use animated leaf collision',
            style=style,family=c.get('curve_family',role),origin=c['origin'],bounds_mode='bind_pose'))
    manifest.update(schema='sandtable.modular-building-kit.v2',revision=CATALOG_REVISION,modules=entries,
        aperture_dimensions_m={k:dict(width=v[0],height=v[1],sill=v[2]) for k,v in APERTURES.items()},
        active_assets=sum(e['asset'] for e in entries),internal_sources=sum(not e['asset'] for e in entries),
        composition_rules={'building_shell':'one welded shell per storey; use dressing derived from canonical assets',
            'animated_door':'DoorRigged leaf geometry shared; rig, driver and Action independent per generated building',
            'curves':'R4/A30 and R2/A45; connect curve_end with matching tangent, radius and angle',
            'floor_thickness_m':.2})
    (OUT/'kit_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
    return manifest

def display_catalog(modules):
    roles=[r for s,r in LIBRARY if s=='Indochine' and r!='Door']
    for style_index,style in enumerate(PALETTES):
        for i,role in enumerate(roles):
            row=style_index*3+i//12;x=3+(i%12)*3.3;y=-48+row*5.5
            if i%12==0:text(modules,style.upper(),(0,y-1,-.19),.30)
            instance(modules,style,role,(x,y,0))
            text(modules,role,(x,y-.65,-.19),.15)

class PBK_Settings(bpy.types.PropertyGroup):
    shape: bpy.props.EnumProperty(items=[(s,{'CornerShopHouse':'Corner Shop House',
        'CornerShopHouse3Fronts':'Corner Shop House - 3 Fronts',
        'CornerShopHouseRounded':'Corner Shop House - Rounded Corner'}.get(s,s),'')
        for s in ('Rectangle','L','U','T','Courtyard','Corner','CornerShopHouse','CornerShopHouse3Fronts','CornerShopHouseRounded')],default='Rectangle')
    style: bpy.props.EnumProperty(items=[(s,s,'') for s in PALETTES],default='Indochine')
    roof: bpy.props.EnumProperty(items=[('Flat','Flat / terrace',''),('Gable','Gable (rectangle only)','')])
    width: bpy.props.IntProperty(name='Width (bays)',default=3,min=2,max=12)
    depth: bpy.props.IntProperty(name='Depth (bays)',default=3,min=2,max=12)
    floors: bpy.props.IntProperty(name='Floors',default=3,min=1,max=8)
    seed: bpy.props.IntProperty(name='Seed',default=101,min=0)
    windows: bpy.props.FloatProperty(name='Window density',default=.8,min=0,max=1)
    balconies: bpy.props.FloatProperty(name='Balcony chance',default=.3,min=0,max=1)
    stepback: bpy.props.BoolProperty(name='Top floor stepback',default=False)
    shop: bpy.props.BoolProperty(name='Ground floor shops',default=True)

class PBK_OT_generate(bpy.types.Operator):
    bl_idname='pbk.generate';bl_label='Generate at 3D Cursor';bl_options={'REGISTER','UNDO'}
    def execute(self,ctx):
        load_library();p=ctx.scene.pbk_settings
        if not LIBRARY:
            self.report({'ERROR'},'Open modular_building_kit.blend first');return {'CANCELLED'}
        try:
            c,s=generate_building(ctx.scene.collection,'User_Building',p.shape,p.width,p.depth,p.floors,
                p.style,p.seed,p.windows,p.balconies,p.roof,tuple(ctx.scene.cursor.location),p.stepback,p.shop)
        except ValueError as e:
            self.report({'ERROR'},str(e));return {'CANCELLED'}
        self.report({'INFO'},f'Generated {len(c.objects)} linked module instances')
        return {'FINISHED'}

class PBK_PT_panel(bpy.types.Panel):
    bl_label='Procedural Building Kit';bl_idname='PBK_PT_panel';bl_space_type='VIEW_3D';bl_region_type='UI';bl_category='Building Kit'
    def draw(self,ctx):
        layout=self.layout;p=ctx.scene.pbk_settings
        layout.label(text='Bay 2m / Storey 3m / Wall 0.2m')
        for prop in ('shape','style','width','depth','floors','seed','windows','balconies','roof','stepback','shop'): layout.prop(p,prop)
        layout.operator('pbk.generate')
        layout.label(text='Place 3D cursor outside sample board')
        active=ctx.active_object
        rig=active if active and active.type=='ARMATURE' and active.get('door_rig') else bpy.data.objects.get(active.get('door_rig','')) if active else None
        if rig:
            box_ui=layout.box();box_ui.label(text='Entrance Door / 0 closed - 1 open')
            box_ui.prop(rig,'["open_amount"]',text='Open',slider=True)
            box_ui.label(text='Play frames 1-96: open / hold / close')

def register():
    for cls in (PBK_PT_panel,PBK_OT_generate,PBK_Settings):
        old=getattr(bpy.types,cls.__name__,None)
        if old:
            if cls==PBK_Settings and hasattr(bpy.types.Scene,'pbk_settings'): del bpy.types.Scene.pbk_settings
            bpy.utils.unregister_class(old)
    for cls in (PBK_Settings,PBK_OT_generate,PBK_PT_panel): bpy.utils.register_class(cls)
    bpy.types.Scene.pbk_settings=bpy.props.PointerProperty(type=PBK_Settings)

if __name__=='__main__':
    if not bpy.data.scenes.get('PBK_Modular_Buildings'): build()
    else: load_library()
    register()
    print('PBK: unified-kit-v3 / 105 active assets; N > Building Kit')

# Retro variant: apply to new continuous shell geometry.
"""Create a separate, reusable retro edition from the existing authoring kit.
Run in a disposable Blender process; never overwrites the original kit.
"""
from pathlib import Path
import json
import math
import shutil
import sys
import bpy
import bmesh
import numpy as np
from mathutils import Vector

BASE = Path('D:\\projects\\njin\\src\\games\\sandtable\\assets\\models\\procedural_building')
RETRO_ROOT = BASE / 'retro'
REVISION = 'unified-kit-v3-retro-v18-low-poly-balcony'
STAT = {'union_operations': 0, 'rounded_meshes': 0, 'protected_edges': 0, 'errors': []}


def make_grain():
    """Deterministic periodic height field; real textures survive glTF export."""
    rng = np.random.default_rng(41023)
    n = 256
    field = rng.normal(size=(n, n))
    # Periodic low-pass: no edge discontinuity in the tile.
    freq = np.fft.fftfreq(n)
    radius = freq[:, None] ** 2 + freq[None, :] ** 2
    height = np.fft.ifft2(np.fft.fft2(field) * np.exp(-radius / .065)).real
    height /= height.std()
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .055
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .055
    normal = np.stack([-dx, -dy, np.ones_like(dx)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    rgba = np.ones((n, n, 4), np.float32)
    rgba[:, :, :3] = normal * .5 + .5
    orm = np.ones_like(rgba)
    orm[:, :, 1] = np.clip(.90 + height * .025, .82, .98)
    orm[:, :, 2] = 0
    images = []
    for name, pixels in [('Retro_Grain_Normal', rgba), ('Retro_Grain_ORM', orm)]:
        image = bpy.data.images.get(name) or bpy.data.images.new(name, n, n, alpha=True)
        image.colorspace_settings.name = 'Non-Color'
        image.pixels.foreach_set(pixels.ravel())
        image.filepath_raw = str(RETRO_ROOT / 'textures' / (name + '.png'))
        image.file_format = 'PNG'
        image.save()
        image.pack()
        images.append(image)
    return images


def style_materials(images):
    palette = {}
    for mat in bpy.data.materials:
        if not mat.name.startswith('PBK_') or mat.name in {'PBK_label', 'PBK_ground'}:
            continue
        shader = next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
        if not shader:
            continue
        original = shader.inputs['Base Color'].default_value
        # Pigmented retro keeps the three original style families readable.
        color = tuple(float(c) * .78 + .72 * .22 for c in original[:3]) + (1,)
        shader.inputs['Base Color'].default_value = color
        mat.diffuse_color = color
        shader.inputs['Metallic'].default_value = 0
        shader.inputs['Roughness'].default_value = .9
        shader.inputs['Specular IOR Level'].default_value = .22
        if 'Transmission Weight' in shader.inputs:
            shader.inputs['Transmission Weight'].default_value = 0
        nodes, links = mat.node_tree.nodes, mat.node_tree.links
        tex = nodes.new('ShaderNodeTexImage');tex.image = images[0];tex.name = 'Retro Grain Normal'
        normal = nodes.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value = 1
        links.new(tex.outputs['Color'], normal.inputs['Color'])
        links.new(normal.outputs['Normal'], shader.inputs['Normal'])
        orm = nodes.new('ShaderNodeTexImage');orm.image = images[1]
        separate = nodes.new('ShaderNodeSeparateRGB')
        links.new(orm.outputs['Color'], separate.inputs['Image'])
        links.new(separate.outputs['G'], shader.inputs['Roughness'])
        mat['visual_variant'] = 'retro';mat['grain_uv_tile_m'] = .25
        palette[mat.name] = list(color)
    return palette


def overlap(a, b):
    amin = [min(p[i] for p in a.bound_box) for i in range(3)]
    amax = [max(p[i] for p in a.bound_box) for i in range(3)]
    bpoints = [a.matrix_world.inverted() @ b.matrix_world @ Vector(p) for p in b.bound_box]
    return all(min(amax[i], max(p[i] for p in bpoints)) >= max(amin[i], min(p[i] for p in bpoints)) - 1e-5 for i in range(3))


def fuse_contacts():
    """Exact unions of touching static pieces of the same pigment, then fillet.
    Never fuse a moving leaf, glass inset, or independent module to its neighbor.
    """
    names = [c.name for c in bpy.data.collections if c.asset_data or
             (c.name.startswith('PBK_') and c.name.endswith('_Door'))]
    for name in names:
        c = bpy.data.collections[name]
        parts = [o.name for o in c.objects if o.type == 'MESH' and not o.get('door_rig')
                 and len(o.data.materials) == 1 and o.data.materials[0]
                 and not o.data.materials[0].name.endswith('_glass')]
        for i, first in enumerate(parts):
            a = bpy.data.objects.get(first)
            if not a:
                continue
            for second in parts[i + 1:]:
                b = bpy.data.objects.get(second)
                if not b or a.data.materials[0] != b.data.materials[0] or not overlap(a, b):
                    continue
                # Bodies in canonical collections are meshes in the same local frame.
                if a.parent or b.parent:
                    continue
                bpy.context.view_layer.objects.active = a
                a.select_set(True)
                if a.data.users > 1:
                    a.data = a.data.copy()
                mod = a.modifiers.new('Retro_Contact_Union', 'BOOLEAN')
                mod.operation = 'UNION';mod.solver = 'EXACT';mod.object = b
                bpy.ops.object.modifier_apply(modifier=mod.name)
                if not a.data.polygons:
                    raise RuntimeError('Empty contact union: ' + first + '/' + second)
                bpy.data.objects.remove(b, do_unlink=True)
                bpy.context.view_layer.update()
                STAT['union_operations'] += 1
        print('RETRO_CONTACTS', name, STAT['union_operations'], flush=True)


def protected_planes():
    """Cross-module cut planes are planar; other exposed corners may round."""
    result = {}
    for c in bpy.data.collections:
        if not c.asset_data:
            continue
        sockets = json.loads(c.get('sockets_json', '{}'))
        planes = []
        if 'left' in sockets and 'right' in sockets:
            angle = math.radians(c.get('turn_degrees', 0))
            planes += [(Vector((0, 0, 0)), Vector((1, 0, 0))),
                       (Vector(sockets['right']), Vector((math.cos(angle), math.sin(angle), 0)))]
        if c.name.endswith('_Floor'):
            planes += [(Vector((0, 0, 0)), Vector((0, 1, 0))), (Vector((0, 2, 0)), Vector((0, 1, 0)))]
            planes += [(Vector((0, 0, 0)), Vector((1, 0, 0))), (Vector((2, 0, 0)), Vector((1, 0, 0)))]
        for ob in c.objects:
            if ob.type == 'MESH':
                result.setdefault(ob.data.name, []).extend(planes)
    return result


def authoring_round_mesh(ob, planes):
    data = ob.data
    hinge = ob.vertex_groups.get('Door_Hinge')
    if hinge:
        for group in list(ob.vertex_groups):
            if group.name != 'Door_Hinge':
                ob.vertex_groups.remove(group)
        hinge.add(list(range(len(data.vertices))), 1, 'REPLACE')
    if data.get('retro_processed') or not data.polygons:
        return
    bm = bmesh.new();bm.from_mesh(data);bm.normal_update()
    extents = [max(v.co[i] for v in bm.verts) - min(v.co[i] for v in bm.verts) for i in range(3)]
    width = min(.065, max(.005, min(extents) * .28))
    if any('metal' in m.name for m in data.materials if m):
        width = min(width, .018)
    edges = []
    door_matrices = [o.matrix_world.inverted() for o in bpy.data.objects
                     if o.type == 'ARMATURE' and o.get('door_rig')]
    structural = 'substrate' in ob.name or 'Continuous_Wall' in ob.name or ob.get('curved_dressing')
    for edge in bm.edges:
        if len(edge.link_faces) != 2 or edge.calc_face_angle(0) < math.radians(25):
            continue
        ends = [ob.matrix_world @ v.co for v in edge.verts]
        # Complex storey Boolean shells already form one seamless wall; rounding
        # their densely cut polygons can fold faces and close doorway corners.
        if 'Continuous_Wall' in ob.name or ob.get('curved_dressing'):
            continue
        if structural:
            blocked = False
            for inverse in door_matrices:
                local = [inverse @ p for p in ends]
                if all(max(p[i] for p in local) >= lo and min(p[i] for p in local) <= hi
                       for i, (lo, hi) in enumerate(((.38, 1.62), (-.16, .36), (-.09, 2.51)))):
                    blocked = True;break
            if blocked:
                continue
        if any(all(abs((p - origin).dot(normal)) < .0001 for p in ends) for origin, normal in planes):
            STAT['protected_edges'] += 1
            continue
        edges.append(edge)
    if edges:
        bmesh.ops.bevel(bm, geom=edges, offset=width, segments=5, profile=.5, affect='EDGES',
                        clamp_overlap=True, loop_slide=True, material=-1, harden_normals=True,
                        miter_outer='ARC', miter_inner='ARC')
    bm.normal_update()
    # Keep broad faces flat and use smooth shading on the rounded fillet strips.
    for face in bm.faces:
        face.smooth = structural or face.calc_area() < width * 3
    if structural:
        for edge in bm.edges:
            edge.smooth = len(edge.link_faces) == 2 and edge.calc_face_angle(0) < math.radians(35)
    bmesh.ops.triangulate(bm, faces=list(bm.faces), quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(data);bm.free();data.update()
    uv = data.uv_layers.get('RetroUV') or data.uv_layers.new(name='RetroUV')
    data.uv_layers.active = uv
    for face in data.polygons:
        dominant = max(range(3), key=lambda i: abs(face.normal[i]))
        axes = ((1, 2), (0, 2), (0, 1))[dominant]
        for loop in face.loop_indices:
            co = data.vertices[data.loops[loop].vertex_index].co
            uv.data[loop].uv = (co[axes[0]] * 4, co[axes[1]] * 4)
    data['retro_processed'] = True;data['retro_bevel_m'] = width
    if hinge:
        hinge.add(list(range(len(data.vertices))), 1, 'REPLACE')
    STAT['rounded_meshes'] += 1


def style_collection(c):
    for ob in list(c.all_objects):
        if ob.type == 'MESH' and not ob.name.startswith('PBK_Label'):
            authoring_round_mesh(ob, [])


def render_views(scene):
    path = BASE / 'render_retro.py'
    exec(compile(path.read_text(encoding='utf8'), str(path), 'exec'),
         {'__name__': 'retro_review', '__file__': str(path)})


OUT = RETRO_ROOT
CATALOG_REVISION = REVISION
_original_generate_building = generate_building
def generate_building(*args, **kwargs):
    result = _original_generate_building(*args, **kwargs)
    style_collection(result[0])
    return result

# Continuous frame revision
"""Continuous rounded frames: one solid mesh, no four-piece corner joints."""
import math
import bpy,bmesh


def rounded_loop(left,right,bottom,top,radius,segments=10):
    result=[]
    for cx,cz,start in ((right-radius,bottom+radius,-math.pi/2),
                        (right-radius,top-radius,0),
                        (left+radius,top-radius,math.pi/2),
                        (left+radius,bottom+radius,math.pi)):
        for i in range(segments+1):
            angle=start+i*math.pi/(2*segments)
            result.append((cx+radius*math.cos(angle),cz+radius*math.sin(angle)))
    return result


def continuous_frame(collection,style,role,width,height,sill,curvature=None):
    closed=role=='Window'
    left,right=1-width/2-.09,1+width/2+.09
    inner_left,inner_right=1-width/2+.05,1+width/2-.05
    bottom,top=sill-.07,sill+height+.07
    inner_bottom,inner_top=sill+.01,sill+height-.03
    front,back=-.145,.035
    vertices=[];faces=[]
    if closed:
        outer=rounded_loop(left,right,bottom,top,.105)
        inner=rounded_loop(inner_left,inner_right,inner_bottom,inner_top,.035)
        n=len(outer)
        for y in (front,back):
            vertices.extend((x,y,z) for x,z in outer+inner)
        for i in range(n):
            j=(i+1)%n
            faces += [(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),
                      (i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)]
    else:
        outline=[(left,0)]
        for cx,cz,a0,a1 in ((left+.105,top-.105,math.pi,math.pi/2),
                           (right-.105,top-.105,math.pi/2,0)):
            for i in range(11):
                a=a0+(a1-a0)*i/10;outline.append((cx+.105*math.cos(a),cz+.105*math.sin(a)))
        outline.extend([(right,0),(inner_right,0)])
        for cx,cz,a0,a1 in ((inner_right-.035,inner_top-.035,0,math.pi/2),
                           (inner_left+.035,inner_top-.035,math.pi/2,math.pi)):
            for i in range(11):
                a=a0+(a1-a0)*i/10;outline.append((cx+.035*math.cos(a),cz+.035*math.sin(a)))
        outline.append((inner_left,0));n=len(outline)
        for y in (front,back):vertices.extend((x,y,z) for x,z in outline)
        faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]
        faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    data=bpy.data.meshes.new('PBK_Continuous_Frame_'+style+'_'+role)
    data.from_pydata(vertices,[],faces);data.materials.append(bpy.data.materials['PBK_'+style+'_trim'])
    ob=bpy.data.objects.new(data.name,data);collection.objects.link(ob)
    bm=bmesh.new();bm.from_mesh(data);bm.normal_update()
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    edges=[e for e in bm.edges if len(e.link_faces)==2 and e.calc_face_angle(0)>.5
           and not all(abs(v.co.z)<1e-6 for v in e.verts)]
    bmesh.ops.bevel(bm,geom=edges,offset=.012,segments=4,profile=.5,affect='EDGES',
                    clamp_overlap=True,loop_slide=True,material=-1,miter_inner='SHARP',miter_outer='SHARP')
    # Subdivide before bending: the top/bottom border follows the facade arc.
    if curvature:
        for x in (j/24 for j in range(1,48)):
            bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
                dist=1e-6,plane_co=(x,0,0),plane_no=(1,0,0),clear_inner=False,clear_outer=False)
        radius,angle=curvature
        for v in bm.verts:
            x,y,z=v.co;theta=-math.pi/2+x*angle/2
            v.co=((radius-y)*math.cos(theta),radius+(radius-y)*math.sin(theta),z)
    bm.normal_update()
    for f in bm.faces:f.smooth=True
    for e in bm.edges:e.smooth=len(e.link_faces)==2 and e.calc_face_angle(0)<math.radians(40)
    bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
    bm.to_mesh(data);bm.free();data.update()
    uv=data.uv_layers.new(name='RetroUV')
    for face in data.polygons:
        axis=max(range(3),key=lambda i:abs(face.normal[i]));axes=((1,2),(0,2),(0,1))[axis]
        for loop in face.loop_indices:
            p=data.vertices[data.loops[loop].vertex_index].co;uv.data[loop].uv=(p[axes[0]]*4,p[axes[1]]*4)
    data['retro_processed']=True
    ob['continuous_frame']=True;ob['frame_role']=role;ob['glass_grid']=False
    ob['frame_clear_width_m']=width-.10;ob['frame_closed_ring']=closed
    return ob

_original_opening_wall = opening_wall
def opening_wall(c, style, role, mats, width, height, sill):
    before=set(o.name for o in c.objects)
    wall=_original_opening_wall(c, style, role, mats, width, height, sill)
    for ob in list(c.objects):
        if ob.name not in before and any(key in ob.name for key in ('jamb','lintel_sill','mullion','transom')):
            bpy.data.objects.remove(ob,do_unlink=True)
    continuous_frame(c,style,role,width,height,sill)
    return wall

# PBR accent revision
"""Glass, wood and metal accents for the rounded retro kit. No export."""
import bpy

def apply_accent_materials():
    for ob in bpy.data.objects:
        if ob.type != 'MESH':
            continue
        style = next((s for s in ('Indochine','Modern','Brick')
                      if any(m and m.name.startswith('PBK_'+s+'_') for m in ob.data.materials)), None)
        if not style:
            continue
        wood = bpy.data.materials.get('PBK_'+style+'_wood')
        glass = bpy.data.materials.get('PBK_'+style+'_glass')
        if ob.get('continuous_frame') and wood:
            ob.data.materials[0] = wood
        elif ob.get('door_rig') or 'door_leaf' in ob.name:
            # Moving panel becomes glass; handle faces retain their metal slot.
            for i, mat in enumerate(ob.data.materials):
                if mat and mat.name == 'PBK_'+style+'_frame':
                    ob.data.materials[i] = glass


_accent_generate_building = generate_building
def generate_building(*args, **kwargs):
    result = _accent_generate_building(*args, **kwargs)
    apply_accent_materials()
    return result
_accent_continuous_frame = continuous_frame
def continuous_frame(*args, **kwargs):
    result = _accent_continuous_frame(*args, **kwargs)
    apply_accent_materials()
    return result

# Working shutter revision
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
        outer=rounded_loop(lo,hi,bottom,top,.031,2)
        inner=rounded_loop(lo+border,hi-border,bottom+border,top-border,.02,2);n=len(outer)
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
                        offset=0.,segments=1,affect='EDGES',clamp_overlap=True)
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

_shutter_load_library = load_library
def load_library():
    result = _shutter_load_library()
    ensure_window_shutters()
    return result
_shutter_generate_building = generate_building
def generate_building(*args, **kwargs):
    ensure_window_shutters()
    return _shutter_generate_building(*args, **kwargs)

# Window glazing revision
"""Opaque glazing on closed windows; wooden shutter windows have no glass."""
import bpy

def apply_window_glazing():
    names=('PBK_Indochine_Window','PBK_Indochine_ArcWindow_R2_A45','PBK_Indochine_ArcWindow_R4_A30')
    restored=[]
    def is_pane(ob):
        return ob.type=='MESH' and ob.data.materials and all(m and m.name.endswith('_glass') for m in ob.data.materials)
    for name in names:
        collection=bpy.data.collections.get(name)
        if not collection:continue
        collection['glazing']='open aperture behind wooden shutters; no glass'
        for ob in list(collection.objects):
            if is_pane(ob):
                restored.append(ob.name)
                bpy.data.objects.remove(ob,do_unlink=True)
    for style in ('Indochine','Modern','Brick'):
        mat=bpy.data.materials.get('PBK_'+style+'_glass')
        if not mat:continue
        bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        bs.inputs['Base Color'].default_value=(.095,.18,.24,1.)
        bs.inputs['Roughness'].default_value=.35
        bs.inputs['Transmission Weight'].default_value=0. # Alpha handles transparency without double attenuation.
        bs.inputs['Alpha'].default_value=1.
        bs.inputs['IOR'].default_value=1.45
        mat.diffuse_color=(.095,.18,.24,1.)
        if hasattr(mat,'surface_render_method'):mat.surface_render_method='BLENDED'
        mat['opacity']=1.;mat['transparency']=0.
        mat['surface_role']='glass';mat['glazing']='sealed clear pane'
        for prop in ('use_raytrace_refraction','use_screen_refraction'):
            if hasattr(mat,prop):setattr(mat,prop,True)
    apply_shopfront_base()
    return restored


def apply_shopfront_base():
    """Split each shop pane vertically: 80% glazing above a 20% frame-material base."""
    for style in ('Indochine','Modern','Brick'):
        for role in ('Shopfront','ArcShopfront_R2_A45','ArcShopfront_R4_A30'):
            c=bpy.data.collections.get('PBK_'+style+'_'+role)
            if not c:continue
            for ob in list(c.objects):
                if ob.type!='MESH' or ob.get('shopfront_fill_ratio') or not ob.data.materials:continue
                if not all(m and m.name.endswith('_glass') for m in ob.data.materials):continue
                lo=min(v.co.z for v in ob.data.vertices);hi=max(v.co.z for v in ob.data.vertices)
                span=hi-lo
                base=ob.copy();base.data=ob.data.copy();base.name=ob.name+'_Frame_Base20'
                base.data.materials.clear();base.data.materials.append(bpy.data.materials['PBK_'+style+'_frame'])
                for collection in list(ob.users_collection):collection.objects.link(base)
                ob.data=ob.data.copy()
                for v in base.data.vertices:v.co.z=lo+(v.co.z-lo)*.2
                for v in ob.data.vertices:v.co.z=lo+span*.2+(v.co.z-lo)*.8
                base['shopfront_base_ratio']=.2;ob['shopfront_fill_ratio']=.8
                base['shopfront_original_z']=[lo,hi];ob['shopfront_original_z']=[lo,hi]
                base.data.update();ob.data.update()
            c['shopfront_glass_fill']=.8;c['shopfront_frame_base']=.2

_glazing_load_library = load_library
def load_library():
    result = _glazing_load_library()
    apply_window_glazing()
    return result
_glazing_generate_building = generate_building
def generate_building(*args, **kwargs):
    apply_window_glazing()
    result = _glazing_generate_building(*args, **kwargs)
    apply_window_glazing()
    return result

# City low-poly revision
"""Reusable, idempotent city-style mesh/material pass. Never exports models."""
import math
import bpy

PROFILE = 'city-low-poly-v2'

def triangles(data):
    return sum(len(p.vertices)-2 for p in data.polygons)

def limits(data):
    return [(min(v.co[i] for v in data.vertices), max(v.co[i] for v in data.vertices)) for i in range(3)]

def simplify(ob):
    data=ob.data
    if not data.vertices or data.get('city_low_poly') == PROFILE or data.get('retro_stair_profile'):return None
    before=triangles(data);old=limits(data)
    curved=ob.get('curved_dressing') or any('_Arc' in c.name for c in ob.users_collection)
    simple_box=(not curved and not ob.vertex_groups and
                any(k in ob.name.lower() for k in ('glass','floor_band','signboard','awning_stripe','awning_wall_mount','awning_front_rail','awning_anchor','awning_support','frame_base20')))
    if simple_box:
        new=bpy.data.meshes.new(data.name+'_City_Box')
        new.from_pydata([(old[0][x],old[1][y],old[2][z]) for x,y,z in
                        [(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)]],[],
                       [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
        for mat in data.materials:new.materials.append(mat)
        for key in data.keys():new[key]=data[key]
        data.user_remap(new);new['city_low_poly']=PROFILE;new['city_before_triangles']=before
        return {'mesh':data.name,'before':before,'after':12}
    # Work on a linked disposable object, keeping the original object's rig,
    # transforms, collection memberships and custom properties intact.
    temp=bpy.data.objects.new('City_Simplify',data.copy())
    bpy.context.scene.collection.objects.link(temp)
    for group in ob.vertex_groups:temp.vertex_groups.new(name=group.name)
    bpy.context.view_layer.objects.active=temp;temp.select_set(True)
    mod=temp.modifiers.new('Remove_Coplanar','DECIMATE');mod.decimate_type='DISSOLVE'
    mod.angle_limit=math.radians(2);mod.delimit={'MATERIAL'}
    bpy.ops.object.modifier_apply(modifier=mod.name)
    structural=any(k in ob.name.lower() for k in ('substrate','continuous_wall','slab','floor_band'))
    n=triangles(temp.data)
    if not structural and not ob.get('shutter_leaf') and not ob.get('city_sign') and n>96:
        mod=temp.modifiers.new('City_Silhouette','DECIMATE')
        mod.ratio=max(48/n,.22);mod.use_collapse_triangulate=True
        bpy.ops.object.modifier_apply(modifier=mod.name)
    new=limits(temp.data)
    # Exact extents retain snap boundaries and rigid leaf fit. Door/shutter
    # armature transforms and vertex groups stay in their original local frame.
    for v in temp.data.vertices:
        for i,((lo,hi),(a,b)) in enumerate(zip(old,new)):
            if not ob.get('shutter_leaf') and b-a>1e-8:v.co[i]=lo+(v.co[i]-a)*(hi-lo)/(b-a)
    for p in temp.data.polygons:p.use_smooth=False
    temp.data.update()
    # Replace all shared users consistently, including dressing collections.
    data.user_remap(temp.data)
    temp.data['city_low_poly']=PROFILE
    temp.data['city_before_triangles']=before
    result={'mesh':data.name,'before':before,'after':triangles(temp.data)}
    bpy.data.objects.remove(temp,do_unlink=True)
    return result

def materials():
    count=0
    for mat in bpy.data.materials:
        if not mat.use_nodes:continue
        bs=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
        if not bs:continue
        color=tuple(bs.inputs['Base Color'].default_value)
        glass=mat.get('surface_role')=='glass' or 'glass' in mat.name.lower()
        metal=mat.get('surface_role')=='metal' or any(k in mat.name.lower() for k in ('metal','steel'))
        # City reference glass uses a solid blue-grey with roughness 0.5.
        if glass:color=(.16,.20,.26,1.)
        for key in ('Base Color','Normal','Roughness','Metallic','Alpha','Transmission Weight'):
            for link in list(bs.inputs[key].links):mat.node_tree.links.remove(link)
        bs.inputs['Base Color'].default_value=color
        bs.inputs['Roughness'].default_value=.5 if glass else (.45 if metal else .75)
        bs.inputs['Metallic'].default_value=.8 if metal else 0.
        bs.inputs['Alpha'].default_value=1.;bs.inputs['Transmission Weight'].default_value=0.
        mat.diffuse_color=color
        for node in list(mat.node_tree.nodes):
            if node.type not in ('BSDF_PRINCIPLED','OUTPUT_MATERIAL'):mat.node_tree.nodes.remove(node)
        mat['city_low_poly']=PROFILE;count+=1
    return count

def apply_city_style(objects=None):
    # Lettering is deliberately omitted from this kit, including display text.
    for ob in list(bpy.data.objects):
        if ob.type=='FONT' or ob.get('city_sign'):
            bpy.data.objects.remove(ob,do_unlink=True)
    for c in list(bpy.data.collections):
        if not any(ob.get('shutter_rig') for ob in c.objects):continue
        c['glazing']='open aperture behind wooden shutters; no glass'
        for ob in list(c.objects):
            if ob.type=='MESH' and ob.data.materials and all(m and m.name.endswith('_glass') for m in ob.data.materials):
                bpy.data.objects.remove(ob,do_unlink=True)
    rows=[]
    for ob in list(objects if objects is not None else bpy.data.objects):
        if ob.type=='MESH':
            row=simplify(ob)
            if row:rows.append(row)
    count=materials()
    return {'profile':PROFILE,'materials_updated':count,'meshes':rows,
            'triangles_before':sum(r['before'] for r in rows),
            'triangles_after':sum(r['after'] for r in rows)}

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

_city_load_library=load_library
def load_library():
    result=_city_load_library()
    apply_city_style()
    apply_retro_style()
    return result
_city_generate_building=generate_building
def generate_building(*args,**kwargs):
    result=_city_generate_building(*args,**kwargs)
    apply_city_style()
    apply_retro_style()
    return result
