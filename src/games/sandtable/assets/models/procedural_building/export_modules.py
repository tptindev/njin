"""Blender 4.4 background export; preserves the source .blend and module origins.

blender -b modular_building_kit.blend --python export_modules.py
Optional arguments after --: --limit 3
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Matrix, Euler

ROOT = Path(__file__).resolve().parent / 'retro'
OUT = ROOT / 'modules'


def bounds(objects, depsgraph):
    points = []
    for ob in objects:
        if ob.type != 'MESH':
            continue
        ev = ob.evaluated_get(depsgraph)
        mesh = ev.to_mesh()
        points.extend(ev.matrix_world @ v.co for v in mesh.vertices)
        ev.to_mesh_clear()
    return {k: [round(fn(p[i] for p in points), 6) for i in range(3)]
            for k, fn in [('min', min), ('max', max)]}


def materialize(collection, destination, transform=Matrix.Identity(4)):
    """Expand nested collection instances, retaining real mesh/armature pairs."""
    clones = {}
    for source in list(collection.all_objects):
        if source.type == 'EMPTY' and source.instance_collection:
            instance_matrix = transform @ source.matrix_world @ Matrix.Translation(-source.instance_collection.instance_offset)
            materialize(source.instance_collection, destination, instance_matrix)
            continue
        if source.type not in {'MESH', 'ARMATURE', 'EMPTY'}:
            continue
        ob = source.copy()
        if source.type == 'ARMATURE':
            ob.data = source.data.copy()
        destination.objects.link(ob)
        ob.hide_viewport = False
        ob.hide_render = False
        clones[source] = ob
    for source, ob in clones.items():
        ob.parent = clones.get(source.parent)
        ob.matrix_world = transform @ source.matrix_world
        for mod in ob.modifiers:
            if mod.type == 'ARMATURE':
                if mod.object not in clones:
                    raise RuntimeError('Armature outside module: ' + source.name)
                mod.object = clones[mod.object]
        if ob.animation_data:
            for fc in ob.animation_data.drivers:
                for variable in fc.driver.variables:
                    for target in variable.targets:
                        if target.id in clones:
                            target.id = clones[target.id]


def bake_door(scene, rig):
    # Export real bone rotation, not the Blender-only open_amount driver.
    bone = rig.pose.bones['Door_Hinge']
    curve = next(fc for fc in rig.animation_data.action.fcurves if fc.data_path == '["open_amount"]')
    driver = next(fc.driver for fc in rig.animation_data.drivers
                  if fc.data_path == 'pose.bones["Door_Hinge"].rotation_euler' and fc.array_index == 1)
    if driver.expression != 'open_amount * 1.5707963267948966':
        raise RuntimeError('Unsupported door driver; update baker explicitly')
    samples = [Euler((0, curve.evaluate(frame) * math.pi / 2, 0), 'XYZ').to_quaternion()
               for frame in range(1, 97)]
    if samples[0].rotation_difference(samples[23]).angle < math.radians(89):
        raise RuntimeError('Door driver did not evaluate: ' + rig.name)
    rig.animation_data_clear()
    bone.rotation_mode = 'QUATERNION'
    for frame, rotation in enumerate(samples, 1):
        bone.rotation_quaternion = rotation
        bone.keyframe_insert(data_path='rotation_quaternion', frame=frame, group='Door_Hinge')
    rig.animation_data.action.name = 'Door_OpenClose'
    scene.frame_set(1)
    bpy.context.view_layer.update()


def convert_point(p):
    return [p[0], p[2], -p[1]]


def main():
    global ROOT, OUT
    parser = argparse.ArgumentParser()
    parser.add_argument('--limit', type=int)
    parser.add_argument('--root', type=Path, help='Alternate kit directory, e.g. retro')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if args.root:
        ROOT = args.root.resolve()
        OUT = ROOT / 'modules'
    manifest = json.loads((ROOT / 'kit_manifest.json').read_text(encoding='utf8'))
    entries = sorted((m for m in manifest['modules'] if m['asset']), key=lambda m: m['id'])
    if args.limit:
        entries = entries[:args.limit]
    result = {'schema': 'sandtable.module-exports.v1', 'kit_revision': manifest['revision'],
              'visual_variant': manifest.get('visual_variant', 'architectural'),
              'source': 'modular_building_kit.blend', 'source_sha256': hashlib.sha256((ROOT / 'modular_building_kit.blend').read_bytes()).hexdigest(),
              'format': 'glTF 2.0 binary', 'units': 'metre', 'axes': {'up': '+Y', 'front': '+Z', 'width': '+X'},
              'blender_to_gltf': '[x,z,-y]', 'root_pivot': [0, 0, 0],
              'engine_render_scale': 6 / 32, 'collision': 'not included; derive structural/door collision separately',
              'modules': []}
    original_scene = bpy.context.scene
    # Remove the large display/building scenes from this disposable process only.
    # This prevents every sampled frame from evaluating thousands of demo objects.
    for existing_scene in bpy.data.scenes:
        for child in list(existing_scene.collection.children):
            existing_scene.collection.children.unlink(child)
        for ob in list(existing_scene.collection.objects):
            existing_scene.collection.objects.unlink(ob)
    for index, entry in enumerate(entries, 1):
        print('PBK_PREPARE', entry['id'], flush=True)
        source = bpy.data.collections.get(entry['collection'])
        if not source:
            raise RuntimeError('Missing source: ' + entry['collection'])
        scene = bpy.data.scenes.new('Door_OpenClose')
        scene.render.fps = 24
        scene.frame_start = 1
        scene.frame_end = 96
        bpy.context.window.scene = scene
        materialize(source, scene.collection)
        print('PBK_MATERIALIZED', len(scene.objects), flush=True)
        bpy.context.view_layer.update()
        rigs = [o for o in scene.objects if o.type == 'ARMATURE']
        for rig in rigs:
            if not rig.get('shutter_rig'):
                bake_door(scene, rig)
        shutter_animation = bool(rigs) and all(o.get('shutter_rig') for o in rigs)
        clip_name = 'Shutter_CloseOpen' if shutter_animation else 'Door_OpenClose'
        scene.name = clip_name
        print('PBK_BAKED', len(rigs), flush=True)
        root = bpy.data.objects.new('ModuleRoot', None)
        root['module_id'] = entry['id']
        root['kit_revision'] = manifest['revision']
        scene.collection.objects.link(root)
        for ob in list(scene.objects):
            if ob != root and ob.parent is None:
                world = ob.matrix_world.copy()
                ob.parent = root
                ob.matrix_world = world
        scene.frame_set(1)
        bpy.context.view_layer.update()
        closed_bounds = bounds(scene.objects, bpy.context.evaluated_depsgraph_get())
        door_samples = []
        for frame in ([1, 24, 48, 72, 96] if rigs else []):
            scene.frame_set(frame)
            bpy.context.view_layer.update()
            leafs = [o for o in scene.objects if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
            door_samples.append({'frame': frame, 'time_s': (frame - 1) / 24,
                                 'leaf_bounds_blender_m': bounds(leafs, bpy.context.evaluated_depsgraph_get())})
        scene.frame_set(1)
        path = OUT / (entry['id'] + '.glb')
        path.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_active_scene=True,
            export_yup=True, export_extras=True, export_cameras=False, export_lights=False,
            export_tangents=manifest.get('visual_variant') == 'handmade_retro',
            export_animations=bool(rigs), export_animation_mode='SCENE', export_anim_scene_split_object=False,
            export_nla_strips_merged_animation_name=clip_name, export_frame_range=True,
            export_anim_slide_to_zero=True, export_force_sampling=True, export_frame_step=1,
            export_skins=True, export_apply=False, export_current_frame=False,
            export_optimize_animation_size=False)
        item = copy.deepcopy(entry)
        item.update(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    pivot_gltf_m=[0, 0, 0], sockets_gltf_m={k: convert_point(v) for k, v in entry['sockets'].items()},
                    closed_bounds_blender_m=closed_bounds, door_pose_samples=door_samples,
                    animation={'name': clip_name, 'fps': 24, 'start_time_s': 0,
                               'duration_s': 95 / 24,
                               'closed_time_s': 23 / 24 if shutter_animation else 0,
                               'open_time_s': 0 if shutter_animation else 23 / 24,
                               'open_angle_degrees': 115 if shutter_animation else 90,
                               'rig_count': len(rigs), 'loop_by_default': False} if rigs else None)
        # Bounds transform must swap min/max for the negated coordinate.
        lo, hi = closed_bounds['min'], closed_bounds['max']
        item['bounds_gltf_m'] = {'min': [lo[0], lo[2], -hi[1]], 'max': [hi[0], hi[2], -lo[1]]}
        item['connection_orientation_gltf'] = {'axis': '+Y', 'angle_sign_from_blender_z': 1,
                                              'source_degrees': entry.get('connection_orientation', {})}
        result['modules'].append(item)
        print(f'PBK_EXPORT {index}/{len(entries)} {entry["id"]}', flush=True)
        bpy.context.window.scene = original_scene
        objects = list(scene.objects)
        bpy.data.scenes.remove(scene)
        for ob in objects:
            bpy.data.objects.remove(ob, do_unlink=True)
    result['module_count'] = len(result['modules'])
    filename = 'export_manifest.preview.json' if args.limit else 'export_manifest.json'
    (ROOT / filename).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print('PBK_EXPORT_DONE', result['module_count'], filename, flush=True)


if __name__ == '__main__':
    main()
