"""Validate the shipped schema subset and measured plans; draw a plan contact sheet."""
from pathlib import Path
import json,math,copy,argparse
from make_building_rules import area,clip,inset_convex

ROOT=Path(__file__).resolve().parents[1];PACK=ROOT/'assets/models/procedural_building/clay/rules'
rules=json.loads((PACK/'building_rules.json').read_text(encoding='utf8'))
manifest=json.loads((PACK.parent/'kit_manifest.json').read_text(encoding='utf8'))
active={x['id'] for x in manifest['modules'] if x['asset']}

def schema_check(value,schema,path='$'):
    if 'oneOf' in schema:
        matches=sum(not schema_check(value,s,path) for s in schema['oneOf'])
        return [] if matches==1 else [path+': must match exactly one variant']
    errors=[];types=schema.get('type');types=[types] if isinstance(types,str) else types
    def matches(t):
        return {'object':isinstance(value,dict),'array':isinstance(value,list),'string':isinstance(value,str),
          'number':isinstance(value,(int,float)) and not isinstance(value,bool),'integer':isinstance(value,int) and not isinstance(value,bool),
          'boolean':isinstance(value,bool),'null':value is None}.get(t,False)
    if types and not any(matches(t) for t in types):return [path+': wrong type']
    if 'const' in schema and value!=schema['const']:errors.append(path+': wrong const')
    if 'enum' in schema and value not in schema['enum']:errors.append(path+': not in enum')
    if isinstance(value,dict):
        for key in schema.get('required',[]):
            if key not in value:errors.append(path+': missing '+key)
        if len(value)<schema.get('minProperties',0):errors.append(path+': too few properties')
        props=schema.get('properties',{});extra=schema.get('additionalProperties',True)
        for key,v in value.items():
            if key in props:errors+=schema_check(v,props[key],path+'.'+key)
            elif extra is False:errors.append(path+': unexpected '+key)
            elif isinstance(extra,dict):errors+=schema_check(v,extra,path+'.'+key)
    elif isinstance(value,list):
        if len(value)<schema.get('minItems',0) or len(value)>schema.get('maxItems',math.inf):errors.append(path+': wrong length')
        for i,v in enumerate(value):errors+=schema_check(v,schema.get('items',{}),path+f'[{i}]')
    elif isinstance(value,(int,float)) and not isinstance(value,bool):
        if not math.isfinite(value):errors.append(path+': nonfinite')
        if value<schema.get('minimum',-math.inf) or value>schema.get('maximum',math.inf):errors.append(path+': out of range')
        if value<=schema.get('exclusiveMinimum',-math.inf):errors.append(path+': not strictly positive')
    return errors

def contains(p,poly,epsilon=1e-7):
    x,y=p;inside=False
    for a,b in zip(poly,poly[1:]+poly[:1]):
        cross=(b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]);dot=(x-a[0])*(x-b[0])+(y-a[1])*(y-b[1])
        if abs(cross)<epsilon and dot<=epsilon:return True
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:inside=not inside
    return inside

def semantic(plan):
    # Fixture validator, not the general runtime solver for every allowed plan shape.
    if len(plan['floors'])!=2 or not plan['stair_core'] or plan['stair_core']['type']!='dogleg':
        return ['unsupported_fixture: requires two floors and a dogleg core']
    errors=[];rooms={r['id']:r for f in plan['floors'] for r in f['rooms']};ids=list(rooms)
    if len(ids)!=sum(len(f['rooms']) for f in plan['floors']):errors.append('duplicate_room_id')
    profile=rules['archetypes'][plan['archetype']]
    for key,dimension in [('width_bays','width_m'),('depth_bays','depth_m')]:
        bays=plan[dimension]/rules['units']['bay_m']
        if int(bays)!=bays or not profile[key][0]<=bays<=profile[key][1]:errors.append('archetype_dimension')
    if not profile['floors'][0]<=len(plan['floors'])<=profile['floors'][1]:errors.append('archetype_floors')
    for module in plan['required_module_ids']+[x['module_id'] for x in plan['daylight_apertures']]:
        if module not in active:errors.append('unknown_module:'+module)
    outer=plan['footprint']['outer_m'];inner=inset_convex(outer,.2)
    for f in plan['floors']:
        for r in f['rooms']:
            typ=rules['room_types'][r['type']];poly=r['polygon_m'];net=area(poly)-sum(area(h) for h in r['floor_voids_m'])
            if abs(net-r['net_area_m2'])>1e-3:errors.append('stale_area:'+r['id'])
            if net<typ['min_net_area_m2']-1e-3:errors.append('small_room:'+r['id'])
            span=min(max(p[j] for p in poly)-min(p[j] for p in poly) for j in (0,1))
            if span<typ['min_clear_width_m']-1e-4:errors.append('small_room_extent:'+r['id'])
            if any(not contains(p,inner,2e-6) for p in poly):errors.append('room_outside:'+r['id'])
            if r['furniture_coverage_target']>rules['space_presets'][plan['space_preset']]['furniture_coverage_max']:errors.append('clutter:'+r['id'])
        for i,a in enumerate(f['rooms']):
            for b in f['rooms'][i+1:]:
                if area(clip(a['polygon_m'],b['polygon_m']))>1e-5:errors.append('room_overlap:'+a['id']+'/'+b['id'])
    graph={id:set() for id in ['outside']+ids}
    for p in plan['portals']:
        a,b=p['from_room'],p['to_room']
        if a not in graph or b not in graph:errors.append('unknown_portal_room');continue
        graph[a].add(b);graph[b].add(a)
        if p['net_clear_width_m']<.8 or p['height_m']<2.2:errors.append('blocked_portal:'+p['id'])
        if p['module_id'] and p['module_id'] not in active:errors.append('unknown_module:'+p['module_id'])
        if p['kind']=='stair_link':continue
        normal=p['normal_m'];length=math.hypot(*normal)
        if abs(length-1)>1e-6:errors.append('invalid_portal_normal')
        # Probe through the actual wall gap. Both rooms must meet this opening.
        for id in (a,b):
            if id=='outside':continue
            probes=[[p['center_m'][j]+sign*normal[j]*.26 for j in (0,1)] for sign in (-1,1)]
            if not any(contains(q,rooms[id]['polygon_m']) for q in probes):errors.append('portal_misses_room:'+p['id']+'/'+id)
    visited={'outside'};todo=['outside']
    while todo:
        for id in graph[todo.pop()]-visited:visited.add(id);todo.append(id)
    if set(ids)-visited:errors.append('unreachable_room')
    for r in rooms.values():
        apertures=[x for x in plan['daylight_apertures'] if x['room_id']==r['id']]
        if rules['room_types'][r['type']]['daylight_required'] and sum(x['glazed_area_m2'] for x in apertures)<r['net_area_m2']*.08-1e-6:
            errors.append('insufficient_daylight:'+r['id'])
    for ap in plan['daylight_apertures']:
        if ap['room_id'] not in rooms:errors.append('unknown_window_room');continue
        p=[ap['center_m'][j]+.25*ap['inward_normal'][j] for j in (0,1)]
        if not contains(p,rooms[ap['room_id']]['polygon_m']):errors.append('window_misses_room')
    stair=plan['stair_core']
    if sum(stair['split_risers'])*stair['riser_m']!=3 or stair['clear_width_m']<1.2:errors.append('invalid_stair')
    for key in ('lower_landing_m','turn_landing_m','upper_landing_m'):
        x0,y0,x1,y1=stair[key]
        if y1-y0<1.4-1e-6 or x1-x0<1.2-1e-6:errors.append('small_landing')
    if not plan['floors'][1]['slab_voids_m']:errors.append('missing_slab_void')
    bath=[next(r for r in f['rooms'] if r['type']=='bathroom')['polygon_m'] for f in plan['floors']]
    if bath[0]!=bath[1]:errors.append('wet_stack_mismatch')
    return errors

def diagram(plans):
    from PIL import Image,ImageDraw,ImageFont
    image=Image.new('RGB',(1500,1500),'#eef3f8');draw=ImageDraw.Draw(image)
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',18);small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',14)
    title=ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf',26)
    draw.text((35,18),'PROCEDURAL BUILDING / SPACIOUS INTERIOR RULE EXAMPLES',font=title,fill='#153252')
    colors={'living':'#badce9','living_dining_kitchen':'#badce9','shop':'#f0c991','bedroom':'#b7d6b4',
            'bathroom':'#b5c2e8','utility':'#d3d3e1','storage':'#d3d3e1','office':'#bfe1d2','kitchen':'#efddb2',
            'corridor':'#faf0c8','stair':'#c9ced5'}
    for row,plan in enumerate(plans):
        for col,floor in enumerate(plan['floors']):
            ox=45+col*745;oy=100+row*465;scale=37
            def point(p):return (ox+p[0]*scale,oy+(10-p[1])*scale)
            draw.text((ox,oy-32),plan['archetype']+' / FLOOR '+str(floor['level']),font=font,fill='#153252')
            draw.polygon([point(p) for p in plan['footprint']['outer_m']],fill='#42546a')
            for r in floor['rooms']:
                draw.polygon([point(p) for p in r['polygon_m']],fill=colors[r['type']])
                bx=sum(p[0] for p in r['polygon_m'])/len(r['polygon_m']);by=sum(p[1] for p in r['polygon_m'])/len(r['polygon_m'])
                if 'front' in r['id']:bx=4 if r['type']=='shop' else 3.1;by=2.5
                if 'east' in r['id']:bx=9;by=2.5
                label=r['type'].replace('living_dining_kitchen','living + kitchen')+'\n'+str(round(r['net_area_m2'],1))+' m2'
                bounds=draw.multiline_textbbox((0,0),label,font=small,spacing=2)
                draw.multiline_text((point([bx,by])[0]-(bounds[2]-bounds[0])/2,point([bx,by])[1]-18),label,font=small,fill='#18344a',spacing=2)
            for poly in floor['slab_voids_m']:
                draw.polygon([point(p) for p in poly],fill='#637083')
            for p in plan['portals']:
                if p['floor_from']!=floor['level'] or p['kind']=='stair_link':continue
                normal=p['normal_m'];t=[normal[1],-normal[0]];w=p['aperture_width_m']/2
                endpoints=[[p['center_m'][j]+sign*w*t[j] for j in (0,1)] for sign in (-1,1)]
                draw.line([point(q) for q in endpoints],fill='#ffffff',width=5)
            for ap in plan['daylight_apertures']:
                if ap['floor']!=floor['level']:continue
                normal=ap['inward_normal'];t=[normal[1],-normal[0]]
                draw.line([point([ap['center_m'][j]+sign*.55*t[j] for j in (0,1)]) for sign in (-1,1)],fill='#22a8c7',width=5)
            walk=plan['stair_core']['walking_line_m'];draw.line([point(p) for p in walk],fill='#dc6f4a',width=3)
            draw.text((ox,oy+380),'12 x 10 m | corridor 1.5 m | door clear 0.9 m | blue: glazing',font=small,fill='#38536b')
    image.save(PACK/'layout_examples.png')

def main():
    global PACK, rules, manifest, active
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,help='Alternate kit folder containing rules/')
    args=parser.parse_args()
    if args.root:
        PACK=args.root.resolve()/'rules'
        rules=json.loads((PACK/'building_rules.json').read_text(encoding='utf8'))
        manifest=json.loads((PACK.parent/'kit_manifest.json').read_text(encoding='utf8'))
        active={x['id'] for x in manifest['modules'] if x['asset']}
    errors=[];rs=json.loads((PACK/'building_rules.schema.json').read_text(encoding='utf8'))
    ps=json.loads((PACK/'building_plan.schema.json').read_text(encoding='utf8'))
    errors+=schema_check(rules,rs)
    if rules['kit_revision']!=manifest['revision']:errors.append('kit_revision_mismatch')
    for name,weights in rules['appearance']['district_weights'].items():
        if abs(sum(weights.values())-1)>1e-6:errors.append('style_weights:'+name)
    for id,p in rules['archetypes'].items():
        if any(x<1 or x>8 for x in p['floors']) or p['floors'][0]>p['floors'][1]:errors.append('floor_range:'+id)
        if abs(sum(p['roof_weights'].values())-1)>1e-6:errors.append('roof_weights:'+id)
    plans=[json.loads(p.read_text(encoding='utf8')) for p in sorted((PACK/'examples').glob('*.plan.json'))]
    measured=[]
    for p in plans:
        err=schema_check(p,ps)+semantic(p);errors+=err
        request=json.loads((PACK/'examples'/f'{p["archetype"]}.request.json').read_text(encoding='utf8'))
        request_schema=json.loads((PACK/'building_request.schema.json').read_text(encoding='utf8'))
        errors+=schema_check(request,request_schema)
        if area(p['footprint']['outer_m'])/area(request['parcel_polygon_m'])>rules['parcel']['max_lot_coverage']:errors.append('lot_coverage')
        anchor=request['footprint_origin_in_parcel_m']
        if any(not contains([point[j]+anchor[j] for j in (0,1)],request['parcel_polygon_m']) for point in p['footprint']['outer_m']):errors.append('parcel_containment')
        measured.append(dict(archetype=p['archetype'],rooms=sum(len(f['rooms']) for f in p['floors']),
            footprint_area_m2=round(area(p['footprint']['outer_m']),3),portals=len(p['portals']),daylight_apertures=len(p['daylight_apertures']),errors=err))
    negative=[]
    for case,mutate,expected in [
        ('unreachable',lambda p:p['portals'].clear(),'unreachable_room'),
        ('invalid_asset',lambda p:p['required_module_ids'].append('Modern/DoesNotExist'),'unknown_module'),
        ('missing_void',lambda p:p['floors'][1]['slab_voids_m'].clear(),'missing_slab_void')]:
        sample=copy.deepcopy(plans[0]);mutate(sample);detected=any(expected in e for e in semantic(sample))
        negative.append(dict(case=case,rejected=detected))
        if not detected:errors.append('negative_case_failed:'+case)
    sample=copy.deepcopy(plans[0]);sample['floors'][0]['rooms'][1]['polygon_m']=copy.deepcopy(sample['floors'][0]['rooms'][0]['polygon_m'])
    detected=any('room_overlap' in x for x in semantic(sample));negative.append(dict(case='overlapping_rooms',rejected=detected))
    if not detected:errors.append('negative_case_failed:overlapping_rooms')
    report=dict(passed=not errors,kit_revision=manifest['revision'],archetypes=len(rules['archetypes']),plans=measured,negative_cases=negative,
        validation_scope=['shipped JSON Schema keyword subset','two-floor convex dogleg fixtures only; not a general plan solver','active module references','room net areas / extents / containment / overlaps',
            'portal-room geometric probes and graph reachability','glazing area / destination','stair dimensions / slab void / wet alignment'],
        runtime_checks_pending=['eroded nav mesh','collision sweep of animated doors','mesh headroom on stair walking line','actual game importer and rendering'],errors=errors)
    (PACK/'rules_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8',newline='\n')
    print(json.dumps(report,indent=2))
    if errors:raise SystemExit(1)
    diagram(plans)

if __name__=='__main__':main()
