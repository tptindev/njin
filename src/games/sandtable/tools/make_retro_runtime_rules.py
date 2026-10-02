"""Author street distribution and rare shutter rules; no model/export changes."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
STREET=ROOT/'assets/models/street_retro'
BUILDING=ROOT/'assets/models/procedural_building/retro'
def write(path,data):
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
groups={
 'bench':(['StoneBench','StoneBenchBack','PublicBench','PublicBenchBack'],[1,3,1,3],20),
 'lamp':(['StreetLampSingle','StreetLampDouble'],[4,1],22),
 'pole':(['UtilityPole'],[1],35),
 'hydrant':(['FireHydrant'],[1],60),
 'bin':(['TrashBinRound','TrashBinWheelie'],[3,1],24),
 'vendor_cluster':(['HuTieuSpace','BanhMiSpace','MiQuangSpace','SidewalkTeaSpace'],[3,4,2,3],16),
 'vendor_anchor':(['HuTieuCart','BanhMiCart','MiQuangCounter','TeaShoulderPole'],[3,4,2,3],16),
 'high_seat':(['PlasticChairHigh','PlasticStoolHigh'],[3,1],0),
 'low_seat':(['PlasticChairLow','PlasticStoolLow'],[2,3],0),
 'low_table':(['PlasticTableLow'],[1],0),
 'shade':(['StreetUmbrella'],[1],0),
 'tea_service':(['TeaServiceSet'],[1],0),
}
profiles={
 'residential':{'bench':.35,'lamp':1.,'pole':.7,'hydrant':.55,'bin':.30,'vendor_cluster':.08,'vendor_anchor':.08},
 'commercial':{'bench':.45,'lamp':1.,'pole':.65,'hydrant':.65,'bin':.8,'vendor_cluster':.45,'vendor_anchor':.45},
 'market':{'bench':.2,'lamp':1.,'pole':.6,'hydrant':.7,'bin':.95,'vendor_cluster':.7,'vendor_anchor':.7},
 'park':{'bench':.85,'lamp':1.,'pole':0.,'hydrant':.3,'bin':.75,'vendor_cluster':.12,'vendor_anchor':.12},
}
assets={}
for group,(names,weights,spacing) in groups.items():
 for name,weight in zip(names,weights):
    asset={'group':group,'selection_weight':weight,'spawn_mode':'standalone' if spacing else 'composition_child',
           'band':'frontage' if group.startswith('vendor') or not spacing else 'curb',
           'minimum_same_group_spacing_m':spacing,'grounded':True,'scale_range':[1,1],
           'rotation':'front faces road (-Y local); seats face table; benches face walkway',
           'exclusive_with':[]}
    if group=='vendor_cluster':asset['exclusive_with']=['decomposed vendor at same vendor_slot_id']
    if group=='vendor_anchor':asset['exclusive_with']=['complete vendor cluster at same vendor_slot_id']
    if group=='hydrant':asset['access_clearance_m']=.65
    if group=='bin':asset['keep_from_food_service_m']=2.
    if group=='pole':asset['wire_link']='adjacent accepted poles; use wire_sockets, never cross doorway/corner clearance'
    if group=='lamp':asset['lights']='use light_points; light enabled 18:00-06:00, no geometry respawn'
    assets['street/'+name]=asset
manifest=json.loads((STREET/'asset_manifest.json').read_text(encoding='utf8'))
assert set(assets)=={a['id'] for a in manifest['assets']}
street={
 'schema':'sandtable.street-distribution.v1','version':1,'asset_revision':manifest['revision'],
 'units':'metre','purpose':'gameplay placement defaults, not a real street design standard',
 'coordinates':{'authoring':'X along street, Y from curb towards buildings, Z up; serving front -Y towards road',
                'gltf_adapter':'[x,z,-y] exactly once; geometry and authoring anchors/clearance polygons must use the same transform'},
 'sampling':{'algorithm':'sha256-counter-v1','key':'world_seed|street_segment_id|rule_version|group|slot_index|attempt_index',
             'uniform':'first 8 SHA256 bytes as big-endian uint64; (value+0.5)/2^64 clamped to [2^-53,1-2^-53]',
             'stable_order':'sorted street IDs then fixed group order; never global frame RNG',
             'group_order':['hydrant','pole','lamp','vendor','bin','bench'],
             'candidate_attempts':8,'slot_jitter_fraction':.20,'on_failure':'skip optional placement; never shrink an asset or walkway'},
 'zone_spawn_probability_per_slot':profiles,
 'groups':{g:{'assets':['street/'+n for n in ns],'weights':ws,'slot_spacing_m':sp} for g,(ns,ws,sp) in groups.items()},
 'vendor':{'modes':{'complete_cluster':.75,'decomposed':.25},'one_mode_per_vendor_slot':True,
           'minimum_spacing_m':8.,'max_per_100m':5,'max_frontage_coverage':.35,
           'no_vendor_on_sidewalk_narrower_than_m':3.2,'opening_hours':[6,22],
           'closed_hours_behavior':'retain static asset; suppress customers/service; do not despawn furniture',
           'complete_cluster_children_already_included':True,
           'decomposed_children':{
             'street/HuTieuCart':{'high_seat_count':[2,4],'low_table_count':[0,0],'shade_count':[0,1]},
             'street/BanhMiCart':{'high_seat_count':[0,2],'low_table_count':[0,0],'shade_count':[0,1]},
             'street/MiQuangCounter':{'high_seat_count':[2,4],'low_table_count':[0,0],'shade_count':[0,1]},
             'street/TeaShoulderPole':{'low_seat_count':[2,5],'low_table_count':[1,1],'tea_service_count':[1,1],'shade_count':[0,1]}},
           'child_placement':'use parent service/work anchors; chairs/stools 0.55m apart, face service/table; table-to-seat edge gap 0.30m; umbrella shades seats',
           'chair_stool_choice':'sample high_seat or low_seat group; low table only with low seats',
           'reserve_entire_group_before_commit':True,'if_child_group_does_not_fit':'reduce optional seats/shade, then reject whole vendor group'},
 'placement':{'curb_margin_m':.15,'facade_margin_m':.20,'walkway_clear_width_m':1.6,
              'door_approach_clear_width_m':1.5,'door_approach_depth_m':1.5,'intersection_corner_exclusion_m':3.,
              'generic_prop_gap_m':.25,'max_ground_coverage':.35,'minimum_headroom_m':2.3,
              'geometry':'transform actual bounds_m and clearance_polygon_m; fit entire footprint/service/work envelope inside permitted band',
              'bands':'curb strip + continuous clear walkway + frontage strip; choose a strip that fits actual asset depth, not a fixed universal strip width',
              'hard_rejections':['outside sidewalk polygon','walkway blocked','door approach/swing overlap','corner or crossing exclusion overlap','reserved service/work zone overlap','prop overlap','hydrant access blocked','canopy below headroom over walkway','bin within 2m of food service','vendor density exceeded'],
              'utilities':'reject pole wire corridors crossing road/door clearances; lights and hydrants use gameplay zones; narrow sidewalks may skip all props',
              'lod':'keep stable IDs; visual LOD/culling must not change placement or collision'},
 'assets':assets,'runtime_status':'data contract only; C++ consumer pending'}
write(STREET/'distribution_rules.json',street)
periods=[
 {'hours':[0,6],'open_probability':.08,'event_rate_per_game_hour':.005},
 {'hours':[6,9],'open_probability':.75,'event_rate_per_game_hour':.12},
 {'hours':[9,17],'open_probability':.85,'event_rate_per_game_hour':.025},
 {'hours':[17,21],'open_probability':.25,'event_rate_per_game_hour':.10},
 {'hours':[21,24],'open_probability':.10,'event_rate_per_game_hour':.005}]
shutters={
 'schema':'sandtable.shutter-behavior.v1','version':1,'kit_revision':json.loads((BUILDING/'kit_manifest.json').read_text(encoding='utf8'))['revision'],
 'eligible_module_ids':['Indochine/Window','Indochine/ArcWindow_R2_A45','Indochine/ArcWindow_R4_A30','Indochine/CornerWindow90'],
 'grouping':'one stable ID per physical shutter rig/window, both leaves together; CornerWindow90 has two independent window rigs',
 'clock':'monotonic absolute game hours; day=floor(hours/24); hour=hours modulo 24; independent of frame rate and wall clock',
 'periods':periods,'hour_boundaries':'start inclusive, end exclusive',
 'sampling':{'algorithm':'sha256-counter-v1','key':'world_seed|building_id|floor|bay_or_curve_piece|rig_index|rule_version|event_index|purpose',
             'uniform':street['sampling']['uniform'],
             'candidate_rate_per_game_hour':.12,'next_candidate':'previous_candidate - log(1-u)/0.12',
             'accept_candidate':'u_accept < current_period.event_rate_per_game_hour/0.12',
             'target':'OPEN if u_target < period.open_probability else CLOSED',
             'initial_state':'sample once from current period on creation; set pose directly without animation',
             'same_target_as_current':'no action, no sound, no cooldown reset',
             'evaluate_on_timer_only':True},
 'limits':{'minimum_dwell_game_hours':8,'max_actual_transitions_per_window_per_game_day':2,
           'max_visible_transition_starts_per_neighborhood_per_real_minute':4,
           'event_rate_note':'0.12/hour is a candidate upper bound, not a forced toggle. Integrated accepted opportunities <=1.005/day before same-state/dwell rejection.',
           'idle_motion':False},
 'state':{'persist':['stable_window_id','state','next_candidate_game_hour','event_index','last_transition_game_hour','last_evaluated_game_hour','transition_day','transitions_today'],
          'load_or_time_jump':'advance logical events in timestamp order; coalesce to final pose for unloaded windows, never replay missed animations',
          'clock_rewind':'restore saved state with saved clock; never use negative elapsed time',
          'global_budget_exhausted':'coalesce to final target pose without animation when offscreen; defer visible transition until budget permits, coalesce pending targets',
          'manual_override':'interaction overrides scheduler until next eligible candidate after dwell; count manual changes toward daily cap'},
 'animation':{'exported_clip':'Shutter_CloseOpen','loop':False,'fps':24,
              'close_clip_time_s':[0,23/24],'open_clip_time_s':[47/24,71/24],
              'open_pose_clip_time_s':0,'closed_pose_clip_time_s':23/24,
              'transition_duration_real_seconds':1.25,'bone_names':['Shutter_Left','Shutter_Right'],
              'open_angle_degrees':115,'preserve_left_right_sign':True,
              'only_on_state_change':True,'interruption':'blend from current pose; do not snap/restart cycle',
              'source_only_actions':['Shutter_Open','Shutter_Close'],
              'visibility':'animate nearby visible windows only; offscreen pose updates directly',
              'collision':'check shutter swing; if obstructed defer transition; open aperture behind shutters; no glass'},
 'runtime_status':'data contract and reference scheduler only; C++ consumer pending'}
write(BUILDING/'rules/shutter_behavior.json',shutters)
rules_path=BUILDING/'rules/building_rules.json';rules=json.loads(rules_path.read_text(encoding='utf8'))
rules.setdefault('runtime_rule_packs',{}).update({'street_distribution':'../../../street_retro/distribution_rules.json','window_shutters':'shutter_behavior.json','railings':'../../../railings_retro/railing_rules.json','path_base':'directory containing building_rules.json'})
rules['assembly'].setdefault('window_glazing',{}).update({'wooden_shutter_modules':shutters['eligible_module_ids'],'wooden_shutter_glass':False,'other_window_glass':'sealed opaque matte panes; alphaMode OPAQUE; opacity 1.0'})
write(rules_path,rules)
schema_path=BUILDING/'rules/building_rules.schema.json'
schema=json.loads(schema_path.read_text(encoding='utf8'))
schema['properties']['runtime_rule_packs']={'type':'object','required':['street_distribution','window_shutters','path_base'],
    'properties':{k:{'type':'string'} for k in ('street_distribution','window_shutters','path_base','interior_lighting','railings')},'additionalProperties':False}
schema['properties']['assembly']['properties']['window_glazing']={'type':'object','required':['wooden_shutter_modules','wooden_shutter_glass','other_window_glass'],
    'properties':{'wooden_shutter_modules':{'type':'array','items':{'type':'string'}},'wooden_shutter_glass':{'type':'boolean'},'other_window_glass':{'type':'string'},'crossbars_pattern':{'type':'string'},'crossbars_scope':{'type':'string'},'crossbars_width_m':{'type':'number'}},'additionalProperties':False}
write(schema_path,schema)
layout_path=STREET/'sidewalk_layout.json';layout=json.loads(layout_path.read_text(encoding='utf8'))
layout['distribution_rule_pack']='distribution_rules.json';write(layout_path,layout)
print('RETRO_RUNTIME_RULES_WRITTEN',len(assets),'street assets;',len(shutters['eligible_module_ids']),'shutter module IDs')
if __name__=='__main__':pass
