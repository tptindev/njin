"""Check catalog coverage, valid choices, spatial rejection and rare event semantics."""
from pathlib import Path
import collections,copy,json,math
from retro_rule_reference import uniform,weighted_choice,select_street_slot,fits_rect,period_for,initialize_shutter,advance_shutter
ROOT=Path(__file__).resolve().parents[1];STREET=ROOT/'assets/models/street_retro';BUILDING=ROOT/'assets/models/procedural_building/retro'
street=json.loads((STREET/'distribution_rules.json').read_text(encoding='utf8'))
shutter=json.loads((BUILDING/'rules/shutter_behavior.json').read_text(encoding='utf8'))
manifest=json.loads((STREET/'asset_manifest.json').read_text(encoding='utf8'))
ids={a['id'] for a in manifest['assets']};assert set(street['assets'])==ids and len(ids)==25
covered=set()
for name,group in street['groups'].items():
    assert len(group['assets'])==len(group['weights']) and all(w>0 for w in group['weights'])
    assert set(group['assets'])<=ids
    chosen={weighted_choice(group['assets'],group['weights'],uniform('coverage',name,i)) for i in range(2000)}
    assert chosen==set(group['assets']);covered|=chosen
assert covered==ids
for zone,groups in street['zone_spawn_probability_per_slot'].items():
    assert all(0<=p<=1 for p in groups.values())
    for slot in range(100):
        for group in ('bench','lamp','pole','hydrant','bin','vendor'):
            a=select_street_slot(street,42,'street-fixture',zone,group,slot,8)
            assert a==select_street_slot(street,42,'street-fixture',zone,group,slot,8)
            if a:assert a['asset_id'] in ids and street['assets'][a['asset_id']]['spawn_mode']=='standalone'
            assert select_street_slot(street,42,'narrow',zone,'vendor',slot,2.4) is None
assert fits_rect([1,4,3,5],[0,3.6,20,8],[0,2,20,3.6],[])
assert not fits_rect([1,3,3,5],[0,0,20,8],[0,2,20,3.6],[])
assert not fits_rect([1,4,3,5],[0,3.6,20,8],[0,2,20,3.6],[[2.5,4,4,5]])
assert not fits_rect([-1,4,3,5],[0,3.6,20,8],[0,2,20,3.6],[])
assert set(street['vendor']['decomposed_children'])==set(street['groups']['vendor_anchor']['assets'])
for a in street['groups']['vendor_cluster']['assets']:assert street['assets'][a]['exclusive_with']
building=json.loads((BUILDING/'export_manifest.json').read_text(encoding='utf8'))
by_id={m['id']:m for m in building['modules']}
catalog={m['id']:m for m in json.loads((BUILDING/'kit_manifest.json').read_text(encoding='utf8'))['modules']}
for id in shutter['eligible_module_ids']:
    m=by_id[id];assert m['animated'] and m['animation']['name']==shutter['animation']['exported_clip']
    assert catalog[id]['glazing'].startswith('open aperture') and m['animation']['open_angle_degrees']==115
assert shutter['periods'][0]['hours'][0]==0 and shutter['periods'][-1]['hours'][1]==24
for a,b in zip(shutter['periods'],shutter['periods'][1:]):assert a['hours'][1]==b['hours'][0]
assert all(0<=p['open_probability']<=1 and 0<=p['event_rate_per_game_hour']<=.12 for p in shutter['periods'])
assert period_for(shutter,6)['open_probability']>.5 and period_for(shutter,23)['open_probability']<.2
total=0;days=30;windows=1000;max_daily=0;minimum_gap=math.inf
for index in range(windows):
    key=(7261,'house-'+str(index),1,'bay-2',0,1)
    state=initialize_shutter(shutter,key,0);events=advance_shutter(shutter,state,days*24)
    daily=collections.Counter(math.floor(e['game_hour']/24) for e in events)
    assert not daily or max(daily.values())<=2
    max_daily=max(max_daily,max(daily.values(),default=0))
    for a,b in zip(events,events[1:]):
        minimum_gap=min(minimum_gap,b['game_hour']-a['game_hour']);assert b['game_hour']-a['game_hour']>=8
    total+=len(events)
    if index<20:
        step=initialize_shutter(shutter,key,0);stepped=[]
        for hour in range(1,days*24+1):stepped+=advance_shutter(shutter,step,hour)
        assert step==state and stepped==events,'tick-rate-dependent result'
        restored=json.loads(json.dumps(state));assert advance_shutter(shutter,restored,days*24+24)==advance_shutter(shutter,state,days*24+24)
        try:advance_shutter(shutter,state,0)
        except ValueError:pass
        else:raise AssertionError('rewind accepted')
rate=total/(windows*days);assert 0<rate<1,'shutters transition too often'
report={'passed':True,'street_asset_ids_covered':len(covered),'shutter_module_ids':len(shutter['eligible_module_ids']),
        'simulation':{'windows':windows,'game_days':days,'actual_transitions_per_window_day':rate,'max_transitions_in_one_day':max_daily,'minimum_gap_game_hours':minimum_gap},
        'checks':['25 IDs selectable in their correct standalone/composition role','deterministic slot selection','narrow-sidewalk vendor rejection','rectangle walkway/overlap/containment rejection','composite exclusivity contract','exported shutter clips and glazing','period coverage','daily cap and dwell','timer step invariance','save/load and rewind'],
        'runtime_pending':['C++ spatial polygon/navmesh placement','global visible animation budget and obstruction checks','actual game rendering/animation playback']}
(STREET/'runtime_rules_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
