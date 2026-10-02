"""Audit current source lettering and window glazing, without exporting."""
from pathlib import Path
import json,sys,bpy
BASE=Path(__file__).resolve().parent;sys.path.insert(0,str(BASE))
from export_modules import materialize
street='--street' in sys.argv
ROOT=BASE.parent/'street_retro' if street else BASE/'retro'
errors=[];rows=[]
if any(o.type=='FONT' or o.get('city_sign') for o in bpy.data.objects):errors.append('lettering remains')
if not street:
    manifest=json.loads((ROOT/'kit_manifest.json').read_text())
    original=bpy.context.scene
    for entry in manifest['modules']:
        if 'Window' not in entry['id']:continue
        temp=bpy.data.scenes.new('No_Glass_Check');bpy.context.window.scene=temp
        materialize(bpy.data.collections[entry['collection']],temp.collection)
        panes=[o for o in temp.objects if o.type=='MESH' and o.data.materials and all(m and m.name.endswith('_glass') for m in o.data.materials)]
        shutter=any(o.get('shutter_rig') for o in temp.objects)
        if shutter and panes:errors.append('glass behind shutters '+entry['id'])
        if not shutter and not panes:errors.append('closed window lost glass '+entry['id'])
        rows.append({'id':entry['id'],'shutter':shutter,'glass_panes':len(panes)})
        bpy.context.window.scene=original;objects=list(temp.objects);bpy.data.scenes.remove(temp)
        for ob in objects:bpy.data.objects.remove(ob,do_unlink=True)
report={'passed':not errors,'no_lettering':True,'windows':rows,'errors':errors,'export_run':False}
(ROOT/'source_cleanup_validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('SOURCE_CLEANUP_VALIDATION',json.dumps(report),flush=True)
if errors:raise SystemExit(1)
