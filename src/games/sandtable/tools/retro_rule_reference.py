"""Portable reference for weighted street choices and game-clock shutter scheduling.
The game must provide spatial fit/collision tests and animation playback.
"""
import hashlib,math

def uniform(*parts):
    digest=hashlib.sha256('|'.join(map(str,parts)).encode('utf8')).digest()
    value=(int.from_bytes(digest[:8],'big')+.5)/2**64
    return max(2**-53,min(1-2**-53,value))

def weighted_choice(ids,weights,u):
    if len(ids)!=len(weights) or not ids or any(w<=0 for w in weights):raise ValueError('invalid weighted group')
    target=u*sum(weights)
    for asset,weight in zip(ids,weights):
        target-=weight
        if target<0:return asset
    return ids[-1]

def select_street_slot(rules,seed,street_id,zone,group,slot,sidewalk_width):
    """Return a parent candidate; spatial fitting must happen before commitment."""
    key=(seed,street_id,rules['version'],group,slot,0)
    vendor=group=='vendor'
    if vendor:
        if sidewalk_width<rules['vendor']['no_vendor_on_sidewalk_narrower_than_m']:return None
        complete=uniform(*key,'mode')<rules['vendor']['modes']['complete_cluster']
        family='vendor_cluster' if complete else 'vendor_anchor'
    else:family=group
    probability=rules['zone_spawn_probability_per_slot'][zone].get(family,0)
    if uniform(*key,'spawn')>=probability:return None
    pool=rules['groups'][family]
    asset=weighted_choice(pool['assets'],pool['weights'],uniform(*key,'asset'))
    # Composition children cannot become independent sidewalk spawns.
    if rules['assets'][asset]['spawn_mode']!='standalone':raise ValueError('child spawned standalone')
    return {'asset_id':asset,'vendor_slot_id':f'{street_id}:{slot}' if vendor else None,
            'mode':('complete_cluster' if complete else 'decomposed') if vendor else 'standalone'}

def fits_rect(envelope,band,walkway,reserved,gap=.25):
    """Conservative horizontal fixture fit; production needs transformed polygons/navmesh."""
    x0,y0,x1,y1=envelope
    if x0>=x1 or y0>=y1:return False
    if x0<band[0] or y0<band[1] or x1>band[2] or y1>band[3]:return False
    def overlap(a,b,margin=0):
        return a[0]<b[2]+margin and a[2]>b[0]-margin and a[1]<b[3]+margin and a[3]>b[1]-margin
    return not overlap(envelope,walkway) and not any(overlap(envelope,r,gap) for r in reserved)

def period_for(rules,time):
    hour=time%24
    return next(p for p in rules['periods'] if p['hours'][0]<=hour<p['hours'][1])

def candidate_interval(rules,key,index):
    return -math.log1p(-uniform(*key,index,'dt'))/rules['sampling']['candidate_rate_per_game_hour']

def initialize_shutter(rules,stable_key,now):
    """stable_key=(seed,building_id,floor,bay_id,rig_index,rule_version)."""
    key=tuple(stable_key)
    return {'stable_window_id':key,'state':'OPEN' if uniform(*key,-1,'initial')<period_for(rules,now)['open_probability'] else 'CLOSED',
            'next_candidate_game_hour':now+candidate_interval(rules,key,0),'event_index':0,
            'last_transition_game_hour':now,'last_evaluated_game_hour':now,
            'transition_day':math.floor(now/24),'transitions_today':0}

def advance_shutter(rules,state,now):
    """Return logical transitions. Offscreen/time jumps apply only final pose, no replay."""
    if now<state['last_evaluated_game_hour']:raise ValueError('game clock rewound without restoring state')
    key=tuple(state['stable_window_id']);events=[];limits=rules['limits']
    while state['next_candidate_game_hour']<=now:
        time=state['next_candidate_game_hour'];index=state['event_index'];period=period_for(rules,time)
        day=math.floor(time/24)
        if day!=state['transition_day']:state['transition_day']=day;state['transitions_today']=0
        accepted=uniform(*key,index,'accept')<period['event_rate_per_game_hour']/rules['sampling']['candidate_rate_per_game_hour']
        target='OPEN' if uniform(*key,index,'target')<period['open_probability'] else 'CLOSED'
        if accepted and target!=state['state'] and time-state['last_transition_game_hour']>=limits['minimum_dwell_game_hours'] and state['transitions_today']<limits['max_actual_transitions_per_window_per_game_day']:
            events.append({'game_hour':time,'from':state['state'],'to':target})
            state['state']=target;state['last_transition_game_hour']=time;state['transitions_today']+=1
        state['event_index']+=1
        state['next_candidate_game_hour']=time+candidate_interval(rules,key,state['event_index'])
    state['last_evaluated_game_hour']=now
    return events
