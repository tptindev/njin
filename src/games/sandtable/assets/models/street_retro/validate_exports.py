"""Re-import all street GLBs and check geometry, resources and manifests."""
from pathlib import Path
import hashlib,json,sys
import bpy
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'procedural_building'))
from export_modules import bounds
from validate_exports import read_glb,delta
manifest=json.loads((ROOT/'export_manifest.json').read_text(encoding='utf8'))
source=json.loads((ROOT/'asset_manifest.json').read_text(encoding='utf8'))
errors=[];results=[]
if {a['id'] for a in manifest['assets']}!={a['id'] for a in source['assets']}:errors.append('asset set mismatch')
if manifest['revision']!=source['revision']:errors.append('revision mismatch')
if hashlib.sha256((ROOT/'street_retro_kit.blend').read_bytes()).hexdigest()!=manifest['source_sha256']:errors.append('source hash mismatch')
for ob in list(bpy.data.objects):bpy.data.objects.remove(ob,do_unlink=True)
for entry in manifest['assets']:
    row={'id':entry['id'],'passed':True}
    try:
        path=ROOT/entry['path']
        assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256'],'hash mismatch'
        gltf,binary=read_glb(path)
        assert all('uri' not in b for b in gltf.get('buffers',[])),'external buffer'
        assert all('uri' not in i for i in gltf.get('images',[])),'external texture'
        if not source.get('visual_profile','').startswith('city-'):assert gltf.get('images'),'textures missing'
        for view in gltf.get('bufferViews',[]):assert view.get('byteOffset',0)+view['byteLength']<=len(binary),'buffer overrun'
        root=next(n for n in gltf['nodes'] if n.get('name')=='AssetRoot')
        assert root['extras']['asset_id']==entry['id'],'wrong ID'
        assert root.get('translation',[0,0,0])==[0,0,0],'pivot moved'
        assert not gltf.get('animations') and not gltf.get('skins'),'unexpected animation'
        for mesh in gltf['meshes']:
            for primitive in mesh['primitives']:
                material=gltf['materials'][primitive['material']]
                if material.get('normalTexture'):
                    assert 'TEXCOORD_0' in primitive['attributes'] and 'TANGENT' in primitive['attributes'],'UV/tangents missing'
        bpy.ops.import_scene.gltf(filepath=str(path))
        bpy.context.view_layer.update()
        measured=bounds(bpy.context.scene.objects,bpy.context.evaluated_depsgraph_get())
        row['roundtrip_bounds_error_m']=delta(measured,entry['bounds_m'])
        assert row['roundtrip_bounds_error_m']<.0001,'geometry bounds changed'
    except Exception as exc:
        row['passed']=False;row['error']=str(exc);errors.append(entry['id']+': '+str(exc))
    finally:
        for ob in list(bpy.data.objects):bpy.data.objects.remove(ob,do_unlink=True)
    results.append(row)
report={'passed':not errors,'assets_checked':len(results),'revision':manifest['revision'],'results':results,'errors':errors,'runtime_pending':['game importer/rendering','collision/navmesh']}
(ROOT/'export_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('STREET_EXPORT_VALIDATION',report['passed'],len(results),errors,flush=True)
if errors:raise SystemExit(1)
