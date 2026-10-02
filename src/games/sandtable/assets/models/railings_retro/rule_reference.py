"""Deterministic choice of a railing for one connected edge chain."""
import hashlib

def choose(rules,world_seed,edge_chain_id,context,style,route_shape='straight'):
    if context not in rules['contexts'] or style not in rules['styles']:raise ValueError('unknown context or style')
    if route_shape not in rules['route_shapes']:raise ValueError('unsupported route shape')
    forced=rules['route_shapes'][route_shape]
    if forced!='selected context design':design=forced
    else:
        weights=rules['contexts'][context]['design_weights']
        key=str(world_seed)+'|'+edge_chain_id+'|'+context+'|'+style
        value=int.from_bytes(hashlib.sha256(key.encode()).digest()[:8],'big')/2**64*sum(weights.values())
        design=list(weights)[-1]
        for candidate,weight in weights.items():
            value-=weight
            if value<0:design=candidate;break
    asset='railings/'+style+'/'+design
    if asset not in rules['asset_ids']:raise ValueError('asset missing from catalog')
    return asset

def boundary_masks(count):
    """The next tile owns shared posts. Last tile keeps its end post."""
    return [{'start':True,'end':i==count-1} for i in range(count)]
