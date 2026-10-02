"""Re-import every exported GLB in a disposable Blender process and measure it.

blender --factory-startup -b --python-exit-code 1 --python validate_exports.py
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import argparse
import tempfile
import bpy
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_modules import bounds

ROOT = Path(__file__).resolve().parent / 'retro'


def read_glb(path):
    data = path.read_bytes()
    magic, version, length = struct.unpack_from('<III', data)
    assert magic == 0x46546c67 and version == 2 and length == len(data), 'invalid GLB header'
    cursor = 12
    chunks = {}
    while cursor < length:
        size, kind = struct.unpack_from('<II', data, cursor)
        cursor += 8
        assert size % 4 == 0 and cursor + size <= length, 'invalid chunk'
        chunks[kind] = data[cursor:cursor + size]
        cursor += size
    return json.loads(chunks[0x4e4f534a]), chunks.get(0x004e4942, b'')


def delta(a, b):
    return max(abs(a[k][i] - b[k][i]) for k in ('min', 'max') for i in range(3))


_metal_texture_cache = {}
def nonmetal_pbr(pbr, gltf, binary):
    """glTF metallic = factor * texture blue; omitted factor defaults to one."""
    factor = pbr.get('metallicFactor', 1)
    if factor == 0:
        return True
    texture = pbr.get('metallicRoughnessTexture')
    if not texture:
        return False
    image = gltf['images'][gltf['textures'][texture['index']]['source']]
    view = gltf['bufferViews'][image['bufferView']]
    start = view.get('byteOffset', 0)
    encoded = binary[start:start + view['byteLength']]
    digest = hashlib.sha256(encoded).hexdigest()
    if digest not in _metal_texture_cache:
        with tempfile.TemporaryDirectory(prefix='pbk-orm-') as folder:
            path = Path(folder) / 'orm.png'
            path.write_bytes(encoded)
            decoded = bpy.data.images.load(str(path), check_existing=False)
            try:
                decoded.colorspace_settings.name = 'Non-Color'
                _metal_texture_cache[digest] = max(tuple(decoded.pixels)[2::4])
            finally:
                bpy.data.images.remove(decoded)
    return factor * _metal_texture_cache[digest] < .001


def main():
    global ROOT
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if args.root:
        ROOT = args.root.resolve()
    manifest = json.loads((ROOT / 'export_manifest.json').read_text(encoding='utf8'))
    source = json.loads((ROOT / 'kit_manifest.json').read_text(encoding='utf8'))
    expected = {m['id'] for m in source['modules'] if m['asset']}
    errors = []
    if {m['id'] for m in manifest['modules']} != expected:
        errors.append('active asset set mismatch')
    scene = bpy.context.scene
    scene.render.fps = 24
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    results = []
    for entry in manifest['modules']:
        item = {'id': entry['id'], 'passed': True, 'errors': []}
        try:
            path = ROOT / entry['path']
            assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256'], 'hash mismatch'
            gltf, binary = read_glb(path)
            assert gltf['asset']['version'] == '2.0'
            assert all('uri' not in b for b in gltf.get('buffers', [])), 'external buffer'
            assert all('uri' not in i for i in gltf.get('images', [])), 'external image'
            if source.get('visual_variant') == 'handmade_retro' and not source.get('visual_profile','').startswith('city-low-poly-'):
                assert gltf.get('images'), 'retro textures missing'
                for material in gltf.get('materials', []):
                    pbr = material.get('pbrMetallicRoughness', {})
                    role = material.get('extras', {}).get('surface_role')
                    if role not in {'glass', 'metal'}:
                        assert nonmetal_pbr(pbr, gltf, binary), 'nonmetal surface unexpectedly metallic'
                        assert material.get('normalTexture'), 'surface normal missing'
                        assert pbr.get('metallicRoughnessTexture'), 'surface roughness texture missing'
                    if role == 'metal':
                        assert pbr.get('metallicFactor', 0) >= .8, 'metal accent lost'
                    if role == 'glass':
                        assert material.get('alphaMode', 'OPAQUE') == 'OPAQUE', 'glass must be opaque'
                        assert abs(material.get('pbrMetallicRoughness', {}).get('baseColorFactor', [1,1,1,1])[3] - 1.) < 1e-5, 'glass opacity must be 100%'
                for mesh in gltf['meshes']:
                    for primitive in mesh['primitives']:
                        mat = gltf['materials'][primitive['material']]
                        if mat.get('normalTexture'):
                            assert 'TEXCOORD_0' in primitive['attributes'] and 'TANGENT' in primitive['attributes'], 'textured surface UV/tangents missing'
            for view in gltf.get('bufferViews', []):
                assert view.get('byteOffset', 0) + view['byteLength'] <= len(binary), 'buffer overrun'
            root = next(n for n in gltf['nodes'] if n.get('name') == 'ModuleRoot')
            assert root.get('translation', [0, 0, 0]) == [0, 0, 0], 'moved pivot'
            assert root.get('rotation', [0, 0, 0, 1]) == [0, 0, 0, 1], 'rotated pivot'
            assert root.get('scale', [1, 1, 1]) == [1, 1, 1], 'scaled pivot'
            assert root['extras']['module_id'] == entry['id']
            if entry['animated']:
                assert len(gltf.get('skins', [])) == entry['animation'].get('rig_count', 1), 'missing skin'
                assert len(gltf.get('animations', [])) == 1, 'missing/surplus animation'
                animation = gltf['animations'][0]
                assert animation['name'] == entry['animation']['name'], 'clip name mismatch'
                assert any(c['target']['path'] == 'rotation' for c in animation['channels']), 'no bone rotation channel'
                samplers = animation['samplers']
                times = [gltf['accessors'][s['input']] for s in samplers]
                assert min(a['min'][0] for a in times) == 0, 'animation not zero-based'
                assert abs(max(a['max'][0] for a in times) - 95 / 24) < 1e-5, 'wrong clip duration'
            else:
                assert not gltf.get('skins') and not gltf.get('animations'), 'unexpected skin/clip'
            bpy.ops.import_scene.gltf(filepath=str(path))
            # glTF importer adds a hidden bone-display Icosphere; it is not asset geometry.
            imported = [o for o in scene.objects if not any(c.name.startswith('glTF_not_exported') for c in o.users_collection)]
            scene.frame_set(0)
            bpy.context.view_layer.update()
            measured = bounds(imported, bpy.context.evaluated_depsgraph_get())
            error = delta(measured, entry['closed_bounds_blender_m'])
            item['roundtrip_bounds_error_m'] = error
            assert error < .0001, f'closed geometry changed: {error}'
            # Compare independently baked/imported leaf poses at all five control times.
            if entry['animated']:
                leafs = [o for o in imported if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
                assert leafs, 'no imported skinned leaf'
                worst = 0
                for pose in entry['door_pose_samples']:
                    scene.frame_set(pose['frame'] - 1)
                    bpy.context.view_layer.update()
                    actual = bounds(leafs, bpy.context.evaluated_depsgraph_get())
                    error = delta(actual, pose['leaf_bounds_blender_m'])
                    worst = max(worst, error)
                    assert error < .0001, f'door pose changed at frame {pose["frame"]}: {error}'
                item['door_pose_bounds_error_m'] = worst
                item['door_poses_checked'] = len(entry['door_pose_samples'])
                rig = next(o for o in imported if o.type == 'ARMATURE')
                scene.frame_set(0)
                bone_name = 'Shutter_Left' if entry.get('animation_kind') == 'window_shutters' else 'Door_Hinge'
                closed = rig.pose.bones[bone_name].matrix_basis.to_quaternion().copy()
                scene.frame_set(23)
                opened = rig.pose.bones[bone_name].matrix_basis.to_quaternion().copy()
                angle = math.degrees(closed.rotation_difference(opened).angle)
                item['opening_angle_degrees'] = angle
                assert abs(angle - entry['animation']['open_angle_degrees']) < .01, 'incorrect hinge rotation'
            # The canonical catalog's closed bounds must also match the exported copy.
            catalog_error = delta(entry['closed_bounds_blender_m'], entry['bounds_m'])
            item['catalog_bounds_error_m'] = catalog_error
            assert catalog_error < .0001, f'canonical pivot/geometry mismatch: {catalog_error}'
        except Exception as exc:
            item['passed'] = False
            item['errors'].append(str(exc))
            errors.append(entry['id'] + ': ' + str(exc))
        finally:
            for ob in list(scene.objects):
                bpy.data.objects.remove(ob, do_unlink=True)
        results.append(item)
        print('PBK_VALIDATE', entry['id'], item['passed'], item['errors'], flush=True)
    if hashlib.sha256((ROOT / manifest['source']).read_bytes()).hexdigest() != manifest['source_sha256']:
        errors.append('source .blend changed')
    report = {'passed': not errors, 'modules_checked': len(results),
              'visual_variant': source.get('visual_variant', 'architectural'),
              'animated_modules': sum(bool(m['animated']) for m in manifest['modules']),
              'door_poses_checked': sum(r.get('door_poses_checked', 0) for r in results),
              'total_bytes': sum(m['bytes'] for m in manifest['modules']),
              'scope': ['GLB container and embedded resources', 'hashes and complete active asset set',
                        'root pivot', 'all geometry closed bounds after Blender re-import',
                        'skinned door pose bounds at five times and 90-degree rotation', 'source file unchanged'],
              'runtime_pending': ['game importer/rendering', 'collision/navmesh/LOD'],
              'results': results, 'errors': errors}
    if source.get('visual_variant') == 'handmade_retro' and not source.get('visual_profile','').startswith('city-low-poly-'):
        report['scope'].append('UV/tangents and embedded retro normal/roughness textures')
    (ROOT / 'export_validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print('PBK_VALIDATION_DONE', report['passed'], len(results), errors, flush=True)
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
