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
