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
