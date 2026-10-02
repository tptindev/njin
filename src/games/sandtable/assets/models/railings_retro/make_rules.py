"""Regenerate the railing choice/assembly contract, without exporting models."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parent
CONTEXTS={
 'stair':{'StairSlope':6,'StairHandrail':3},
 'balcony':{'StraightVertical':5,'SquareFrame':4,'DiagonalCross':2,'Wave':2,'Arch':2,'Diamond':1,'SolidParapet':2},
 'bridge':{'BridgeHeavy':5,'DiagonalCross':4,'DiagonalSingle':2,'Arch':3,'StraightVertical':3,'WoodFence':2,'SolidParapet':2},
 'lake':{'LakeLow':4,'WoodFence':3,'Wave':2,'StraightVertical':3,'RiversideBars':2},
 'river':{'RiversideBars':6,'StraightVertical':4,'BridgeHeavy':2,'SolidParapet':2,'StraightHorizontal':1},
 'terrace':{'SquareGrid':4,'SquareFrame':4,'StraightHorizontal':3,'StraightVertical':3,'SolidParapet':2},
 'park':{'WoodFence':5,'LakeLow':3,'Wave':2,'Arch':2,'StraightVertical':2}}

def main():
 manifest=json.loads((ROOT/'asset_manifest.json').read_text(encoding='utf8'))
 rules={'schema':'sandtable.railings-rules.v1','asset_revision':manifest['revision'],
  'asset_manifest':'asset_manifest.json','units':'metre','socket_space':'Blender XYZ; convert to glTF [x,z,-y]',
  'styles':['Indochine','Modern','Brick'],
  'contexts':{k:{'design_weights':v,'height_policy':'use chosen asset height along the whole connected chain'} for k,v in CONTEXTS.items()},
  'route_shapes':{'straight':'selected context design','corner_90':'Corner90','arc_r2_45':'ArcR2A45','arc_r4_30':'ArcR4A30',
                  'stair_3m_3_75m':'StairSlope','stair_wall_mount':'StairHandrail','wall_mount':'WallHandrail'},
  'terminals':{'free_end':'EndPost','height_or_pattern_transition':'TransitionPost'},
  'selection':{'seed_key':['world_seed','edge_chain_id','context','style'],
               'scope':'one pattern and material palette per connected edge chain; never reroll each tile',
               'priority':'route geometry overrides ornament selection',
               'balcony':'follow building style','public_edges':'choose one style per bridge or waterfront zone'},
  'assembly':{'straight_span_m':2,'start_socket':'start','end_socket':'end',
              'placement':'align start to previous end; rotate by matching end/start tangents',
              'height':'attach floor/path sockets to walking surface; top handrail sockets must meet exactly',
              'curve':'fixed-radius arc modules; never nonuniformly scale curves',
              'corner':'Corner90 preserves a continuous top rail through the 90 degree turn',
              'stair':'3m rise / 3.75m run, 15x200mm risers and 250mm treads; mount beside the flight',
              'stairs_both_sides':'duplicate with lateral offset; no full-flight collider in the walkway',
              'wall_mount':'WallHandrail and StairHandrail have brackets and no floor posts',
              'shared_posts':'hide previous EndPost when next StartPost occupies the same socket; keep the final EndPost',
              'terminal_height':'scale a simple EndPost vertically by selected asset height / 1.1; TransitionPost supports joining distinct rail heights',
              'short_remainder':'trim/regenerate the final straight tile with capped rails; do not stretch a 2m tile along a whole bridge',
              'door_and_stair_landings':'split edge chains at access openings; keep thresholds and landings clear',
              'edge_offset_m':.06,'terminal_snap_tolerance_m':.001},
  'collision':{'method':'separate thin edge barrier; stair barrier follows rise; do not use entire railing AABB as a filled box',
               'clearance':'keep route width and door keepouts from parent building/street rules',
               'ends':'no collider across a gate or access opening'},
  'rendering':{'instancing_key':['revision','asset_id','material_style','boundary_post_mask'],
               'lod0':'full rail and infill','lod1':'keep posts and top rail; omit ornamental infill',
               'lod2':'edge silhouette or omit below pixel threshold',
               'frustum_culling':'per edge chunk, with model bounds','opaque_materials':True,
               'backface_culling':True,'shadow_budget':'nearby rails only; simplify distant shadow casters'},
  'asset_ids':[a['id'] for a in manifest['assets']]}
 (ROOT/'railing_rules.json').write_text(json.dumps(rules,indent=2)+'\n',encoding='utf8')
 # Link the optional pack while retaining all other runtime rule packs.
 building=ROOT.parent/'procedural_building/retro/rules'
 p=building/'building_rules.json';doc=json.loads(p.read_text(encoding='utf8'))
 doc.setdefault('runtime_rule_packs',{})['railings']='../../../railings_retro/railing_rules.json'
 p.write_text(json.dumps(doc,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 p=building/'building_rules.schema.json';schema=json.loads(p.read_text(encoding='utf8'))
 schema['properties']['runtime_rule_packs']['properties']['railings']={'type':'string'}
 p.write_text(json.dumps(schema,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print('RAILING_RULES_WRITTEN',len(rules['asset_ids']))

if __name__=='__main__':main()
