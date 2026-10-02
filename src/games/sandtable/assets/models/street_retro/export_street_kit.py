"""Manual-only GLB export. Run with the saved street_retro_kit.blend loaded."""
from pathlib import Path
import sys,json,hashlib
import bpy
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'procedural_building'))
from export_modules import materialize,bounds
def main():
    manifest=json.loads((ROOT/'asset_manifest.json').read_text(encoding='utf8'))
    original=bpy.context.scene;items=[]
    for entry in manifest['assets']:
        scene=bpy.data.scenes.new('Street_Export');bpy.context.window.scene=scene
        materialize(bpy.data.collections[entry['collection']],scene.collection)
        root=bpy.data.objects.new('AssetRoot',None);scene.collection.objects.link(root);root['asset_id']=entry['id'];root['revision']=manifest['revision']
        for ob in list(scene.objects):
            if ob!=root and ob.parent is None:
                world=ob.matrix_world.copy();ob.parent=root;ob.matrix_world=world
        path=ROOT/'modules'/(entry['id'].split('/')[-1]+'.glb');path.parent.mkdir(parents=True,exist_ok=True)
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_active_scene=True,export_yup=True,
            export_extras=True,export_tangents=True,export_animations=False,export_cameras=False,export_lights=False)
        items.append({**entry,'path':path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        bpy.context.window.scene=original;objects=list(scene.objects);bpy.data.scenes.remove(scene)
        for ob in objects:bpy.data.objects.remove(ob,do_unlink=True)
    out={'revision':manifest['revision'],'source_sha256':hashlib.sha256((ROOT/'street_retro_kit.blend').read_bytes()).hexdigest(),'units':'metre','axes':{'up':'+Y','front':'+Z'},'blender_to_gltf':'[x,z,-y]','assets':items}
    (ROOT/'export_manifest.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
    print('STREET_EXPORT_DONE',len(items),flush=True)
if __name__=='__main__':main()
