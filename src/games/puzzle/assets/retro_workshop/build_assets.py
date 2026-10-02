"""Original flat-shaded workshop props, Blender 4.4+. Run inside Blender."""
import bpy, bmesh, math, random, json, os
from mathutils import Vector

OUT = r'D:\projects\njin\src\games\puzzle\assets\retro_workshop'
os.makedirs(os.path.join(OUT, 'models'), exist_ok=True)
scene = bpy.data.scenes.new('RETRO WORKSHOP | Asset Library')
bpy.context.window.scene = scene
scene.unit_settings.scale_length = 1.0
assets = bpy.data.collections.new('01 | GAME ASSETS (23)')
stage = bpy.data.collections.new('02 | PRESENTATION')
scene.collection.children.link(assets)
scene.collection.children.link(stage)
random.seed(44)

def mat(name, rgb, metal=0.0, rough=.8):
    m=bpy.data.materials.new('RW | '+name); m.diffuse_color=(*rgb,1); m.use_nodes=True
    n=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    n.inputs[0].default_value=(*rgb,1); n.inputs[1].default_value=metal; n.inputs[2].default_value=rough
    return m
M=[mat('Steel blue',(.22,.31,.34),.65,.56),mat('Steel highlight',(.48,.59,.60),.5,.5),
   mat('Iron charcoal',(.065,.09,.105),.5,.73),mat('Rust ochre',(.37,.12,.045),.1,.95),
   mat('Rust orange',(.61,.25,.085),.05,.96),mat('Pine honey',(.56,.31,.12)),
   mat('Pine light',(.72,.46,.22)),mat('End grain',(.36,.18,.065)),
   mat('Grain dark',(.24,.115,.045)),mat('Old walnut',(.25,.13,.075)),
   mat('Weathered wood',(.31,.32,.25)),mat('Paint teal',(.075,.30,.28)),
   mat('Enamel red',(.48,.085,.05),.12,.75),mat('Galvanized',(.47,.56,.54),.55,.6),
   mat('Galvanized pale',(.66,.71,.65),.4,.65),mat('Grip black',(.04,.047,.043)),
   mat('Grip bands',(.12,.15,.14)),mat('Brass',(.69,.43,.13),.45,.62)]

class Geo:
    def __init__(self): self.v=[]; self.f=[]; self.mi=[]
    def add(self, verts, faces, material=0):
        start=len(self.v); self.v.extend(verts)
        for i,f in enumerate(faces):
            self.f.append(tuple(start+j for j in f)); self.mi.append(material[i] if isinstance(material,list) else material)
    def box(self,c,d,m):
        x,y,z=c; a,b,h=[v/2 for v in d]
        self.add([(x+sx*a,y+sy*b,z+sz*h) for sz in (-1,1) for sy in (-1,1) for sx in (-1,1)],
          [(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],m)
    def loft(self,rings,n,m,axis='Z'):
        vs=[]
        for z,r,x,y in rings:
            for i in range(n):
                a=2*math.pi*i/n+math.pi/n; p=(x+r*math.cos(a),y+r*math.sin(a),z)
                vs.append(p if axis=='Z' else (p[2],p[1],p[0]))
        fs=[tuple(range(n-1,-1,-1))]
        for k in range(len(rings)-1):
            fs.extend((k*n+i,k*n+(i+1)%n,(k+1)*n+(i+1)%n,(k+1)*n+i) for i in range(n))
        fs.append(tuple((len(rings)-1)*n+i for i in range(n)))
        self.add(vs,fs,m)
    def profile(self, poly, length, m):
        n=len(poly); vs=[(x,y,z) for y in (-length/2,length/2) for x,z in poly]
        fs=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]
        fs.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
        self.add(vs,fs,m)
    def object(self,name,coll=assets):
        me=bpy.data.meshes.new(name+' Mesh'); me.from_pydata(self.v,[],self.f); me.update()
        for m in M: me.materials.append(m)
        for p,mi in zip(me.polygons,self.mi): p.material_index=mi
        bm=bmesh.new(); bm.from_mesh(me); bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(me); bm.free()
        # Per-face planar UVs: portable, no external textures required.
        uv=me.uv_layers.new(name='UVMap')
        for p in me.polygons:
            axis=max(range(3),key=lambda a:abs(p.normal[a])); axes=[a for a in range(3) if a!=axis]
            for li in p.loop_indices:
                co=me.vertices[me.loops[li].vertex_index].co
                uv.data[li].uv=(co[axes[0]],co[axes[1]])
        ob=bpy.data.objects.new(name,me); coll.objects.link(ob)
        return ob

manifest=[]; models=[]
def finish(g,name,category,loc,rot=(0,0,0),props=None):
    ob=g.object(name); ob.location=loc; ob.rotation_euler=rot
    ob['category']=category; ob['style']='original low poly retro'; ob['units']='meters'
    if props:
        for k,v in props.items(): ob[k]=v
    ob.asset_mark(); ob.asset_data.description='Original retro workshop prop. Flat shading, palette materials, planar UVs.'
    models.append(ob)
    manifest.append(dict(name=name,category=category,vertices=len(ob.data.vertices),triangles=sum(len(p.vertices)-2 for p in ob.data.polygons),**(props or {})))
    return ob

# NAILS: tip at local zero, head on +Z. 8 sides, bevelled head and pointed tip.
for row,(size,r,L) in enumerate([('S',.006,.08),('M',.009,.13),('L',.013,.20)]):
    for col,(surface,material) in enumerate([('Clean',1),('Dark',2),('Rust',3)]):
        g=Geo(); g.loft([(0,.0001,0,0),(.022 if size=='L' else L*.16,r,0,0),(L-.008,r,0,0)],8,material)
        g.loft([(L-.008,r*2.6,0,0),(L-.004,r*2.9,0,0),(L,r*2.5,0,0)],8,material)
        if surface=='Rust':
            for i in range(2,len(g.mi),7): g.mi[i]=4
        ob=finish(g,'Nail_'+size+'_'+surface,'Nails',(-2.48+row*.63,1.72+col*.26,.005+r*2.9*2.6),(0,math.pi/2,0),{'shaft_radius_m':r,'length_m':L,'size':size,'finish':surface})
        # The gallery magnifies these tiny parts; individual GLBs retain metre scale.
        ob.scale=(2.6,2.6,2.6); ob['gallery_magnification']=2.6

# HAMMERS: tapered octagonal handle, faceted strike face, split curved claw.
for idx in range(2):
    g=Geo()
    g.loft([(0,.031,0,0),(.018,.039,0,0),(.16,.037,0,0),(.38,.025,0,0),(.58,.031,0,0),(.63,.035,0,0)],8,5 if idx==0 else 12)
    if idx:
        g.loft([(.01,.039,0,0),(.025,.042,0,0),(.22,.037,0,0)],8,15)
        for z in [.05,.09,.13,.17]: g.loft([(z,.040-z*.02,0,0),(z+.012,.040-z*.02,0,0)],8,16)
    else:
        g.box((.032,0,.16),(.002,.018,.17),7)
    g.box((0,0,.63),(.125,.081,.093),0)
    # striking cylinder along X, elevated by head height
    start=len(g.v); g.loft([(-.155,.047,0,0),(-.146,.052,0,0),(-.123,.052,0,0),(-.108,.033,0,0),(-.047,.032,0,0)],8,1,'X')
    for i in range(start,len(g.v)): x,y,z=g.v[i]; g.v[i]=(x,y,z+.63)
    for yy in (-.028,.028):
        # two tapered claws, side profile extruded in Y
        poly=[(.05,.673),(.10,.673),(.16,.65),(.20,.60),(.22,.555),(.196,.565),(.166,.614),(.102,.635),(.05,.635)]
        a=len(g.v); g.profile(poly,.021,0)
        for i in range(a,len(g.v)): x,y,z=g.v[i]; g.v[i]=(x,y+yy,z)
    g.box((0,0,.678),(.035,.025,.003),17)
    finish(g,'Hammer_'+('Wood_Claw' if idx==0 else 'Red_Grip'),'Hammers',(.55+idx*1.12,2.5,.072),(math.pi/2,0,-.18))

# WOOD: chamfered end profile, original polygon grain, knots and chips.
for idx,(name,w,L,h,material) in enumerate([('Pine',.26,.96,.055,5),('Dark',.25,.83,.07,9),('Broken',.27,.93,.055,5),('Painted',.28,1.05,.048,11)]):
    g=Geo(); a=w/2; c=.012
    poly=[(-a+c,0),(a-c,0),(a,c),(a,h-c),(a-c,h),(-a+c,h),(-a,h-c),(-a,c)]
    g.profile(poly,L,material)
    g.mi[0]=g.mi[1]=7
    if name=='Broken':
        for i in range(8,16):
            x,y,z=g.v[i]; g.v[i]=(x,y+[-.07,.05,-.025,.015,-.11,.01,-.04,.03][i-8],z)
    for k in range(9):
        x=-a+.025+k*(w-.05)/8; y=random.uniform(-L*.4,-L*.15); length=random.uniform(.15,.45)
        g.add([(x,y,h+.0005),(x+.004,y+.035,h+.0005),(x+.002,y+length,h+.0005),(x-.003,y+length*.7,h+.0005)],[(0,1,2,3)],8 if name!='Painted' else 10)
    if name!='Painted':
        g.add([(.02,-.04,h+.0008),(.042,-.012,h+.0008),(.03,.034,h+.0008),(.011,.023,h+.0008)],[(0,1,2,3)],8)
        g.add([(.021,-.008,h+.001),(.03,.006,h+.001),(.022,.025,h+.001)],[(0,1,2)],7)
    else:
        for x,y in [(-.08,.25),(.07,-.32),(.10,.37)]:
            g.add([(x,y,h+.001),(x+.035,y+.015,h+.001),(x+.021,y+.08,h+.001),(x-.008,y+.05,h+.001)],[(0,1,2,3)],6)
    finish(g,'Plank_'+name,'Wood',(-2.0+idx*1.3,.48,.01))

# STEEL: solid plate, I-beam, L-angle, hollow rectangular section.
for idx,name in enumerate(['Plate','I_Beam','L_Angle','Box_Tube']):
    g=Geo(); w=.26; h=.19; t=.027; L=.92
    if idx==0:
        g.box((0,0,.018),(.43,.88,.036),2)
        for x,y in [(-.12,.22),(.06,-.18),(.15,.30)]:
            g.add([(x,y,.0365),(x+.052,y-.018,.0365),(x+.075,y+.037,.0365),(x+.016,y+.058,.0365)],[(0,1,2,3)],3)
    elif idx==1:
        g.profile([(-w/2,0),(w/2,0),(w/2,t),(t/2,t),(t/2,h-t),(w/2,h-t),(w/2,h),(-w/2,h),(-w/2,h-t),(-t/2,h-t),(-t/2,t),(-w/2,t)],L,0)
        g.mi[0]=g.mi[1]=1
    elif idx==2:
        g.profile([(-w/2,0),(w/2,0),(w/2,t),(-w/2+t,t),(-w/2+t,h),(-w/2,h)],L,3)
        for i in range(2,len(g.mi),2): g.mi[i]=0
    else:
        outer=[(-w/2,0),(w/2,0),(w/2,h),(-w/2,h)]
        inner=[(-w/2+t,t),(w/2-t,t),(w/2-t,h-t),(-w/2+t,h-t)]
        verts=[(x,y,z) for y in (-L/2,L/2) for ring in (outer,inner) for x,z in ring]; fs=[]; mis=[]
        for i in range(4):
            j=(i+1)%4
            fs.extend([(i,j,j+8,i+8),(i+4,i+12,j+12,j+4),(i,i+4,j+4,j),(i+8,j+8,j+12,i+12)])
            mis.extend([0,2,1,1])
        g.add(verts,fs,mis)
    finish(g,'Steel_'+name,'Steel',(-2.0+idx*1.3,-.94,.01))

# CORRUGATED SHEETS: closed mesh, polygon ridges; rust, enamel and bent variants.
for idx,name in enumerate(['Galvanized','Rusty','Red','Bent_Teal']):
    g=Geo(); nx=24; ny=5; w=.81; L=1.05; thick=.006
    vs=[]
    for layer in range(2):
        for j in range(ny+1):
            y=-L/2+L*j/ny
            for i in range(nx+1):
                x=-w/2+w*i/nx
                z=.035+(.028 if i%4 in (1,2) else 0)
                if idx==3: z+=.10*max(0,(i/nx-.55)/.45)**2*(j/ny)**2
                vs.append((x,y,z-layer*thick))
    stride=nx+1; offset=stride*(ny+1); fs=[]; mis=[]
    for j in range(ny):
        for i in range(nx):
            a=j*stride+i; b=a+1; c=b+stride; d=a+stride
            fs.extend([(a,b,c,d),(a+offset,d+offset,c+offset,b+offset)])
            mm=13 if idx==0 else (3 if idx==1 else (12 if idx==2 else 11))
            if idx==0 and (i+j*3)%11==0: mm=14
            if idx==1: mm=4 if (i*7+j*3)%9<4 else (13 if (i+j)%7==0 else 3)
            if idx==2 and (i+j*3)%23==0: mm=3
            if idx==3 and i>19 and j>2: mm=13
            mis.extend([mm,mm])
    perimeter=list(range(stride))+[j*stride+nx for j in range(1,ny+1)]+list(range(ny*stride+nx-1,ny*stride-1,-1))+[j*stride for j in range(ny-1,0,-1)]
    for k,a in enumerate(perimeter):
        b=perimeter[(k+1)%len(perimeter)]; fs.append((a,a+offset,b+offset,b)); mis.append(2)
    g.add(vs,fs,mis)
    finish(g,'Sheet_'+name,'Sheet metal',(-2.0+idx*1.3,-2.50,-.0245))

# Presentation is separate from the game meshes and individual exports.
floor=mat('Display charcoal',(.032,.054,.065)); pad=mat('Display card',(.052,.082,.089)); cream=mat('Typography cream',(.80,.75,.57)); accent=mat('Typography amber',(.91,.39,.095))
M.extend([floor,pad,cream,accent])
g=Geo(); g.box((-.05,-.2,-.105),(6.2,7.1,.20),18); g.object('Display_Base',stage)
for y in [2.03,.48,-.94,-2.5]:
    g=Geo(); g.box((-.05,y,-.008),(5.7,1.26,.025),19); g.object('Display_Row',stage)
def text(body,x,y,size,material=20):
    cu=bpy.data.curves.new('Label '+body,'FONT'); cu.body=body; cu.size=size; cu.extrude=0; cu.space_character=1.12
    ob=bpy.data.objects.new('Label | '+body,cu); stage.objects.link(ob); ob.location=(x,y,.012); cu.materials.append(M[material]); return ob
text('RETRO / WORKSHOP',-2.68,2.96,.29)
text('23 ORIGINAL LOW-POLY PROPS  /  MATERIAL PALETTE  /  GAME ASSETS',-2.64,2.74,.071,21)
text('01  NAILS',-2.65,2.53,.10,21); text('02  CLAW HAMMERS',.10,2.53,.10,21)
text('S / M / L   -   CLEAN / DARK / RUST   -   DISPLAY x2.6',-2.58,1.46,.064)
text('WOOD HANDLE',.25,1.46,.072); text('RED + RUBBER',1.42,1.46,.072)
for y,title in [(1.10,'03  TIMBER'),(-.32,'04  STEEL SECTIONS'),(-1.85,'05  CORRUGATED METAL')]: text(title,-2.65,y,.10,21)
for idx,name in enumerate(['PINE','DARK WOOD','SPLINTERED','PAINTED TEAL']): text(name,-2.40+idx*1.3,-.15,.074)
for idx,name in enumerate(['PLATE / RUST','I-BEAM','L-ANGLE','HOLLOW TUBE']): text(name,-2.40+idx*1.3,-1.59,.074)
for idx,name in enumerate(['GALVANIZED','OXIDIZED','RED ENAMEL','BENT / TEAL']): text(name,-2.40+idx*1.3,-3.13,.074)
text('FLAT SHADING   /   8-SIDED TOOLS   /   NO EXTERNAL TEXTURES',-2.65,-3.55,.074,21)

world=bpy.data.worlds.new('RW Studio World'); world.use_nodes=True; scene.world=world
bg=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND'); bg.inputs[0].default_value=(.14,.20,.24,1); bg.inputs[1].default_value=.65
def aim(ob,target): ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler()
for name,pos,power,color,size in [('Key',(-3,-1,7),950,(1,.82,.61),5),('Fill',(4,2,5),800,(.58,.78,1),4),('Rim',(-1,5,4),700,(1,.48,.18),3)]:
    data=bpy.data.lights.new('RW '+name,'AREA'); data.energy=power; data.color=color; data.size=size
    ob=bpy.data.objects.new('RW '+name,data); stage.objects.link(ob); ob.location=pos; aim(ob,(0,0,0))
cam_data=bpy.data.cameras.new('RW Camera'); cam_data.type='ORTHO'; cam_data.ortho_scale=8.8
cam=bpy.data.objects.new('RW Camera',cam_data); stage.objects.link(cam); cam.location=(.7,-5.5,11); aim(cam,(0,-.2,0)); scene.camera=cam
try: scene.render.engine='BLENDER_EEVEE_NEXT'
except TypeError: pass
scene.render.resolution_x=1400; scene.render.resolution_y=1500; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'; scene.render.filepath=os.path.join(OUT,'preview.png')
scene.render.film_transparent=False
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.shading.type='MATERIAL'
            area.spaces.active.overlay.show_overlays=False

# Export each prop at its functional origin, with identity transform.
for ob in models:
    for other in bpy.context.selected_objects: other.select_set(False)
    ob.select_set(True); bpy.context.view_layer.objects.active=ob
    loc=ob.location.copy(); rot=ob.rotation_euler.copy(); scale=ob.scale.copy()
    ob.location=(0,0,0); ob.rotation_euler=(0,0,0); ob.scale=(1,1,1)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,'models',ob.name+'.glb'),use_selection=True,export_animations=False,export_extras=True)
    ob.location=loc; ob.rotation_euler=rot; ob.scale=scale; ob.select_set(False)
for ob in models: ob.select_set(True)
bpy.context.view_layer.objects.active=models[0]
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,'retro_workshop_gallery.glb'),use_selection=True,export_animations=False,export_extras=True)
with open(os.path.join(OUT,'manifest.json'),'w',encoding='utf-8') as f: json.dump(manifest,f,indent=2,ensure_ascii=False)
with open(os.path.join(OUT,'README.md'),'w',encoding='utf-8') as f:
    f.write('# Retro Workshop\n\n23 original low poly assets, created in Blender 4.4.3.\n\n9 nails (3 sizes x clean/dark/rust), 2 claw hammers, 4 wood planks, 4 steel sections, 4 corrugated sheets.\n\nIndividual models/*.glb use metre scale, identity transforms, flat normals, UVs, palette PBR materials, and custom properties. No external textures. Nail pivot is at tip (+Z toward head); hammer pivot is at handle bottom; panels/steel use bottom-centre pivots. glTF converts Blender Z-up to Y-up.\n\nNails: S radius 6 mm / length 80 mm; M radius 9 mm / length 130 mm; L radius 13 mm / length 200 mm. These intentionally chunky proportions improve retro game readability.\n\nThe blend contains an editable asset gallery and the original startup scene. Presentation collections are separate. Gallery nails are magnified 2.6x; individual GLBs retain original scale. The gallery GLB contains props only at display positions.\n\nAll geometry and palette materials are original; no third-party assets. Run build_assets.py inside Blender to regenerate into a new scene.\n')
for ob in bpy.context.selected_objects: ob.select_set(False)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'retro_workshop.blend'))
print(json.dumps({'assets':len(models),'triangles':sum(x['triangles'] for x in manifest),'blend':os.path.join(OUT,'retro_workshop.blend')},indent=2))
