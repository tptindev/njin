"""Author a portable rule pack and measured example plans; does not modify the game."""
from pathlib import Path
import json, math, ast

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/models/procedural_building/clay/rules'
KIT=json.loads((OUT.parent/'kit_manifest.json').read_text(encoding='utf8'))
CURRENT_RULES=json.loads((OUT/'building_rules.json').read_text(encoding='utf8'))
KIT_PALETTES={style:[colors[role] for role in ('wall','trim','frame','roof')]
              for style,colors in CURRENT_RULES['appearance']['palettes'].items()}

def write(name,data):
    p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')

def room(minimum,target,width,daylight,privacy,legacy):
    return dict(min_net_area_m2=minimum,target_net_area_m2=target,min_clear_width_m=width,
                daylight_required=daylight,privacy=privacy,legacy_room_kind=legacy)

ROOMS={
 'living':room(18,26,3.2,True,'public','living'),
 'living_dining_kitchen':room(22,32,3.4,True,'shared','living'),
 'shop':room(24,40,3.4,True,'public','shop'),
 'bedroom':room(12,18,2.8,True,'private','bedroom'),
 'kitchen':room(9,14,2.4,True,'service','kitchen'),
 'bathroom':room(4,6,1.8,False,'private','bathroom'),
 'storage':room(3,7,1.4,False,'service','hall_storage'),
 'utility':room(5,8,1.8,False,'service','kitchen'),
 'office':room(9,13,2.4,True,'private','office'),
 'corridor':room(0,0,1.4,False,'circulation','corridor'),
 'stair':room(0,0,2.84,False,'circulation','stair'),
 'apartment_unit':room(38,55,4,True,'private','apartment_unit'),
 # Rooms of the town's other buildings: hotel floors, schools, the open
 # floors of workshops, warehouses and the market hall.
 'hotel_room':room(14,20,3,True,'private','hotel_room'),
 'classroom':room(30,45,5,True,'public','classroom'),
 'hall':room(40,90,6,True,'public','hall_storage'),
 'market_floor':room(60,140,8,True,'public','hall_market')}

def profile(shape,w,d,floors,program,fronts,**extra):
    return dict(footprint_shape=shape,width_bays=w,depth_bays=d,floors=floors,
                program=program,road_frontages=fronts,space_preset='spacious',independent_access=False,**extra)

PROFILES={
 'detached_spacious':profile('Rectangle',[5,8],[5,8],[1,3],{'ground':['living','kitchen','bathroom'],'upper':['bedroom','bathroom']},['south'],roof_weights={'Flat':.45,'Gable':.55}),
 'townhouse':profile('Rectangle',[3,4],[6,9],[2,4],{'ground':['living','kitchen','bathroom'],'upper':['bedroom','bathroom']},['south'],space_override_on_small_lot=False,roof_weights={'Flat':.9,'Gable':.1}),
 'shop_house':profile('Rectangle',[4,6],[5,8],[2,4],{'ground':['shop','storage','bathroom'],'upper':['living_dining_kitchen','bedroom','bathroom']},['south'],roof_weights={'Flat':.9,'Gable':.1}),
 'corner_shop_house':profile('CornerShopHouse',[4,6],[4,7],[2,4],{'ground':['shop','storage','bathroom'],'upper':['living_dining_kitchen','bedroom','bathroom']},['south','east'],roof_weights={'Flat':1}),
 'corner_shop_3_fronts':profile('CornerShopHouse3Fronts',[5,7],[5,7],[2,4],{'ground':['shop','storage','bathroom'],'upper':['living_dining_kitchen','bedroom','bathroom']},['south','east','west'],rear_blank=True,roof_weights={'Flat':1}),
 'corner_shop_rounded':profile('CornerShopHouseRounded',[5,7],[5,7],[2,4],{'ground':['shop','storage','bathroom'],'upper':['living_dining_kitchen','bedroom','bathroom']},['south','rounded_corner','east'],radius_m=[4],rear_blank=True,roof_weights={'Flat':1}),
 'courtyard_house':profile('Courtyard',[6,8],[6,8],[1,3],{'ground':['living','kitchen','bedroom','bathroom'],'upper':['bedroom','bathroom']},['south'],courtyard_min_clear_m=[4,4],wing_min_bays=2,requires_custom_tiles=True,roof_weights={'Flat':1}),
 'low_rise_apartment':profile('Rectangle',[6,9],[6,10],[2,5],{'ground':['corridor','apartment_unit'],'upper':['corridor','apartment_unit']},['south'],units_per_floor=[2,4],shared_core=True,roof_weights={'Flat':1}),
 # Nha ong: one room across a 2-3 bay lot, rooms one behind another, a
 # straight flight of the kit's Stair along a side wall with the way past it.
 'tube_house':profile('Rectangle',[2,4],[6,12],[1,5],{'ground':['living','kitchen','bathroom'],'upper':['bedroom','bathroom']},['south'],layout='tube',stair='straight',party_walls=True,roof_weights={'Flat':.85,'Gable':.15}),
 'tube_shop_house':profile('Rectangle',[2,4],[6,12],[1,5],{'ground':['shop','kitchen','bathroom'],'upper':['living_dining_kitchen','bedroom','bathroom']},['south'],layout='tube',stair='straight',party_walls=True,roof_weights={'Flat':.9,'Gable':.1}),
 # Blocks round a corridor: units front and back, one dogleg core at an end.
 'apartment_block':profile('Rectangle',[5,15],[4,12],[2,8],{'ground':['shop','corridor','storage'],'upper':['corridor','apartment_unit']},['south'],layout='corridor',free_standing=True,shared_core=True,roof_weights={'Flat':1}),
 'hotel':profile('Rectangle',[4,12],[4,12],[2,8],{'ground':['shop','corridor','office','bathroom'],'upper':['corridor','hotel_room']},['south'],layout='corridor',free_standing=True,shared_core=True,roof_weights={'Flat':1}),
 'school':profile('Rectangle',[6,14],[3,8],[1,3],{'ground':['classroom','corridor','office','bathroom'],'upper':['classroom','corridor']},['south'],layout='corridor',free_standing=True,shared_core=True,roof_weights={'Flat':1}),
 # One open floor, an office, a store and a washroom along the back.
 'workshop_hall':profile('Rectangle',[4,14],[3,10],[1,2],{'ground':['hall','office','storage','bathroom'],'upper':['hall','storage']},['south'],layout='hall',free_standing=True,roof_weights={'Gable':1}),
 'warehouse_hall':profile('Rectangle',[5,14],[4,10],[1,3],{'ground':['hall','office','bathroom'],'upper':['hall','storage']},['south'],layout='hall',free_standing=True,roof_weights={'Gable':1}),
 'market_hall':profile('Rectangle',[8,16],[6,12],[1,2],{'ground':['market_floor','office','storage','bathroom'],'upper':['market_floor','storage']},['south'],layout='hall',free_standing=True,shop_frontage=True,roof_weights={'Gable':1})}

RULES={
 'schema':'sandtable.building-rules.v1','version':1,'kit_revision':KIT['revision'],
 'design_intent':'Tunable gameplay defaults and a data contract for the generator.',
 'units':{'length':'metre','area':'square_metre','angle':'radian','local_axes':['+X width','+Y front to back','+Z up'],
          'bay_m':2,'storey_m':3,'exterior_wall_m':.2,'partition_wall_m':.12,'slab_m':.2,
          'interior_grid_m':.5,'geometry_epsilon_m':.001},
 'engine_adapter':{'table_world_units_per_metre':6,'render_units_per_table_unit':.03125,
          'render_units_per_metre':.1875,'building_angle_unit':'degree',
          'table_formula':'center + axis_x * (local_x-width/2)*6 + axis_y * (local_y-depth/2)*6',
          'render_formula':'to3d(table_xy, local_z*6*unit3d)',
          'legacy_interior_cell_world_units':22,'legacy_interior_cell_metres':22/6,
          'desired_4m_room_cell_world_units':24},
 'sampling':{'algorithm':'mix32_matching_city_render_common','uint32_overflow':'wrap',
          'seed_input':'building.look; persist rule_version and archetype',
          'streams':{'footprint':101,'program':211,'layout':307,'appearance':401,'facade':503,'furniture':601},
          'stable_iteration':'sort keys and candidate IDs; do not use std::hash or unordered iteration',
          'retry_count':32,'failure':'NoFit with reasons; never shrink a hard clearance to force a fit'},
 'parcel':{'max_lot_coverage':.85,'front_setback_m':[1,3],'free_side_setback_m':[.5,2],
           'party_wall_side_setback_m':0,'rear_setback_m':[1,2],'garden_target_area_m2':12,
           'balcony_and_awning_overhang_must_fit_reserved_apron':True,'entrance_must_connect_to_walkable_street':True},
 'space_presets':{
   'comfortable':{'corridor_min_m':1.2,'corridor_target_m':1.4,'furniture_coverage_max':.35,'room_area_scale':.9},
   'spacious':{'corridor_min_m':1.4,'corridor_target_m':1.6,'furniture_coverage_max':.25,'room_area_scale':1},
   'generous':{'corridor_min_m':1.8,'corridor_target_m':2,'furniture_coverage_max':.20,'room_area_scale':1.25}},
 'appearance':{
   'styles':['Indochine','Modern','Brick'],
   'district_weights':{'old_quarter':{'Indochine':.65,'Brick':.35},'market':{'Indochine':.65,'Brick':.2,'Modern':.15},
     'residential':{'Indochine':.45,'Modern':.45,'Brick':.1},'nightlife':{'Modern':.7,'Indochine':.2,'Brick':.1},
     'docks':{'Brick':.6,'Modern':.4},'industrial':{'Brick':.65,'Modern':.35},'new_urban':{'Modern':.9,'Indochine':.1}},
   'per_building_style_count':1,'palette_source':'../generate.py PALETTES; Blender Principled Base Color',
   'palette_color_space':'linear_rgba',
   'palettes':{style:dict(zip(('wall','trim','frame','roof'),map(list,colors))) for style,colors in KIT_PALETTES.items()},
   'window_density':[.55,.9],'balcony_chance':[.15,.4],
   'mandatory_room_windows_override_density':True,'vertical_bay_alignment':True,
   'detail_budget_per_bay':{'primary_features':1,'secondary_features':2},
   'back_party_wall_windows':False,'roof_ribs_follow_surface_normal':True,
   'awning_min_clear_above_opening_m':.06,'awning_must_have_wall_mount':True},
 'room_types':ROOMS,'archetypes':PROFILES,
 'layout':{'sequence':['reserve exterior wall offset and voids','reserve fixed stair and wet cores','solve room graph',
      'partition net polygons','place portals','connect daylight to facade','place furniture','validate walkable geometry'],
   'room_areas_are':'net polygons; exclude walls, shafts, stair voids and courtyard',
   'wing_min_clear_width_m':3.2,'wet_stack_alignment_tolerance_m':.1,
   'room_graph':{'must_connect_to':'primary entrance through portals and stairs',
       'bedroom_may_be_through_room':False,'bathroom_directly_opens_into_kitchen':False,
       'preferred_adjacencies':[['living','kitchen'],['living','corridor'],['shop','storage'],['corridor','bedroom'],['corridor','bathroom'],['corridor','stair']],
       'commercial_private_access':'If independent_access is enabled, connect upper core to a separate street entrance; do not traverse shop.'},
   'daylight':{'bedroom_requires_facade_or_courtyard':True,'living_requires_facade_or_courtyard':True,
       'minimum_glazed_area_to_room_area':.08,'window_to_room_mapping_required':True}},
 'circulation':{'actor_radius_m':.3,'wall_margin_m':.1,'single_actor_clear_m':.8,'two_actor_passing_clear_m':1.4,
      'entrance_aperture_m':1.05,'entrance_min_net_open_m':.9,'interior_door_min_net_open_m':.9,
      'headroom_min_m':2.2,'furniture_distance_to_portal_m':.35,
      'door_swing':'90 degrees inward; sweep collision over the whole motion',
      'nav_validation':'Erode walkable polygons by actor radius + margin; BFS/A* to room targets; a graph alone is insufficient.'},
 'stairs':{'fixed_core_on_all_floors':True,'flight_clear_width_m':1.2,'landing_clear_depth_m':1.4,
      'riser_m':.2,'tread_m':.25,'risers_per_storey':15,
      'straight':{'flight_run_m':3.75,'min_core_length_including_two_landings_m':6.55,'kit_role':'Stair'},
      'dogleg':{'split_risers':[8,7],'central_gap_m':.2,'min_core_clear_width_m':2.84,
           'min_core_length_m':4.8,'geometry_source':'runtime_generated; no dogleg asset in current kit'},
      'upper_floor_slab_opening_required':True,'headroom_check':'sample the stair walking line against every slab and beam above'},
 'assembly':{'module_id_format':'Style/Role','active_asset_ids':[e['id'] for e in KIT['modules'] if e['asset']],
      'entrance_role':'DoorRigged','animated_rig_nonuniform_scale_allowed':False,
      'shell':'one welded shell per floor; apply opening schedule once; use facade dressing without substrate/floor_band',
      'curve_families':[{'radius_m':4,'angle_degrees':30,'pieces_per_quarter_turn':3},{'radius_m':2,'angle_degrees':45,'pieces_per_quarter_turn':2}],
      'curve_mating':'right socket + right_tangent_degrees; require same radius/angle',
      'rounded_corner_entrance':'planar DoorRigged on middle tangent; ArcDoor is an authoring module, not a second overlapping shell',
      'balcony':'full-height access door; threshold <= 0.03m; slab flush with interior; check overhang against neighbouring bays',
      'interior_geometry':'generate partition walls, interior doors, ceilings and stair voids; they are not exported assets in this kit',
      'collision_and_nav':'derive from the same accepted plan as renderer'},
 'validation':{'hard':['parcel_and_setback_containment','connected_footprint','no_overlapping_room_interiors',
       'min_room_area_and_width','all_rooms_reachable','geometric_nav_clearance','stairs_aligned_and_headroom',
       'slabs_have_stair_voids','wet_stack_aligned','window_and_balcony_destination_valid',
       'doors_sweep_clear','module_ids_and_curve_sockets_valid','no_duplicate_substrate'],
      'soft_score_weights':{'room_target_area':.3,'daylight':.25,'short_private_routes':.2,'facade_rhythm':.15,'low_furniture_density':.1},
      'on_failure':['reduce optional furniture','remove optional balcony','reduce optional rooms','retry layout with a new layout sub-seed','return NoFit'],
      'preserve_hard_constraints':True},
 'integration_status':'Rule/data package only; not wired to city generator or renderer.'}

def rect(x0,y0,x1,y1):return [[x0,y0],[x1,y0],[x1,y1],[x0,y1]]
def area(poly):return abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(poly,poly[1:]+poly[:1])))/2

def inset_convex(poly,amount):
    result=[]
    for i,p in enumerate(poly):
        prev=poly[i-1];nxt=poly[(i+1)%len(poly)]
        a=[p[0]-prev[0],p[1]-prev[1]];b=[nxt[0]-p[0],nxt[1]-p[1]]
        la=math.hypot(*a);lb=math.hypot(*b);na=[-a[1]/la,a[0]/la];nb=[-b[1]/lb,b[0]/lb]
        den=1+na[0]*nb[0]+na[1]*nb[1]
        result.append([p[j]+amount*(na[j]+nb[j])/den for j in range(2)])
    return result

def clip(subject,boundary):
    for a,b in zip(boundary,boundary[1:]+boundary[:1]):
        def side(p):return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])
        result=[]
        for p,q in zip(subject,subject[1:]+subject[:1]):
            sp,sq=side(p),side(q)
            if sp>=-1e-8:result.append(p)
            if (sp>=0)!=(sq>=0):
                t=sp/(sp-sq);result.append([p[j]+t*(q[j]-p[j]) for j in range(2)])
        subject=result
        if not subject:break
    return subject

def example(kind,style,seed):
    rounded=kind=='corner_shop_rounded';commercial=kind!='detached_spacious';w,d=12,10;r=4
    outer=rect(0,0,w,d)
    if rounded:
        outer=[[0,0],[w-r,0]]+[[w-r+r*math.cos(-math.pi/2+i*math.pi/96),r+r*math.sin(-math.pi/2+i*math.pi/96)] for i in range(1,49)]+[[w,d],[0,d]]
    inner=inset_convex(outer,.2);floors=[];portals=[]
    def portal(level,from_id,to_id,center,width=1.05,axis='y',kind='door'):
        portals.append(dict(id=f'p{len(portals):02}',floor_from=level,floor_to=level,
             from_room=from_id,to_room=to_id,center_m=center,axis=axis,kind=kind,
             normal_m=[1,0] if axis=='x' else [0,1],
             aperture_width_m=width,net_clear_width_m=width-.15 if kind=='door' else width,
             height_m=2.35,module_id=f'{style}/DoorRigged' if from_id=='outside' else None))
    for level in (0,1):
        prefix=f'f{level}_';rooms=[]
        def add(id,typ,box,exposures=()):
            polygon=clip(rect(*box),inner)
            rooms.append(dict(id=prefix+id,type=typ,polygon_m=polygon,net_area_m2=round(area(polygon),4),
                floor_voids_m=[],daylight_edges=list(exposures),furniture_coverage_target=0 if typ in ('stair','corridor') else .20))
        if level==0 and commercial:add('front','shop',(.2,.2,11.8,4.44),['south','east']+(['west'] if kind=='corner_shop_3_fronts' else []))
        else:
            add('front','living_dining_kitchen' if commercial else 'living',(.2,.2,5.94 if level else 7.94,4.44),['south'])
            add('east','bedroom' if level else 'kitchen',(6.06 if level else 8.06,.2,11.8,4.44),['east','south'])
        add('stair','stair',(.2,4.56,3.08,9.8))
        add('hall','corridor',(3.2,4.56,11.8,6.06))
        add('bath','bathroom',(3.2,6.18,5.5,9.8))
        add('service','storage' if commercial and level==0 else 'utility',(5.62,6.18,7.92,9.8))
        add('rear','office' if commercial else 'bedroom',(8.04,6.18,11.8,9.8),['east'])
        portal(level,prefix+'front',prefix+'hall',[5,4.5],1.6,kind='open')
        if any(x['id']==prefix+'east' for x in rooms):portal(level,prefix+'east',prefix+'hall',[10,4.5])
        portal(level,prefix+'hall',prefix+'stair',[3.14,5.4],axis='x')
        for id,x in [('bath',4.2),('service',6.7),('rear',9.5)]:portal(level,prefix+'hall',prefix+id,[x,6.12])
        void=[[.22,5.86],[1.62,5.86],[1.62,6.23],[3.06,6.23],[3.06,9.5],[.22,9.5]]
        floors.append(dict(level=level,elevation_m=level*3,rooms=rooms,slab_voids_m=[void] if level else []))
        if level:
            stair_room=next(x for x in rooms if x['type']=='stair');stair_room['floor_voids_m']=[void]
            stair_room['net_area_m2']=round(area(stair_room['polygon_m'])-area(void),4)
    entrance=[w-r+r/math.sqrt(2),r-r/math.sqrt(2)] if rounded else [5,.1]
    portal(0,'outside','f0_front',entrance,axis='tangent' if rounded else 'y')
    if rounded:portals[-1]['normal_m']=[-1/math.sqrt(2),1/math.sqrt(2)]
    portals.append(dict(id='vertical_main',floor_from=0,floor_to=1,from_room='f0_stair',to_room='f1_stair',
         center_m=[2.34,5.5],axis='vertical',normal_m=[0,0],kind='stair_link',aperture_width_m=1.2,net_clear_width_m=1.2,height_m=2.8,module_id=None))
    plan=dict(schema='sandtable.building-plan.v1',rule_version=1,kit_revision=KIT['revision'],seed=seed,
         archetype=kind,style=style,space_preset='spacious',width_m=w,depth_m=d,
         footprint={'shape':PROFILES[kind]['footprint_shape'],'outer_m':outer,'holes_m':[],'radius_m':r if rounded else None},
         road_frontages=PROFILES[kind]['road_frontages'],floors=floors,portals=portals,
         stair_core={'id':'main','type':'dogleg','room_ids':['f0_stair','f1_stair'],'clear_width_m':1.2,
             'riser_m':.2,'tread_m':.25,'split_risers':[8,7],
             'lower_flight_m':[.34,5.98,1.54,7.98],'upper_flight_m':[1.74,6.23,2.94,7.98],
             'lower_landing_m':[.34,4.56,1.54,5.98],'turn_landing_m':[.34,7.98,2.94,9.38],
             'upper_landing_m':[1.74,4.83,2.94,6.23],
             'walking_line_m':[[.94,5.5,0],[.94,5.98,0],[.94,7.98,1.6],[.94,8.68,1.6],[2.34,8.68,1.6],[2.34,7.98,1.6],[2.34,6.23,3],[2.34,5.5,3]],
             'geometry_source':'runtime_generated'},
         required_module_ids=[f'{style}/{x}' for x in ['Wall','Window','Shopfront' if commercial else 'Wall','DoorRigged','Floor','FloorBand','Coping']],
         runtime_generated_geometry=['interior_partitions','interior_portal_frames_and_leaves','dogleg_stair','ceiling','slab_with_voids'],
         provenance='Measured rule example, not an interior already authored in the Blender scene.')
    if rounded:plan['required_module_ids'] += [f'{style}/Arc{x}_R4_A30' for x in ('Window','Shopfront','Band','Parapet','Coping')]
    # Derive window destinations from accepted room polygons, before random facade decoration.
    def inside(point,poly):
        result=False;x,y=point
        for a,b in zip(poly,poly[1:]+poly[:1]):
            if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:result=not result
        return result
    candidates=[]
    for x in range(1,w-r if rounded else w,2):candidates.append(([x,0],[0,1],False))
    if rounded:
        for t in (-75,-45,-15):
            theta=math.radians(t);candidates.append(([w-r+r*math.cos(theta),r+r*math.sin(theta)],[-math.cos(theta),-math.sin(theta)],True))
    for y in range(r+1 if rounded else 1,d,2):candidates.append(([w,y],[-1,0],False))
    if kind=='corner_shop_3_fronts':
        for y in range(1,d,2):candidates.append(([0,y],[1,0],False))
    if kind=='detached_spacious':
        for x in range(1,w,2):candidates.append(([x,d],[0,-1],False))
    plan['daylight_apertures']=[]
    for floor in floors:
        for room_data in floor['rooms']:
            if not ROOMS[room_data['type']]['daylight_required']:continue
            family='Shopfront' if room_data['type']=='shop' else 'Window';glazing=3.8 if family=='Shopfront' else 1.5
            count=math.ceil(room_data['net_area_m2']*.08/glazing);placed=0
            for center,normal,curved in candidates:
                target=[center[j]+normal[j]*.25 for j in range(2)]
                if not inside(target,room_data['polygon_m']):continue
                if floor['level']==0 and math.dist(center,entrance)<1.5:continue
                plan['daylight_apertures'].append(dict(floor=floor['level'],room_id=room_data['id'],center_m=center,
                    inward_normal=normal,module_id=f'{style}/'+(f'Arc{family}_R4_A30' if curved else family),
                    glazed_area_m2=glazing))
                placed+=1
                if placed==count:break
            if placed<count:raise ValueError(f'Not enough daylight bays for {kind}/{room_data["id"]}')
    return plan

def schema_for(value):
    """Conservative Draft 2020-12 structural schema; semantic constraints are separate."""
    if isinstance(value,dict):return {'type':'object','required':list(value),'properties':{k:schema_for(v) for k,v in value.items()},'additionalProperties':False}
    if isinstance(value,list):
        if not value:return {'type':'array'}
        first=value[0]
        if all(type(v)==type(first) for v in value):return {'type':'array','items':schema_for(first)}
        return {'type':'array'}
    if value is None:return {'type':['string','number','null']}
    if isinstance(value,bool):return {'type':'boolean'}
    if isinstance(value,int):return {'type':'number'}
    if isinstance(value,float):return {'type':'number'}
    return {'type':'string'}

def main():
    # Preserve the current runtime extensions when regenerating architectural fixtures.
    if 'runtime_rule_packs' in CURRENT_RULES: RULES['runtime_rule_packs']=CURRENT_RULES['runtime_rule_packs']
    if 'window_glazing' in CURRENT_RULES['assembly']: RULES['assembly']['window_glazing']=CURRENT_RULES['assembly']['window_glazing']
    write('building_rules.json',RULES)
    rs=schema_for(RULES);rs['$schema']='https://json-schema.org/draft/2020-12/schema';rs['title']='Sandtable building rule pack v1'
    # Profiles/styles are open dictionaries, not fixed to the shipped presets.
    for key in ('archetypes','room_types'):
        item=next(iter(RULES[key].values()));rs['properties'][key]={'type':'object','additionalProperties':schema_for(item)}
    # Archetype records have optional specialization fields, all sharing the required contract.
    common=['footprint_shape','width_bays','depth_bays','floors','program','road_frontages','space_preset','roof_weights']
    prop=rs['properties']['archetypes']['additionalProperties'];prop['required']=common;prop['additionalProperties']=True
    prop['properties']['roof_weights']={'type':'object','minProperties':1,'additionalProperties':{'type':'number','minimum':0,'maximum':1}}
    rs['properties']['appearance']['properties']['district_weights']={'type':'object','additionalProperties':{'type':'object','minProperties':1,'additionalProperties':{'type':'number','minimum':0,'maximum':1}}}
    write('building_rules.schema.json',rs)
    plans=[example('detached_spacious','Modern',101),example('corner_shop_3_fronts','Brick',202),example('corner_shop_rounded','Indochine',303)]
    ps=schema_for(plans[0]);ps['$schema']=rs['$schema'];ps['title']='Accepted building plan v1'
    # Rooms/floors have variable counts; polygons and voids are variable length arrays.
    point={'type':'array','items':{'type':'number'},'minItems':2,'maxItems':2}
    polygon={'type':'array','items':point,'minItems':3}
    fp=ps['properties']['footprint']['properties'];fp['outer_m']=polygon;fp['holes_m']={'type':'array','items':polygon}
    ps['properties']['floors'].update(minItems=1,maxItems=8)
    ps['properties']['seed'].update(type='integer',minimum=0,maximum=4294967295)
    ps['properties']['rule_version'].update(type='integer',const=1)
    for dimension in ('width_m','depth_m'):ps['properties'][dimension].update(exclusiveMinimum=0)
    floor=ps['properties']['floors']['items']['properties'];floor['slab_voids_m']={'type':'array','items':polygon}
    floor['level'].update(type='integer',minimum=0,maximum=7)
    room_schema=floor['rooms']['items']['properties'];room_schema['polygon_m']=polygon;room_schema['type']['enum']=list(ROOMS)
    room_schema['floor_voids_m']={'type':'array','items':polygon}
    room_schema['net_area_m2'].update(minimum=0)
    ps['properties']['style']['enum']=RULES['appearance']['styles'];ps['properties']['space_preset']['enum']=list(RULES['space_presets'])
    portal_schema=ps['properties']['portals']['items']['properties'];portal_schema['module_id']={'type':['string','null']}
    portal_schema['kind']['enum']=['door','open','stair_link'];portal_schema['axis']['enum']=['x','y','vertical','tangent']
    portal_schema['center_m']=point;portal_schema['normal_m']=point
    daylight_schema=ps['properties']['daylight_apertures']['items']['properties'];daylight_schema['center_m']=point;daylight_schema['inward_normal']=point
    ps['properties']['stair_core']['properties']['walking_line_m']['items'].update(minItems=3,maxItems=3)
    core=ps['properties']['stair_core'];core['required']=['id','type','room_ids','clear_width_m','riser_m','tread_m','walking_line_m','geometry_source']
    dogleg=dict(core);dogleg['required']=core['required']+['split_risers','lower_flight_m','upper_flight_m','lower_landing_m','turn_landing_m','upper_landing_m']
    dogleg['properties']=dict(core['properties']);dogleg['properties']['type']={'const':'dogleg'}
    straight=dict(core);straight['required']=core['required']+['flight_m','bottom_landing_m','top_landing_m','risers']
    straight['properties']=dict(core['properties']);straight['properties']['type']={'const':'straight'}
    for key in ('flight_m','bottom_landing_m','top_landing_m'):straight['properties'][key]={'type':'array','items':{'type':'number'},'minItems':4,'maxItems':4}
    straight['properties']['risers']={'type':'integer','minimum':1}
    ps['properties']['stair_core']={'oneOf':[{'type':'null'},dogleg,straight]}
    write('building_plan.schema.json',ps)
    for p in plans:
        write(f'examples/{p["archetype"]}.plan.json',p)
        req=dict(schema='sandtable.building-request.v1',seed=p['seed'],rule_version=1,archetype=p['archetype'],
            style=p['style'],space_preset=p['space_preset'],width_m=p['width_m'],depth_m=p['depth_m'],floors=len(p['floors']),
            parcel_polygon_m=rect(0,0,16,14),footprint_origin_in_parcel_m=[2,2],
            road_sides=['south','east','west'] if p['archetype']=='corner_shop_3_fronts' else ['south','east'] if p['archetype']=='corner_shop_rounded' else ['south'],
            table_center_world_units=[100,200],building_angle_degrees=30,
            capabilities_required=['polygon_shell','slab_void','runtime_dogleg_stair','animated_door','interior_partitions'])
        write(f'examples/{p["archetype"]}.request.json',req)
    request_schema=schema_for(req);request_schema['$schema']=rs['$schema'];request_schema['title']='Building generator request v1'
    request_schema['properties']['seed'].update(type='integer',minimum=0,maximum=4294967295)
    request_schema['properties']['floors'].update(type='integer',minimum=1,maximum=8)
    request_schema['properties']['parcel_polygon_m']=polygon
    for key in ('footprint_origin_in_parcel_m','table_center_world_units'):request_schema['properties'][key]=point
    write('building_request.schema.json',request_schema)
    print(f'Wrote {len(PROFILES)} archetypes and {len(plans)} plans to {OUT}')

if __name__=='__main__':main()
