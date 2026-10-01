"""Open wooden-shutter apertures and sealed clear glazing elsewhere."""
import bpy

def apply_window_glazing():
    names=('PBK_Indochine_Window','PBK_Indochine_ArcWindow_R2_A45','PBK_Indochine_ArcWindow_R4_A30')
    glass_meshes=set()
    for name in names:
        collection=bpy.data.collections.get(name)
        if collection:
            collection['glazing']='none; wooden shutters only'
            for ob in collection.objects:
                if ob.type=='MESH' and ob.data.materials and all(m and m.name.endswith('_glass') for m in ob.data.materials):
                    glass_meshes.add(ob.data)
    removed=[]
    # Also remove linked copies already placed in generated houses.
    for ob in list(bpy.data.objects):
        if ob.type=='MESH' and ob.data in glass_meshes:
            removed.append(ob.name)
            bpy.data.objects.remove(ob,do_unlink=True)
    for style in ('Indochine','Modern','Brick'):
        mat=bpy.data.materials.get('PBK_'+style+'_glass')
        if not mat:continue
        bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        bs.inputs['Base Color'].default_value=(.96,.985,1.,1.)
        bs.inputs['Roughness'].default_value=.035
        bs.inputs['Transmission Weight'].default_value=1.
        bs.inputs['Alpha'].default_value=1.
        bs.inputs['IOR'].default_value=1.45
        mat.diffuse_color=(.96,.985,1.,.18)
        mat['surface_role']='glass';mat['glazing']='sealed clear pane'
        for prop in ('use_raytrace_refraction','use_screen_refraction'):
            if hasattr(mat,prop):setattr(mat,prop,True)
    return removed
