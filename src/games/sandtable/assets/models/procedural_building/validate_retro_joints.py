"""Measure frame/wall and pane/base contact in flat and curved module coordinates."""
from pathlib import Path
import json,math,bpy
ROOT=Path(__file__).resolve().parent/'retro';errors=[];cases=[]
doc=json.loads((ROOT/'kit_manifest.json').read_text())
for entry in doc['modules']:
    ident=entry['id'];role=ident.split('/')[1]
    if role not in ('Window','Shopfront','ArcWindow_R2_A45','ArcWindow_R4_A30','ArcShopfront_R2_A45','ArcShopfront_R4_A30'):continue
    c=bpy.data.collections[entry['collection']]
    frame=next(o for o in c.objects if o.type=='MESH' and 'continuous_frame' in o.name.lower())
    radius=float(c.get('radius_m',0))
    depth=lambda v: radius-math.hypot(v.co.x,v.co.y-radius) if radius else v.co.y
    frame_back=max(depth(v) for v in frame.data.vertices)
    if abs(frame_back)>1e-5:errors.append('frame does not touch wall '+ident)
    panes=[o for o in c.objects if o.type=='MESH' and o.data.materials and all(m and m.name.endswith('_glass') for m in o.data.materials)]
    for ob in panes:
        if any(abs(depth(v))>1e-5 for v in ob.data.vertices):errors.append('pane depth gap '+ident)
    if 'Shopfront' in role:
        base=next(o for o in c.objects if o.get('shopfront_base_ratio'))
        pane=panes[0]
        if base.data.materials[0]!=frame.data.materials[0]:errors.append('base material does not match frame '+ident)
        lower=max(v.co.z for v in base.data.vertices);upper=min(v.co.z for v in pane.data.vertices)
        if abs(lower-upper)>1e-6:errors.append('base/glass vertical gap '+ident)
        if any(abs(depth(v))>1e-5 for v in base.data.vertices):errors.append('base depth gap '+ident)
        total=max(v.co.z for v in pane.data.vertices)-min(v.co.z for v in base.data.vertices)
        if abs((max(v.co.z for v in base.data.vertices)-min(v.co.z for v in base.data.vertices))/total-.2)>1e-5:errors.append('shopfront ratio changed '+ident)
    cases.append({'id':ident,'frame_wall_gap_m':abs(frame_back),'pane_depth_m':0.})
report={'passed':not errors,'cases':cases,'errors':errors,'export_run':False}
(ROOT/'retro_joint_validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('RETRO_JOINT_VALIDATION',not errors,len(cases),errors,flush=True)
if errors:raise SystemExit(1)
