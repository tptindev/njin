"""Author textured PBR accents, pack them and save Blender source only."""
from pathlib import Path
import json,sys
import bpy
import numpy as np
BASE=Path(__file__).resolve().parent;ROOT=BASE/'retro'
sys.path.insert(0,str(BASE))
from retro_accent_materials import apply_accent_materials
REV='unified-kit-v3-retro-v3-materials'

def image(name,pixels,color_space):
    n=pixels.shape[0]
    im=bpy.data.images.get(name) or bpy.data.images.new(name,n,n,alpha=True)
    im.colorspace_settings.name=color_space
    im.pixels.foreach_set(pixels.astype(np.float32).ravel())
    im.filepath_raw=str(ROOT/'textures'/(name+'.png'));im.file_format='PNG'
    im.save();im.pack();return im

def shader(mat,color,roughness,metallic=0,transmission=0):
    mat.use_nodes=True;nodes=mat.node_tree.nodes;nodes.clear()
    out=nodes.new('ShaderNodeOutputMaterial');bs=nodes.new('ShaderNodeBsdfPrincipled')
    mat.node_tree.links.new(bs.outputs['BSDF'],out.inputs['Surface'])
    bs.inputs['Base Color'].default_value=(*color,1)
    bs.inputs['Roughness'].default_value=roughness;bs.inputs['Metallic'].default_value=metallic
    bs.inputs['Transmission Weight'].default_value=transmission
    bs.inputs['IOR'].default_value=1.45
    mat.diffuse_color=(*color,1);mat['accent_material']=True
    return bs

n=512;y,x=np.mgrid[0:n,0:n]/n
grain=np.sin(2*np.pi*(x*24+.38*np.sin(2*np.pi*y)+.13*np.sin(2*np.pi*y*3)))
grain+=.35*np.sin(2*np.pi*(x*67+.20*np.sin(2*np.pi*y*2)))
grain=grain/1.35
rgba=np.ones((n,n,4));rgba[:,:,:3]=(.63+grain[:,:,None]*.12)
wood_color=image('Wood_Grain_BaseColor',rgba,'sRGB')
orm=np.ones((n,n,4));orm[:,:,1]=.43+grain*.045;orm[:,:,2]=0
wood_orm=image('Wood_Grain_ORM',orm,'Non-Color')
dx=(np.roll(grain,-1,1)-np.roll(grain,1,1))*.055
dy=(np.roll(grain,-1,0)-np.roll(grain,1,0))*.055
norm=np.stack((-dx,-dy,np.ones_like(dx)),axis=-1);norm/=np.linalg.norm(norm,axis=-1)[:,:,None]
rgba[:,:,:3]=norm*.5+.5
wood_normal=image('Wood_Grain_Normal',rgba,'Non-Color')
report={'revision':REV,'materials':[],'counts':{},'export_run':False}
for style,tint in [('Indochine',(.38,.21,.095)),('Modern',(.22,.115,.055)),('Brick',(.29,.14,.065))]:
    for suffix in ('wood','frame'):
        name='PBK_'+style+'_'+suffix
        mat=bpy.data.materials.get(name) or bpy.data.materials.new(name)
        bs=shader(mat,tint,.43);nodes=mat.node_tree.nodes;links=mat.node_tree.links
        tex=nodes.new('ShaderNodeTexImage');tex.image=wood_color
        multiply=nodes.new('ShaderNodeMixRGB');multiply.blend_type='MULTIPLY';multiply.inputs[0].default_value=1
        multiply.inputs[2].default_value=(*tint,1)
        links.new(tex.outputs['Color'],multiply.inputs[1]);links.new(multiply.outputs[0],bs.inputs['Base Color'])
        # Base tint is multiplied into a dedicated image for glTF-compatible direct textures.
        pixels=np.ones((n,n,4));pixels[:,:,:3]=np.clip((.85+grain[:,:,None]*.14)*np.array(tint)[None,None,:]**(1/2.2),0,1)
        tex.image=image(style+'_Wood_BaseColor',pixels,'sRGB')
        links.remove(bs.inputs['Base Color'].links[0]);links.new(tex.outputs['Color'],bs.inputs['Base Color'])
        nodes.remove(multiply)
        texorm=nodes.new('ShaderNodeTexImage');texorm.image=wood_orm
        sep=nodes.new('ShaderNodeSeparateRGB');links.new(texorm.outputs['Color'],sep.inputs[0])
        links.new(sep.outputs['G'],bs.inputs['Roughness']);links.new(sep.outputs['B'],bs.inputs['Metallic'])
        tn=nodes.new('ShaderNodeTexImage');tn.image=wood_normal
        normal=nodes.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value=.45
        links.new(tn.outputs['Color'],normal.inputs['Color']);links.new(normal.outputs[0],bs.inputs['Normal'])
        mat['surface_role']='wood';report['materials'].append(name)
    glass=bpy.data.materials['PBK_'+style+'_glass']
    shader(glass,(.50,.72,.78),.16,transmission=.78)
    glass['surface_role']='glass';report['materials'].append(glass.name)
    metal=bpy.data.materials['PBK_'+style+'_metal']
    shader(metal,(.25,.28,.30),.30,metallic=.9)
    metal['surface_role']='metal';report['materials'].append(metal.name)
apply_accent_materials()
generator=bpy.data.texts['PBK_Generator.py'].as_string().split('\n# PBR accent revision')[0]
generator=generator.replace('unified-kit-v3-retro-v2-frames',REV)
generator+='\n# PBR accent revision\n'+(BASE/'retro_accent_materials.py').read_text(encoding='utf8')
generator+='''
_accent_generate_building = generate_building
def generate_building(*args, **kwargs):
    result = _accent_generate_building(*args, **kwargs)
    apply_accent_materials()
    return result
_accent_continuous_frame = continuous_frame
def continuous_frame(*args, **kwargs):
    result = _accent_continuous_frame(*args, **kwargs)
    apply_accent_materials()
    return result
'''
text=bpy.data.texts['PBK_Generator.py'];text.clear();text.write(generator)
(ROOT/'generate.py').write_text(generator,encoding='utf8')
for filename,key in [('kit_manifest.json','revision'),('rules/building_rules.json','kit_revision'),('source_status.json','kit_revision'),('art_direction.json','revision')]:
    p=ROOT/filename;doc=json.loads(p.read_text(encoding='utf-8-sig'));doc[key]=REV
    if filename=='kit_manifest.json':
        for entry in doc['modules']:entry['revision']=REV
    if filename=='art_direction.json':
        doc['technical']['accent_materials']={'glass_transmission':.78,'glass_roughness':.16,'wood_roughness':.43,'metallic_railings':.9,'metal_roughness':.30}
    p.write_text(json.dumps(doc,ensure_ascii=False,indent=2),encoding='utf8')
for c in bpy.data.collections:
    if c.asset_data:c['catalog_revision']=REV
errors=[]
for kind in ('glass','wood','metal'):
    report['counts'][kind]=sum(1 for o in bpy.data.objects if o.type=='MESH' and any(m and m.get('surface_role')==kind for m in o.data.materials))
    if not report['counts'][kind]:errors.append('missing '+kind)
for ob in bpy.data.objects:
    if ob.type=='MESH' and ob.get('continuous_frame') and not ob.data.materials[0].get('surface_role')=='wood':errors.append('unassigned frame '+ob.name)
report['errors']=errors;report['passed']=not errors
(ROOT/'material_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if errors:raise RuntimeError(errors)
bpy.context.window.scene=bpy.data.scenes['PBK_Modular_Buildings']
bpy.context.scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'modular_building_kit.blend'))
print('ACCENT_MATERIALS',json.dumps(report),flush=True)
