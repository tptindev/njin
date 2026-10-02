"""Update building/street Blender sources and reusable generators, no export."""
from pathlib import Path
import json,sys
import bpy
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(BASE))
from city_low_poly import apply_city_style,PROFILE
from retro_low_poly import apply_retro_style,RETRO
from export_modules import materialize,bounds
street='--street' in sys.argv
ROOT=BASE.parent/'street_retro' if street else BASE/'retro'
REV='vietnam-street-retro-v4' if street else 'unified-kit-v3-retro-v18-low-poly-balcony'
# Keep dense demo instances out of the dependency graph while applying modifiers.
links=[]
for scene in bpy.data.scenes:
    for child in list(scene.collection.children):
        links.append((scene,child));scene.collection.children.unlink(child)
report=apply_city_style()
report['retro']=apply_retro_style()
for scene,child in links:scene.collection.children.link(child)
manifest_path=ROOT/('asset_manifest.json' if street else 'kit_manifest.json')
doc=json.loads(manifest_path.read_text(encoding='utf8'))
doc['revision']=REV;doc['visual_profile']=RETRO;doc['glb_export_run']=False
if not street:doc['visual_variant']='low_poly_retro'
original=bpy.context.scene
rows=[]
old_exports=ROOT/'export_manifest.json'
baseline={}
if old_exports.exists():
    previous=json.loads(old_exports.read_text(encoding='utf8'))
    baseline={e['id']:e.get('triangles') for e in previous.get('assets' if street else 'modules',[])}
for entry in doc['assets' if street else 'modules']:
    c=bpy.data.collections[entry['collection']];c['catalog_revision']=REV
    temp=bpy.data.scenes.new('City_Measure');bpy.context.window.scene=temp
    materialize(c,temp.collection);temp.frame_set(1);bpy.context.view_layer.update()
    entry['bounds_m']=bounds(temp.objects,bpy.context.evaluated_depsgraph_get())
    before=baseline.get(entry['id']) or entry.get('triangles')
    entry['triangles']=sum(sum(len(p.vertices)-2 for p in ob.data.polygons) for ob in temp.objects if ob.type=='MESH')
    entry['visual_profile']=RETRO
    if any(o.get('facade_surface') for o in c.objects):
        entry['render_wall_thickness_m']=0.
        entry['collision']='separate plan-derived collider; visual wall is a zero-thickness exterior surface'
    if entry['id'] in ('Indochine/Window','Indochine/ArcWindow_R2_A45','Indochine/ArcWindow_R4_A30','Indochine/CornerWindow90'):
        entry['glazing']='open aperture behind wooden shutters; no glass'
    if not street:entry['revision']=REV
    rows.append({'id':entry['id'],'before':before,'after':entry['triangles']})
    bpy.context.window.scene=original;objects=list(temp.objects);bpy.data.scenes.remove(temp)
    for ob in objects:bpy.data.objects.remove(ob,do_unlink=True)
doc['visual_style']={'reference':'assets/models/city; Quaternius Downtown City MegaKit','shape':'retro low poly; square frames, hard edges, faceted curves, no retro rounding',
                     'materials':'solid palette, opaque blue-grey glass, no retro normal/ORM textures',
                     'preserved':'module IDs, pivots, snap extents, rigs, clip timing, shopfront 80/20 split'}
manifest_path.write_text(json.dumps(doc,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
if not street:
    text=bpy.data.texts['PBK_Generator.py']
    generator=text.as_string().split('\n# City low-poly revision')[0]
    generator=generator.replace('unified-kit-v3-retro-v12-shopfront-base20',REV)
    generator=generator.replace('unified-kit-v3-retro-v13-city-low-poly',REV)
    generator=generator.replace('unified-kit-v3-retro-v14-open-shutters',REV)
    generator=generator.replace('unified-kit-v3-retro-v15-surface-walls',REV)
    generator=generator.replace('unified-kit-v3-retro-v16-stairs',REV)
    generator=generator.replace('unified-kit-v3-retro-v17-window-cross',REV)
    if '\n# Window glazing revision\n' in generator:
        head,tail=generator.split('\n# Window glazing revision\n',1)
        generator=head+'\n# Window glazing revision\n'+(BASE/'retro_window_glazing.py').read_text(encoding='utf8')+'\n'+tail[tail.index('_glazing_load_library ='):]
    generator+='\n# City low-poly revision\n'+(BASE/'city_low_poly.py').read_text(encoding='utf8')
    generator+='\n'+(BASE/'retro_low_poly.py').read_text(encoding='utf8')
    generator+='''
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
'''
    text.clear();text.write(generator);(ROOT/'generate.py').write_text(generator,encoding='utf8')
    p=ROOT/'rules/building_rules.json';rules=json.loads(p.read_text(encoding='utf8'));rules['kit_revision']=REV
    rules['assembly']['window_glazing']['wooden_shutter_glass']=False
    rules['assembly']['wall_rendering']={'geometry':'single exterior surface; zero render thickness','backface_culling':True,
        'interior':'separate interior wall surfaces when required','collision_thickness_m':.2,
        'collision':'derive from plan, never use the render-surface AABB as a solid collider'}
    p.write_text(json.dumps(rules,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    p=ROOT/'art_direction.json';art=json.loads(p.read_text(encoding='utf8'));art['revision']=REV;art['city_style']=doc['visual_style']
    p.write_text(json.dumps(art,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
report['assets']=rows;report['revision']=REV;report['export_run']=False
if not street:
    report['catalog_triangles_before']=sum(r['before'] or 0 for r in rows)
    report['catalog_triangles_after']=sum(r['after'] for r in rows)
for image in list(bpy.data.images):
    if image.users==0:bpy.data.images.remove(image)
(ROOT/'city_style_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
status={'kit_revision':REV,'source_saved':True,'export_required':True,'export_validated':False,
        'glb_export_run':False,'note':'City low-poly source updated; existing GLBs remain previous revision until explicit export.'}
(ROOT/'source_status.json').write_text(json.dumps(status,indent=2)+'\n',encoding='utf8')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/('street_retro_kit.blend' if street else 'modular_building_kit.blend')))
print('CITY_STYLE_SAVED',REV,report['triangles_before'],report['triangles_after'],flush=True)
