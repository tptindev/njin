"""Check balcony detail bounds, topology, dressing links and generator reuse."""
from pathlib import Path
import json, sys, bpy, bmesh
ROOT = Path(__file__).resolve().parent / 'retro'
sys.path.insert(0, str(ROOT.parent))
from retro_low_poly import apply_retro_style

def check():
    rows = []
    for style in ('Indochine', 'Modern', 'Brick'):
        c = bpy.data.collections['PBK_' + style + '_Balcony']
        details = [o for o in c.objects if o.type == 'MESH' and o.data.get('balcony_detail_low_poly')]
        assert len(details) == 18, (style, len(details))
        for ob in details:
            data = ob.data
            assert sum(len(p.vertices)-2 for p in data.polygons) == 12
            original = json.loads(data['balcony_detail_bounds_json'])
            for i in range(3):
                assert abs(min(v.co[i] for v in data.vertices)-original['min'][i]) < 1e-6
                assert abs(max(v.co[i] for v in data.vertices)-original['max'][i]) < 1e-6
            bm = bmesh.new(); bm.from_mesh(data)
            assert all(e.is_manifold for e in bm.edges)
            assert bm.calc_volume(signed=True) > 0
            bm.free()
            assert data.materials[0] is not None
            assert ob in bpy.data.collections[c.name+'_Dressing'].objects[:]
        glasses = [o for o in c.objects if o.type == 'MESH' and 'glass' in o.name.lower()]
        assert len(glasses) == 2
        assert not any(o.get('window_crossbar') for o in c.objects)
        triangles = sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in c.objects if o.type == 'MESH')
        rows.append({'id': style+'/Balcony', 'before': int(c['balcony_optimization_baseline']),
                     'after': triangles, 'details': len(details)})
    return rows

rows = check()
apply_retro_style(); apply_retro_style()
assert check() == rows
ns = {'__name__': 'balcony_generator_test', '__file__': str(ROOT/'generate.py')}
exec(bpy.data.texts['PBK_Generator.py'].as_string(), ns)
ns['load_library']()
assert check() == rows
assert len([c for c in bpy.data.collections if c.asset_data]) == 105
report = {'passed': True, 'revision': 'unified-kit-v3-retro-v18-low-poly-balcony',
          'balconies': rows, 'exact_detail_bounds': True, 'closed_details': True,
          'dressing_linked': True, 'idempotent': True, 'generator_reopened': True,
          'export_run': False}
(ROOT/'balcony_validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
print('BALCONY_VALIDATION_PASS', json.dumps(report), flush=True)
