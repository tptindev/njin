"""Manual-only GLB export; authoring scripts never call this exporter."""
from pathlib import Path
import hashlib,json,sys,bpy
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'procedural_building'))
from export_modules import materialize,bounds

def main():
    source=ROOT/'railings_retro_kit.blend'
    assert Path(bpy.data.filepath).resolve()==source.resolve(),'Load saved railing source first'
    catalog=json.loads((ROOT/'asset_manifest.json').read_text(encoding='utf8'))
    original=bpy.context.scene;entries=[]
    for entry in catalog['assets']:
        scene=bpy.data.scenes.new('Railing_Export');bpy.context.window.scene=scene
        materialize(bpy.data.collections[entry['collection']],scene.collection)
        root=bpy.data.objects.new('AssetRoot',None);root['asset_id']=entry['id'];root['revision']=catalog['revision'];scene.collection.objects.link(root)
        for ob in list(scene.objects):
            if ob!=root and ob.parent is None:ob.parent=root
        # Materialized copies receive Blender's numeric name suffix while the
        # source objects remain loaded. Record the actual exported node names.
        posts={}
        for side,name in entry['boundary_post_nodes'].items():
            posts[side]=next((o.name for o in scene.objects if name and
                             (o.name==name or o.name.startswith(name+'.'))),None)
            assert not name or posts[side],(entry['id'],side,'post node missing')
        path=ROOT/entry['path'];path.parent.mkdir(parents=True,exist_ok=True)
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_active_scene=True,
            export_yup=True,export_extras=True,export_animations=False,export_tangents=False,export_lights=False,export_cameras=False)
        entries.append({**entry,'boundary_post_nodes':posts,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size,
                        'sockets_gltf':{k:[v[0],v[2],-v[1]] for k,v in entry['sockets'].items()}})
        bpy.context.window.scene=original;objects=list(scene.objects);bpy.data.scenes.remove(scene)
        for ob in objects:bpy.data.objects.remove(ob,do_unlink=True)
    result={'revision':catalog['revision'],'source':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'units':'metre','axes':{'up':'+Y','front':'+Z'},'assets':entries}
    (ROOT/'export_manifest.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print('RAILINGS_EXPORTED',len(entries),flush=True)

if __name__=='__main__':main()
