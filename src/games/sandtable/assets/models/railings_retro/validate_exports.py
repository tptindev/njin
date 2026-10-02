"""Manual GLB roundtrip check for the complete railings pack."""
from pathlib import Path
import hashlib,json,sys,bpy
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'procedural_building'))
from export_modules import bounds
from validate_exports import read_glb,delta

def main():
    manifest=json.loads((ROOT/'export_manifest.json').read_text(encoding='utf8'))
    source=json.loads((ROOT/'asset_manifest.json').read_text(encoding='utf8'))
    assert manifest['revision']==source['revision']
    assert len(manifest['assets'])==66 and {a['id'] for a in manifest['assets']}=={a['id'] for a in source['assets']}
    assert manifest['source_sha256']==hashlib.sha256((ROOT/manifest['source']).read_bytes()).hexdigest()
    for ob in list(bpy.data.objects):bpy.data.objects.remove(ob,do_unlink=True)
    for entry in manifest['assets']:
        path=ROOT/entry['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256']
        doc,data=read_glb(path)
        assert not doc.get('images') and not doc.get('skins') and not doc.get('animations')
        assert all('uri' not in b for b in doc.get('buffers',[]))
        pivot=next(n for n in doc['nodes'] if n.get('name')=='AssetRoot')
        assert pivot['extras']['asset_id']==entry['id'] and pivot.get('translation',[0,0,0])==[0,0,0]
        for node_name in entry['boundary_post_nodes'].values():
            if node_name:assert any(n.get('name')==node_name for n in doc['nodes'])
        bpy.ops.import_scene.gltf(filepath=str(path));bpy.context.view_layer.update()
        measured=bounds(bpy.context.scene.objects,bpy.context.evaluated_depsgraph_get())
        assert delta(measured,entry['bounds_m'])<.0001,entry['id']
        for ob in list(bpy.data.objects):bpy.data.objects.remove(ob,do_unlink=True)
    report={'passed':True,'revision':source['revision'],'assets_checked':66,'source_unchanged':True}
    (ROOT/'export_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    source['glb_export_run']=True;(ROOT/'asset_manifest.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf8')
    (ROOT/'source_status.json').write_text(json.dumps({'revision':source['revision'],'source_saved':True,
        'export_required':False,'export_validated':True,'glb_export_run':True},indent=2)+'\n',encoding='utf8')
    print('RAILINGS_GLB_VALIDATION_PASS',66,flush=True)

if __name__=='__main__':main()
