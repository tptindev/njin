"""Remove brick mortar decoration from the retro authoring kit. No GLB export."""
from pathlib import Path
import json,sys
import bpy
BASE=Path(__file__).resolve().parent;ROOT=BASE/'retro'
sys.path.insert(0,str(BASE))
from export_modules import materialize,bounds
REV='unified-kit-v3-retro-v4-plain-walls'
scene=bpy.data.scenes['PBK_Modular_Buildings'];bpy.context.window.scene=scene
removed=[]
for ob in list(bpy.data.objects):
    if ob.name.startswith('PBK_') and 'mortar' in ob.name.lower():
        removed.append(ob.name);bpy.data.objects.remove(ob,do_unlink=True)
text=bpy.data.texts['PBK_Generator.py'];generator=text.as_string()
start=generator.index("        if style == 'Brick':\n")
end=generator.index("    elif role == 'Floor':",start)
generator=generator[:start]+generator[end:]
generator=generator.replace('unified-kit-v3-retro-v3-materials',REV)
text.clear();text.write(generator);(ROOT/'generate.py').write_text(generator,encoding='utf8')
manifest=json.loads((ROOT/'kit_manifest.json').read_text(encoding='utf8'));manifest['revision']=REV
manifest['wall_pattern']='plain retro; no mortar decoration'
for entry in manifest['modules']:
    c=bpy.data.collections[entry['collection']];c['catalog_revision']=REV;entry['revision']=REV
    temp=bpy.data.scenes.new('Plain_Wall_Measure');bpy.context.window.scene=temp
    materialize(c,temp.collection);temp.frame_set(1);bpy.context.view_layer.update()
    entry['bounds_m']=bounds(temp.objects,bpy.context.evaluated_depsgraph_get())
    entry['triangles']=sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in temp.objects if o.type=='MESH')
    bpy.context.window.scene=scene;obs=list(temp.objects);bpy.data.scenes.remove(temp)
    for ob in obs:bpy.data.objects.remove(ob,do_unlink=True)
(ROOT/'kit_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
for filename,key in [('rules/building_rules.json','kit_revision'),('source_status.json','kit_revision'),('art_direction.json','revision')]:
    p=ROOT/filename;doc=json.loads(p.read_text(encoding='utf-8-sig'));doc[key]=REV
    if filename=='art_direction.json':doc['technical']['wall_pattern']='plain retro; mortar decoration removed'
    p.write_text(json.dumps(doc,ensure_ascii=False,indent=2),encoding='utf8')
scene.frame_set(1);bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'modular_building_kit.blend'))
# Check regeneration independently after saving the actual authored source.
ns={'__name__':'plain_wall_test','__file__':str(ROOT/'generate.py')}
exec(generator,ns);ns['load_library']()
c,spec=ns['generate_building'](scene.collection,name='Plain_Brick_Test',shape='Rectangle',width=3,depth=3,floors=2,style='Brick',seed=22,offset=(0,65,0))
remaining=[o.name for o in bpy.data.objects if o.name.startswith('PBK_') and 'mortar' in o.name.lower()]
report={'revision':REV,'removed_objects':len(removed),'remaining_pattern_objects':remaining,
        'generated_brick_house_checked':True,'passed':not remaining and len(removed)>0,'glb_export_run':False}
(ROOT/'wall_pattern_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('PLAIN_WALL_VALIDATION',json.dumps(report),flush=True)
if not report['passed']:raise RuntimeError(report)
