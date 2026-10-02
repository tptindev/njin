"""Rebuild shutters with restrained chamfers, retaining exact fitted geometry."""
from pathlib import Path
import sys,math,bpy
BASE=Path(__file__).resolve().parent;sys.path.insert(0,str(BASE))
from city_low_poly import apply_city_style
text=bpy.data.texts['PBK_Generator.py'];generator=text.as_string()
head,tail=generator.split('def build_window_shutters(c,curvature=None):',1)
body,end=tail.split('def ensure_window_shutters():',1)
body=body.replace('offset=.002,segments=1','offset=0.,segments=1').replace(',.031,6)',',.031,2)').replace(',.02,6)',',.02,2)').replace('offset=.004,segments=3','offset=.002,segments=1')
generator=head+'def build_window_shutters(c,curvature=None):'+body+'def ensure_window_shutters():'+end
# Refresh embedded optimizer so regeneration preserves fitted shutter geometry.
generator=generator.split('\n# City low-poly revision')[0]+'\n# City low-poly revision\n'+(BASE/'city_low_poly.py').read_text(encoding='utf8')+'''
_city_load_library=load_library
def load_library():
    result=_city_load_library()
    apply_city_style()
    return result
_city_generate_building=generate_building
def generate_building(*args,**kwargs):
    result=_city_generate_building(*args,**kwargs)
    apply_city_style()
    return result
'''
text.clear();text.write(generator);(BASE/'retro/generate.py').write_text(generator,encoding='utf8')
ns={'__name__':'city_shutter_repair','__file__':str(BASE/'retro/generate.py')};exec(generator,ns)
for name in ('PBK_Indochine_Window','PBK_Indochine_ArcWindow_R2_A45','PBK_Indochine_ArcWindow_R4_A30'):
    c=bpy.data.collections[name]
    for ob in list(c.objects):
        if ob.get('shutter_leaf') or ob.get('shutter_rig') or ob.get('shutter_hardware'):bpy.data.objects.remove(ob,do_unlink=True)
ns['ensure_window_shutters']()
links=[]
for scene in bpy.data.scenes:
    for child in list(scene.collection.children):
        links.append((scene,child));scene.collection.children.unlink(child)
apply_city_style()
for scene,child in links:scene.collection.children.link(child)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(BASE/'retro/modular_building_kit.blend'))
for ob in bpy.data.collections['PBK_Modern_Shopfront'].objects:
    if ob.type=='MESH':print('SHOP_MESH',ob.name,sum(len(p.vertices)-2 for p in ob.data.polygons),flush=True)
