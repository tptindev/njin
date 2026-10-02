"""Package validated building, street and railing GLBs with their rule packs."""
from pathlib import Path
import hashlib,json,zipfile
ROOT=Path(__file__).resolve().parents[1]
BUILDING=ROOT/'assets/models/procedural_building/retro'
STREET=ROOT/'assets/models/street_retro'
RAILINGS=ROOT/'assets/models/railings_retro'
files=[];count=0
for folder,prefix,list_key,report_count in [(BUILDING,'building','modules','modules_checked'),(STREET,'street','assets','assets_checked'),(RAILINGS,'railings','assets','assets_checked')]:
    assert (folder/'export_manifest.json').exists(),f'Export and validate {prefix} manually before packaging'
    manifest=json.loads((folder/'export_manifest.json').read_text(encoding='utf8'))
    report=json.loads((folder/'export_validation.json').read_text(encoding='utf8'))
    assert report['passed'] and report[report_count]==len(manifest[list_key])
    source=folder/({'building':'modular_building_kit.blend','street':'street_retro_kit.blend','railings':'railings_retro_kit.blend'}[prefix])
    assert hashlib.sha256(source.read_bytes()).hexdigest()==manifest['source_sha256']
    for entry in manifest[list_key]:
        path=folder/entry['path']
        assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256']
        files.append((path,prefix+'/'+entry['path']))
    count+=len(manifest[list_key])
    for name in ('export_manifest.json','export_validation.json','README.vi.md'):
        files.append((folder/name,prefix+'/'+name))
    if prefix=='building':
        files.append((folder/'kit_manifest.json',prefix+'/kit_manifest.json'))
        files.extend((p,prefix+'/'+p.relative_to(folder).as_posix()) for p in (folder/'rules').rglob('*') if p.is_file())
    elif prefix=='street':
        files.extend((folder/name,prefix+'/'+name) for name in ('asset_manifest.json','sidewalk_layout.json','distribution_rules.json','RULES.vi.md','runtime_rules_validation.json'))
    else:
        files.extend((folder/name,prefix+'/'+name) for name in ('asset_manifest.json','railing_rules.json','validation.json'))
assert count==196
output=ROOT/'assets/models/retro_game_kit_glb.zip'
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path,name in sorted(files):
        if name=='building/rules/building_rules.json':
            portable=json.loads(path.read_text(encoding='utf8'))
            portable['runtime_rule_packs']['street_distribution']='../../street/distribution_rules.json'
            portable['runtime_rule_packs']['railings']='../../railings/railing_rules.json'
            archive.writestr(name,json.dumps(portable,ensure_ascii=False,indent=2))
        elif name=='building/rules/README.vi.md':
            archive.writestr(name,path.read_text(encoding='utf8').replace('../../../street_retro/','../../street/'))
        elif name=='street/RULES.vi.md':
            archive.writestr(name,path.read_text(encoding='utf8').replace('../procedural_building/retro/','../building/'))
        else:archive.write(path,name)
    archive.write(ROOT/'tools/retro_rule_reference.py','scripts/retro_rule_reference.py')
    archive.write(RAILINGS/'rule_reference.py','scripts/railings_rule_reference.py')
    archive.writestr('README.txt','196 retro GLBs: 105 building modules + 25 street props + 66 railings.\nUse each pack\'s export_manifest.json; paths are relative to its manifest.\nUnits: metres; up +Y. Source rail socket coordinates are Blender XYZ; railings also include sockets_gltf.\nFlat colours, opaque materials, no embedded textures.\nAnimated entrance doors: Door_OpenClose. Animated wooden shutters: Shutter_CloseOpen.\nNo colliders/navmesh included.\n')
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    assert sum(name.endswith('.glb') for name in archive.namelist())==count
    import posixpath
    rule_path='building/rules/building_rules.json'
    links=json.loads(archive.read(rule_path))['runtime_rule_packs']
    for key in ('street_distribution','window_shutters','interior_lighting','railings'):
        if key not in links:continue
        target=posixpath.normpath(posixpath.join(posixpath.dirname(rule_path),links[key]))
        assert target in archive.namelist(),target
print(json.dumps({'archive':str(output),'modules':count,'bytes':output.stat().st_size}))
