"""Update retro source and generator; validate glazing, never export GLB."""
from pathlib import Path
import json,sys
import bpy
BASE=Path(__file__).resolve().parent;ROOT=BASE/'retro';sys.path.insert(0,str(BASE))
from retro_window_glazing import apply_window_glazing
from export_modules import materialize,bounds
REV='unified-kit-v3-retro-v12-shopfront-base20'
scene=bpy.data.scenes['PBK_Modular_Buildings'];bpy.context.window.scene=scene
removed=apply_window_glazing()
text=bpy.data.texts['PBK_Generator.py']
generator=text.as_string().split('\n# Window glazing revision')[0]
generator=generator.replace('unified-kit-v3-retro-v7-fitted-shutters',REV)
generator=generator.replace('unified-kit-v3-retro-v8-window-glazing',REV)
generator=generator.replace('unified-kit-v3-retro-v9-all-window-glass',REV)
generator=generator.replace('unified-kit-v3-retro-v10-glass-opacity40',REV)
generator=generator.replace('unified-kit-v3-retro-v11-opaque-glass',REV)
# Restore unconditional aperture glazing in future freshly-built libraries.
generator=generator.replace("    if not (style == 'Indochine' and role == 'Window'):\n        box(c,'glass'", "    box(c,'glass'")
generator=generator.replace("            (width-.10,.045,height-.06),mats['glass']", "        (width-.10,.045,height-.06),mats['glass']")
generator=generator.replace("        for v in data.vertices:v.co=src.matrix_local@v.co","        for key in src.keys():ob[key]=src[key]\n        for v in data.vertices:v.co=src.matrix_local@v.co")
generator+='\n# Window glazing revision\n'+(BASE/'retro_window_glazing.py').read_text(encoding='utf8')
generator+='''
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
'''
text.clear();text.write(generator);(ROOT/'generate.py').write_text(generator,encoding='utf8')
manifest=json.loads((ROOT/'kit_manifest.json').read_text(encoding='utf8'));manifest['revision']=REV
cases=[];errors=[]
for entry in manifest['modules']:
    c=bpy.data.collections[entry['collection']];c['catalog_revision']=REV;entry['revision']=REV
    temp=bpy.data.scenes.new('Glazing_Measure');bpy.context.window.scene=temp
    materialize(c,temp.collection);temp.frame_set(1);bpy.context.view_layer.update()
    entry['bounds_m']=bounds(temp.objects,bpy.context.evaluated_depsgraph_get())
    entry['triangles']=sum(sum(len(p.vertices)-2 for p in ob.data.polygons) for ob in temp.objects if ob.type=='MESH')
    panes=[o for o in temp.objects if o.type=='MESH' and o.data.materials and all(m and m.name.endswith('_glass') for m in o.data.materials)]
    if entry['id'] in ('Indochine/Window','Indochine/ArcWindow_R2_A45','Indochine/ArcWindow_R4_A30','Indochine/CornerWindow90'):
        if panes:errors.append('unexpected glass behind wooden shutters: '+entry['id'])
        entry['glazing']='open aperture behind wooden shutters; no glass'
        cases.append({'id':entry['id'],'glass_panes':len(panes)})
    elif 'Window' in entry['id']:
        if not panes:errors.append('missing sealed glazing: '+entry['id'])
        entry['glazing']='sealed clear glass'
        cases.append({'id':entry['id'],'glass_panes':len(panes)})
    if 'Shopfront' in entry['id']:
        bases=[o for o in temp.objects if o.type=='MESH' and o.get('shopfront_base_ratio')]
        if not panes or len(bases)!=len(panes):errors.append('shopfront base missing: '+entry['id'])
        for ob in panes+bases:
            lo,hi=ob['shopfront_original_z'];span=hi-lo
            actual=max(v.co.z for v in ob.data.vertices)-min(v.co.z for v in ob.data.vertices)
            ratio=.2 if ob.get('shopfront_base_ratio') else .8
            if abs(actual/span-ratio)>1e-5:errors.append('shopfront fill ratio: '+ob.name)
            if ratio==.2 and not all(m.name.endswith('_frame') for m in ob.data.materials):errors.append('base material: '+ob.name)
        entry['shopfront_glass_fill']=.8;entry['shopfront_frame_base']=.2
        cases.append({'id':entry['id'],'glass_panes':len(panes),'frame_bases':len(bases),'glass_ratio':.8,'base_ratio':.2})
    bpy.context.window.scene=scene;obs=list(temp.objects);bpy.data.scenes.remove(temp)
    for ob in obs:bpy.data.objects.remove(ob,do_unlink=True)
for style in ('Indochine','Modern','Brick'):
    mat=bpy.data.materials['PBK_'+style+'_glass']
    bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    if abs(bs.inputs['Alpha'].default_value-1.)>1e-5 or abs(bs.inputs['Roughness'].default_value-.35)>1e-5:errors.append('unclear '+style+' glazing')
if errors:raise RuntimeError(errors)
(ROOT/'kit_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
for filename,key in [('rules/building_rules.json','kit_revision'),('source_status.json','kit_revision'),('art_direction.json','revision')]:
    p=ROOT/filename;doc=json.loads(p.read_text(encoding='utf-8-sig'));doc[key]=REV
    if filename=='art_direction.json':
        doc['technical']['window_glazing']={'wooden_shutter_windows':'open aperture; no glass','other_panes':'sealed clear glass','transmission':0.,'opacity':1.,'transparency':0.,'roughness':.35,'ior':1.45}
        doc['technical']['accent_materials']['glass_transmission']=0.
        doc['technical']['accent_materials']['glass_opacity']=1.
        doc['technical']['accent_materials']['glass_roughness']=.35
    p.write_text(json.dumps(doc,ensure_ascii=False,indent=2),encoding='utf8')
status_path=ROOT/'source_status.json'
status=json.loads(status_path.read_text(encoding='utf8'));status.update(export_required=True,export_validated=False,note='v12 shopfront glazing 80%, frame-material base 20%; export pending.');status_path.write_text(json.dumps(status,indent=2),encoding='utf8')
scene.frame_set(1);bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'modular_building_kit.blend'))
ns={'__name__':'glazing_check','__file__':str(ROOT/'generate.py')}
exec(generator,ns);ns['load_library']()
ns['generate_building'](scene.collection,name='Glazing_Check_House',shape='CornerShopHouseRounded',width=5,depth=5,floors=2,style='Indochine',seed=312,offset=(0,65,0))
report={'revision':REV,'passed':True,'restored_glass_objects':removed,'window_cases':cases,'generator_reopened':True,'export_run':False}
(ROOT/'glazing_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('WINDOW_GLAZING_SAVED',json.dumps(report),flush=True)
