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

