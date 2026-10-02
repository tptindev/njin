"""Keep curved facade bands as clean, closed strips rather than box approximations."""
from pathlib import Path
import math,bpy
BASE=Path(__file__).resolve().parent
text=bpy.data.texts['PBK_Generator.py'];gen=text.as_string()
head,tail=gen.split('\n# City low-poly revision')
wrappers=tail[tail.index('_city_load_library='):]
gen=head+'\n# City low-poly revision\n'+(BASE/'city_low_poly.py').read_text(encoding='utf8')+'\n'+wrappers
text.clear();text.write(gen);(BASE/'retro/generate.py').write_text(gen,encoding='utf8')
ns={'__name__':'city_arc_repair','__file__':str(BASE/'retro/generate.py')};exec(gen,ns)
for c in list(bpy.data.collections):
    if not c.asset_data or '_Arc' not in c.name or not c.get('radius_m'):continue
    radius=float(c['radius_m']);angle=math.radians(float(c['angle_degrees']))
    part=c.name.split('_')[2][3:];style=c.name.split('_')[1]
    if part in ('Wall','Window','Door','Shopfront'):name,z,h,key,outer,inner='floor_band',2.84,.16,'trim',-.065,.025
    elif part in ('Band','Parapet','Coping'):
        name=part
        z,h,key,outer,inner={'Band':(2.84,.16,'trim',-.065,.025),'Parapet':(0,.55,'wall',0,.2),'Coping':(.55,.1,'trim',-.06,.25)}[part]
    else:continue
    for ob in list(c.objects):
        if ob.type=='MESH' and name.lower() in ob.name.lower():bpy.data.objects.remove(ob,do_unlink=True)
    ob=ns['arc_strip'](c,name,radius,angle,z,h,bpy.data.materials['PBK_'+style+'_'+key],outer,inner)
    ob['curved_dressing']=True;ob.data['city_low_poly']='city-low-poly-v2'
    dressing=bpy.data.collections.get(c.name+'_Dressing')
    if dressing and part not in ('Wall','Window','Door','Shopfront'):dressing.objects.link(ob)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(BASE/'retro/modular_building_kit.blend'))
