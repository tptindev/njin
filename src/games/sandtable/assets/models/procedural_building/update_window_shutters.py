"""Replace decorative shutters with usable hinged leaves; save source, no export."""
from pathlib import Path
import json,sys,math
import bpy
BASE=Path(__file__).resolve().parent;ROOT=BASE/'clay';sys.path.insert(0,str(BASE))
from clay_frame_geometry import rounded_loop
import clay_window_shutters as shutters
shutters.rounded_loop=rounded_loop
from export_modules import materialize,bounds
REV='unified-kit-v3-clay-v7-fitted-shutters'
scene=bpy.data.scenes['PBK_Modular_Buildings'];bpy.context.window.scene=scene
for ob in list(bpy.data.objects):
    if ob.get('shutter_leaf') or ob.get('shutter_rig') or ob.get('shutter_hardware'):
        bpy.data.objects.remove(ob,do_unlink=True)
shutters.ensure_window_shutters()
text=bpy.data.texts['PBK_Generator.py'];generator=text.as_string().split('\n# Working shutter revision')[0]
generator=generator.replace('unified-kit-v3-clay-v4-plain-walls',REV)
generator=generator.replace('unified-kit-v3-clay-v5-shutters',REV)
generator=generator.replace('unified-kit-v3-clay-v6-louvers',REV)
# Decorative slabs are replaced by complete hinge-controlled shutter assemblies.
if "        if style == 'Indochine' and role == 'Window':" in generator:
    start=generator.index("        if style == 'Indochine' and role == 'Window':")
    end=generator.index("    elif role == 'Floor':",start)
    generator=generator[:start]+generator[end:]
generator+='\n# Working shutter revision\n'+(BASE/'clay_window_shutters.py').read_text(encoding='utf8')
generator+='''
_shutter_load_library = load_library
def load_library():
    result = _shutter_load_library()
    ensure_window_shutters()
    return result
_shutter_generate_building = generate_building
def generate_building(*args, **kwargs):
    ensure_window_shutters()
    return _shutter_generate_building(*args, **kwargs)
'''
text.clear();text.write(generator);(ROOT/'generate.py').write_text(generator,encoding='utf8')
manifest=json.loads((ROOT/'kit_manifest.json').read_text(encoding='utf8'));manifest['revision']=REV
for entry in manifest['modules']:
    c=bpy.data.collections[entry['collection']];c['catalog_revision']=REV;entry['revision']=REV
    temp=bpy.data.scenes.new('Measure_Shutters');bpy.context.window.scene=temp
    materialize(c,temp.collection);temp.frame_set(1);bpy.context.view_layer.update()
    entry['bounds_m']=bounds(temp.objects,bpy.context.evaluated_depsgraph_get())
    entry['triangles']=sum(sum(len(p.vertices)-2 for p in ob.data.polygons) for ob in temp.objects if ob.type=='MESH')
    rigs=[o for o in temp.objects if o.type=='ARMATURE' and o.get('shutter_rig')]
    if rigs:
        entry.update(animated=True,animation_kind='window_shutters',shutter_rig_count=len(rigs),
                     shutter_bones=['Shutter_Left','Shutter_Right'],open_angle_degrees=115,
                     open_frame=1,closed_frame=24,louver_angle_degrees=-25,
                     louver_slope='outward-down when closed',closed_clearance_m=.004,
                     center_gap_m=.008,leaf_height_m=1.452,leaf_width_m=.532,
                     source_actions=['Shutter_Open','Shutter_Close','Shutter_CloseOpen'])
    bpy.context.window.scene=scene;obs=list(temp.objects);bpy.data.scenes.remove(temp)
    for ob in obs:bpy.data.objects.remove(ob,do_unlink=True)
(ROOT/'kit_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
for filename,key in [('rules/building_rules.json','kit_revision'),('source_status.json','kit_revision'),('art_direction.json','revision')]:
    p=ROOT/filename;doc=json.loads(p.read_text(encoding='utf-8-sig'));doc[key]=REV
    if filename=='art_direction.json':doc['technical']['window_shutters']='full aperture coverage, wooden louvers, two hinge bones, 115 degree outward swing'
    p.write_text(json.dumps(doc,ensure_ascii=False,indent=2),encoding='utf8')
scene.frame_set(1);bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'modular_building_kit.blend'))
print('WINDOW_SHUTTERS_SAVED',REV,flush=True)
