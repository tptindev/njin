"""Package only the current retro catalog; excludes backups and authoring temp files."""
import json
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[1]/'assets/models/procedural_building/retro'
manifest=json.loads((ROOT/'export_manifest.json').read_text(encoding='utf8'))
report=json.loads((ROOT/'export_validation.json').read_text(encoding='utf8'))
kit=json.loads((ROOT/'kit_manifest.json').read_text(encoding='utf8'))
assert manifest['kit_revision']==kit['revision'], 'Run manual export for the current source revision first'
assert report['passed'] and report['modules_checked']==105
for name in ('door_validation.json','generator_validation.json','rules/rules_validation.json'):
    assert json.loads((ROOT/name).read_text(encoding='utf8'))['passed'],name
files=[ROOT/entry['path'] for entry in manifest['modules']]
files += [ROOT/name for name in ('README.vi.md','art_direction.json','kit_manifest.json','export_manifest.json',
          'export_validation.json','door_validation.json','generator_validation.json')]
files += [p for p in (ROOT/'retro_full_kit.png',) if p.exists()]
files += [p for folder in ('textures','rules') for p in (ROOT/folder).rglob('*') if p.is_file()]
path=ROOT/'retro_module_kit_glb.zip'
with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for p in sorted(files):archive.write(p,p.relative_to(ROOT).as_posix())
with zipfile.ZipFile(path) as archive:
    assert archive.testzip() is None
    assert sum(n.endswith('.glb') for n in archive.namelist())==105
print(json.dumps({'zip':str(path),'bytes':path.stat().st_size,'modules':105,'validation':True}))
