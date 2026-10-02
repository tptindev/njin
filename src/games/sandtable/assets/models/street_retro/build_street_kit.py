"""Author original Vietnamese street props matching the rounded retro building kit.
Runs in a disposable Blender process. Saves .blend/JSON only; never exports GLB.
"""
from pathlib import Path
import math,json,random
import bpy,bmesh
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parent
REV='vietnam-street-retro-v4'
SOURCE=ROOT.parent/'procedural_building/retro/modular_building_kit.blend'
random.seed(7261)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.name='Street_Retro_Catalog'
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
with bpy.data.libraries.load(str(SOURCE),link=False) as (src,dst):
    dst.materials=['PBK_Indochine_wall','PBK_Brick_wall','PBK_Indochine_wood','PBK_Indochine_metal','PBK_Indochine_glass','PBK_Brick_concrete']
M={}
def material(key,color,base='PBK_Indochine_wall',rough=None):
    mat=bpy.data.materials[base].copy();mat.name='ST_'+key
    bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    bs.inputs['Base Color'].default_value=(*color,1);mat.diffuse_color=(*color,1)
    if rough is not None:
        for link in list(bs.inputs['Roughness'].links):mat.node_tree.links.remove(link)
        bs.inputs['Roughness'].default_value=rough
    mat['street_surface']=key;M[key]=mat;return mat
for key,col in [('stone',(.47,.49,.45)),('cream',(.82,.76,.62)),('red',(.55,.16,.11)),('blue',(.12,.30,.39)),('green',(.15,.34,.26)),('yellow',(.78,.54,.19)),('black',(.055,.063,.061)),('white',(.86,.85,.77)),('leaf',(.19,.40,.14)),('bread',(.63,.37,.12)),('broth',(.39,.20,.055)),('noodle',(.80,.61,.23)),('orange',(.61,.28,.10)),('pavement',(.48,.49,.46))]:material(key,col)
for key,base in [('wood','PBK_Indochine_wood'),('metal','PBK_Indochine_metal'),('glass','PBK_Indochine_glass')]:
    mat=bpy.data.materials[base];M[key]=mat
material('steel',(.50,.53,.55),'PBK_Indochine_metal',.28)
material('light',(.95,.81,.54),'PBK_Indochine_wall',.25)
bs=next(n for n in M['light'].node_tree.nodes if n.type=='BSDF_PRINCIPLED')
bs.inputs['Emission Color'].default_value=(1,.77,.40,1);bs.inputs['Emission Strength'].default_value=2.5
for key in ('red','blue','green'):
    material('plastic_'+key,M[key].diffuse_color[:3],rough=.58)
font=bpy.data.fonts.load('C:/Windows/Fonts/arial.ttf')
ASSETS={};META={}

def finish(c,name,bm,mat,bevel=.012):
    bm.normal_update();bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    if False: # Retro: no geometric bevels.
        edges=[e for e in bm.edges if len(e.link_faces)==2 and e.calc_face_angle(0)>.6]
        bmesh.ops.bevel(bm,geom=edges,offset=bevel,segments=3,affect='EDGES',clamp_overlap=True)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
    bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-6)
    bm.normal_update()
    for f in bm.faces:f.smooth=False
    for e in bm.edges:e.smooth=len(e.link_faces)==2 and e.calc_face_angle(0)<.75
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-6)
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    data=bpy.data.meshes.new('ST_'+name);bm.to_mesh(data);bm.free();data.materials.append(M[mat]);data.update();data['city_low_poly']='city-low-poly-v2'
    uv=data.uv_layers.new(name='RetroUV')
    for p in data.polygons:
        axis=max(range(3),key=lambda i:abs(p.normal[i]));axes=((1,2),(0,2),(0,1))[axis]
        for loop in p.loop_indices:
            v=data.vertices[data.loops[loop].vertex_index].co;uv.data[loop].uv=(v[axes[0]]*4,v[axes[1]]*4)
    ob=bpy.data.objects.new(data.name,data);c.objects.link(ob);return ob
def box(c,name,at,size,mat,bevel=.015,rot=None):
    bm=bmesh.new();bmesh.ops.create_cube(bm,size=1)
    for v in bm.verts:
        v.co=Vector(tuple(v.co[i]*size[i] for i in range(3)))
        if rot:v.co=rot@v.co
        v.co+=Vector(at)
    return finish(c,name,bm,mat,min(bevel,min(size)*.24))
def lathe(c,name,at,profile,mat,axis=(0,0,1),segments=8,bevel=0):
    # Round the profile before revolving. Beveling triangle-fan caps produces
    # collapsed slivers on thin lids and insulator ribs.
    if False: # Retro: no geometric bevels.
        curved=[]
        for i,p in enumerate(profile):
            p=Vector(p);prev=Vector(profile[i-1]);nxt=Vector(profile[(i+1)%len(profile)])
            if p.x<1e-7:curved.append(tuple(p));continue
            amount=min(bevel,(prev-p).length*.22,(nxt-p).length*.22)
            start=p+(prev-p).normalized()*amount;end=p+(nxt-p).normalized()*amount
            for j in range(4):
                t=j/3;q=(1-t)**2*start+2*t*(1-t)*p+t*t*end;curved.append(tuple(q))
        profile=curved
    verts=[];rings=[];faces=[]
    for radius,z in profile:
        if radius<1e-7:rings.append([len(verts)]);verts.append((0,0,z))
        else:
            ring=[]
            for i in range(segments):
                a=2*math.pi*i/segments;ring.append(len(verts));verts.append((radius*math.cos(a),radius*math.sin(a),z))
            rings.append(ring)
    for a,b in zip(rings,rings[1:]+rings[:1]):
        if len(a)==len(b)==1:continue
        for i in range(segments):
            j=(i+1)%segments
            if len(a)==1:faces.append((a[0],b[j],b[i]))
            elif len(b)==1:faces.append((a[i],a[j],b[0]))
            else:faces.append((a[i],a[j],b[j],b[i]))
    data=bpy.data.meshes.new('temp');data.from_pydata(verts,[],faces)
    bm=bmesh.new();bm.from_mesh(data);bpy.data.meshes.remove(data)
    rotation=Vector((0,0,1)).rotation_difference(Vector(axis))
    for v in bm.verts:v.co=rotation@v.co+Vector(at)
    return finish(c,name,bm,mat,0)
def cylinder(c,name,at,r,h,mat,axis=(0,0,1),bevel=.006):
    return lathe(c,name,at,[(0,-h/2),(r,-h/2),(r,h/2),(0,h/2)],mat,axis,bevel=bevel)
def vessel(c,name,at,r,h,mat,t=.015):
    return lathe(c,name,at,[(0,0),(r*.65,0),(r,h),(r-t,h),(max(.015,r*.65-t),t),(0,t)],mat,bevel=.003)
def sphere(c,name,at,size,mat):
    bm=bmesh.new();bmesh.ops.create_uvsphere(bm,u_segments=8,v_segments=4,radius=1)
    for v in bm.verts:v.co=Vector(tuple(v.co[i]*size[i] for i in range(3)))+Vector(at)
    return finish(c,name,bm,mat,0)
def beam(c,name,a,b,width,mat):
    a,b=Vector(a),Vector(b);length=(b-a).length
    return box(c,name,(a+b)/2,(width,width,length),mat,min(width*.2,.012),Vector((0,0,1)).rotation_difference((b-a).normalized()).to_matrix())
def tube(c,name,points,r,mat):
    # Continuous bent tube with closed end caps, no disconnected pipe joints.
    closed=(Vector(points[0])-Vector(points[-1])).length<1e-6
    if closed:points=points[:-1]
    verts=[];faces=[];count=6
    for i,p in enumerate(points):
        tangent=Vector(points[(i+1)%len(points)] if closed else points[min(i+1,len(points)-1)])-Vector(points[(i-1)%len(points)] if closed else points[max(0,i-1)])
        tangent.normalize();u=tangent.cross(Vector((0,0,1)))
        if u.length<.1:u=tangent.cross(Vector((0,1,0)))
        u.normalize();v=tangent.cross(u).normalized()
        for j in range(count):verts.append(Vector(p)+r*(u*math.cos(j*2*math.pi/count)+v*math.sin(j*2*math.pi/count)))
    faces=[] if closed else [tuple(range(count-1,-1,-1)),tuple((len(points)-1)*count+j for j in range(count))]
    for i in range(len(points) if closed else len(points)-1):
        ni=(i+1)%len(points)
        for j in range(count):faces.append((i*count+j,i*count+(j+1)%count,ni*count+(j+1)%count,ni*count+j))
    data=bpy.data.meshes.new('temp');data.from_pydata(verts,[],faces);bm=bmesh.new();bm.from_mesh(data);bpy.data.meshes.remove(data)
    return finish(c,name,bm,mat,0)
def text(c,words,at,size=.13,mat='cream'):
    return None # No lettering in model geometry.

def asset(key,label,category,**meta):
    c=bpy.data.collections.new('ST_'+key);scene.collection.children.link(c)
    c.asset_mark();c.asset_data.description=label+' / rounded retro / metric / ground pivot'
    c['asset_id']='street/'+key;c['revision']=REV;c['front']='-Y';c['pivot']='ground-center'
    ASSETS[key]=c;META[key]={'label_vi':label,'category':category,**meta};return c
def inst(c,key,at=(0,0,0),angle=0):
    ob=bpy.data.objects.new('ST_Instance_'+key,None);c.objects.link(ob);ob.instance_type='COLLECTION';ob.instance_collection=ASSETS[key]
    ob.location=at;ob.rotation_euler.z=angle;ob['asset_id']='street/'+key;return ob

# Seating: load-bearing stone pedestals, metal public frames, open plastic backs.
def stone_bench(key,back):
    c=asset(key,'Ghế đá có tựa' if back else 'Ghế đá không tựa','seating',seat_height_m=.45,seat_points=[[-.55,0,.45],[0,0,.45],[.55,0,.45]])
    for x in (-.62,.62):box(c,'Stone_Pedestal',(x,0,.195),(.24,.48,.39),'stone',.05)
    box(c,'Stone_Seat',(0,0,.425),(1.85,.55,.05),'cream',.02)
    if back:
        for x in (-.72,.72):beam(c,'Stone_Back_Support',(x,.16,.38),(x,.24,.81),.11,'stone')
        box(c,'Stone_Back',(0,.23,.74),(1.85,.09,.28),'cream',.04)
stone_bench('StoneBench',False);stone_bench('StoneBenchBack',True)
def public_bench(key,back):
    c=asset(key,'Ghế công cộng có tựa' if back else 'Ghế công cộng không tựa','seating',seat_height_m=.46,seat_points=[[-.5,0,.46],[.5,0,.46]])
    for x in (-.65,.65):
        for y in (-.19,.19):beam(c,'Bench_Leg',(x,y,0),(x,y,.43),.06,'metal')
        beam(c,'Seat_Crossbar',(x,-.24,.405),(x,.24,.405),.055,'metal')
        if back:beam(c,'Back_Upright',(x,.20,.38),(x,.30,.87),.06,'metal')
    for y in (-.20,-.10,0,.10,.20):box(c,'Seat_Wood_Slat',(0,y,.44),(1.8,.087,.045),'wood',.012)
    if back:
        for z in (.62,.73,.84):box(c,'Back_Wood_Slat',(0,.275,z),(1.8,.045,.085),'wood',.012)
        for x in (-.82,.82):
            tube(c,'Armrest',[(x,-.20,.44),(x,-.22,.62),(x,-.18,.66),(x,.22,.66),(x,.27,.60)],.024,'metal')
public_bench('PublicBench',False);public_bench('PublicBenchBack',True)
def plastic_seat(key,h,back,color):
    c=asset(key,('Ghế nhựa ' if back else 'Đôn nhựa ')+('cao' if h>.35 else 'thấp'),'seating',seat_height_m=h,seat_points=[[0,0,h]])
    w=.42 if back else .32;d=.40 if back else .32;mat='plastic_'+color
    box(c,'Moulded_Seat',(0,0,h-.022),(w,d,.044),mat,.018)
    for x in (-1,1):
        for y in (-1,1):beam(c,'Splayed_Leg',(x*(w/2-.025),y*(d/2-.025),0),(x*(w/2-.055),y*(d/2-.055),h-.035),.047,mat)
    if back:
        for x in (-w/2+.035,w/2-.035):beam(c,'Back_Post',(x,d/2-.035,h-.035),(x,d/2+.015,h+.39),.047,mat)
        box(c,'Back_Top',(0,d/2+.014,h+.37),(w,.055,.065),mat,.025)
        for x in (-.11,0,.11):box(c,'Back_Slat',(x,d/2,h+.23),(.058,.038,.28),mat,.013)
    else:
        for y in (-d/2+.04,d/2-.04):beam(c,'Leg_Brace',(-w/2+.04,y,h*.4),(w/2-.04,y,h*.4),.025,mat)
plastic_seat('PlasticChairHigh',.45,True,'red');plastic_seat('PlasticChairLow',.27,True,'blue')
plastic_seat('PlasticStoolHigh',.44,False,'blue');plastic_seat('PlasticStoolLow',.24,False,'red')
c=asset('PlasticTableLow','Bàn nhựa thấp','furniture',table_height_m=.48)
box(c,'Table_Top',(0,0,.46),(.72,.62,.04),'plastic_green',.018)
for x in (-.28,.28):
    for y in (-.23,.23):beam(c,'Table_Leg',(x*1.08,y*1.08,0),(x,y,.44),.045,'plastic_green')

c=asset('FireHydrant','Trụ cấp nước chữa cháy','infrastructure',connection_points={'hose_left':[-.24,0,.49],'hose_right':[.24,0,.49]},clearance_m=.65)
cylinder(c,'Hydrant_Foot',(0,0,.035),.19,.07,'red')
cylinder(c,'Hydrant_Body',(0,0,.36),.115,.58,'red')
sphere(c,'Hydrant_Dome',(0,0,.65),(.12,.12,.085),'red')
for side in (-1,1):
    cylinder(c,'Hydrant_Nozzle',(side*.16,0,.46),.066,.17,'red',(1,0,0))
    cylinder(c,'Hose_Cap',(side*.25,0,.46),.074,.034,'steel',(1,0,0))
    box(c,'Cap_Nut',(side*.278,0,.46),(.025,.047,.047),'steel',.004)
for a in range(4):cylinder(c,'Base_Bolt',(.145*math.cos(a*math.pi/2),.145*math.sin(a*math.pi/2),.078),.016,.025,'steel')
box(c,'Hydrant_Nut',(0,0,.745),(.042,.042,.025),'steel',.004)

def street_lamp(key,double):
    c=asset(key,'Trụ đèn hai nhánh' if double else 'Trụ đèn một nhánh','infrastructure',light_points=[[-.9,0,4.9],[.9,0,4.9]] if double else [[.9,0,4.9]],light_color=[1,.77,.40])
    box(c,'Lamp_Base',(0,0,.06),(.34,.34,.12),'stone',.035)
    lathe(c,'Lamp_Post',(0,0,0),[(0,.12),(.10,.12),(.073,4.72),(.035,4.8),(0,4.8)],'metal',bevel=.006)
    for sign in (-1,1) if double else (1,):
        tube(c,'Lamp_Neck',[(0,0,4.48),(sign*.12,0,4.75),(sign*.28,0,4.93),(sign*.65,0,5.02),(sign*.9,0,4.98)],.043,'metal')
        box(c,'Lamp_Housing',(sign*.9,0,4.99),(.55,.24,.10),'metal',.04)
        box(c,'Lamp_Lens',(sign*.9,0,4.932),(.43,.18,.02),'light',.008)
street_lamp('StreetLampSingle',False);street_lamp('StreetLampDouble',True)
c=asset('UtilityPole','Trụ điện bê tông','infrastructure',wire_sockets=[[-.60,0,7.15],[0,0,7.15],[.60,0,7.15]])
lathe(c,'Concrete_Pole',(0,0,0),[(0,0),(.16,0),(.11,7.1),(0,7.1)],'stone',bevel=.008)
for z in (6.35,6.95):
    box(c,'Power_Crossarm',(0,0,z),(1.55,.14,.13),'metal',.012)
    for x in (-.60,0,.60):
        cylinder(c,'Ceramic_Insulator',(x,0,z+.115),.042,.17,'cream')
        for i in range(3):cylinder(c,'Insulator_Rib',(x,0,z+.06+i*.055),.065,.016,'cream')
box(c,'Pole_Junction_Box',(0,-.19,1.65),(.22,.16,.34),'metal',.025)
for z in (1.43,1.85):box(c,'Pole_Strap',(0,0,z),(.34,.35,.022),'metal',.006)
text(c,'ĐIỆN',(0,-.279,1.65),.062)

c=asset('TrashBinRound','Thùng rác công cộng tròn','infrastructure')
vessel(c,'Bin_Hollow_Body',(0,0,0),.25,.72,'green',.025)
for z in (.12,.61):lathe(c,'Bin_Rim',(0,0,z),[(.264,-.0125),(.264,.0125),(.246,.0125),(.246,-.0125)],'metal')
for x in (-.25,.25):beam(c,'Bin_Hood_Post',(x,0,.54),(x,0,.87),.025,'metal')
lathe(c,'Bin_Rain_Hood',(0,0,.87),[(0,0),(.28,0),(.27,.055),(0,.085)],'green')
text(c,'RÁC',(0,-.247,.42),.12)
c=asset('TrashBinWheelie','Thùng rác có bánh xe','infrastructure')
box(c,'Wheelie_Body',(0,0,.49),(.46,.42,.70),'green',.055)
box(c,'Wheelie_Lid',(0,0,.87),(.51,.47,.075),'green',.025)
for x in (-.205,.205):cylinder(c,'Bin_Wheel',(x,.14,.095),.095,.065,'black',(1,0,0))
for x in (-.18,.18):box(c,'Bin_Front_Foot',(x,-.14,.07),(.055,.075,.14),'green',.012)
tube(c,'Bin_Push_Handle',[(-.19,.22,.77),(-.19,.29,.80),(.19,.29,.80),(.19,.22,.77)],.020,'metal')
text(c,'RÁC',(0,-.216,.50),.14)

c=asset('StreetUmbrella','Dù che quầy vỉa hè','furniture',shade_radius_m=1.1)
cylinder(c,'Umbrella_Base',(0,0,.035),.27,.07,'stone')
cylinder(c,'Umbrella_Pole',(0,0,1.18),.028,2.30,'metal')
# Thick sector canopy with alternating pigments and visible structural ribs.
for i in range(10):
    a,b=i*math.pi/5,(i+1)*math.pi/5
    verts=[]
    for zoff in (0,-.025):
        for r,z in [(.03,2.38),(.50,2.27),(1.1,2.05)]:
            for t in (a,b):verts.append((r*math.cos(t),r*math.sin(t),z+zoff))
    faces=[(0,1,3,2),(2,3,5,4),(6,8,9,7),(8,10,11,9),(0,6,7,1),(4,5,11,10),(0,2,4,10,8,6),(1,7,9,11,5,3)]
    data=bpy.data.meshes.new('temp');data.from_pydata(verts,[],faces);bm=bmesh.new();bm.from_mesh(data);bpy.data.meshes.remove(data)
    finish(c,'Umbrella_Sector',bm,'cream' if i%2 else 'red',.004)
    beam(c,'Umbrella_Rib',(.03*math.cos(a),.03*math.sin(a),2.36),(1.07*math.cos(a),1.07*math.sin(a),2.027),.015,'metal')

def bowl(c,at,kind='noodle'):
    x,y,z=at;vessel(c,'Serving_Bowl',at,.092,.068,'white',.008)
    cylinder(c,'Bowl_Broth',(x,y,z+.049),.072,.006,'broth',bevel=.001)
    for i in range(4):
        pts=[(x+(.045-i*.012)*math.cos(t),y+(.045-i*.012)*math.sin(t),z+.057+i*.002) for t in [j*math.pi/6 for j in range(13)]]
        tube(c,'Noodles',pts,.003,'noodle' if kind=='miquang' else 'cream')
    for dx,dy in [(-.03,.025),(.03,.02)]:sphere(c,'Herb',(x+dx,y+dy,z+.068),(.02,.013,.006),'leaf')
    if kind=='miquang':
        sphere(c,'Half_Egg',(x-.025,y-.025,z+.075),(.025,.032,.014),'white')
        sphere(c,'Egg_Yolk',(x-.025,y-.025,z+.087),(.016,.018,.005),'yellow')
def bottles(c,z,x0):
    for i,col in enumerate(('broth','red','yellow')):
        lathe(c,'Condiment',(x0+i*.11,.16,z),[(0,0),(.035,0),(.035,.15),(.02,.18),(.02,.24),(0,.24)],col)
def soup_pot(c,at):
    vessel(c,'Soup_Pot',at,.23,.28,'steel',.02)
    x,y,z=at;cylinder(c,'Soup_Surface',(x,y,z+.24),.205,.005,'broth')
    for side in (-1,1):tube(c,'Pot_Handle',[(x+side*.22,y-.055,z+.22),(x+side*.28,y-.055,z+.22),(x+side*.28,y+.055,z+.22),(x+side*.22,y+.055,z+.22)],.013,'metal')
    cylinder(c,'Pot_Lid',(x+.26,y+.17,1.014),.21,.028,'steel')
    cylinder(c,'Pot_Lid_Knob',(x+.26,y+.17,1.06),.025,.06,'black')
def cart_base(c,color,canopy=True):
    box(c,'Cart_Cabinet',(0,0,.57),(1.45,.66,.76),color,.035)
    box(c,'Countertop',(0,0,.975),(1.55,.75,.05),'steel',.015)
    for x in (-.58,.58):
        for y in (-.255,.255):
            cylinder(c,'Cart_Wheel',(x,y,.16),.16,.075,'black',(1,0,0))
            cylinder(c,'Wheel_Hub',(x+(.04 if x>0 else -.04),y,.16),.065,.012,'steel',(1,0,0))
    for x in (-.69,.69):
        beam(c,'Cart_Frame',(x,.29,.94),(x,.29,1.97),.045,'metal')
    if canopy:
        box(c,'Cart_Canopy',(0,0,1.99),(1.72,.89,.09),color,.035)
        box(c,'Cart_Signboard',(0,-.43,1.85),(1.55,.05,.23),color,.02)
    for x in (-.37,.37):
        box(c,'Cabinet_Door',(x,-.341,.58),(.66,.025,.58),color,.012)
        beam(c,'Cabinet_Handle',(x+.17,-.37,.56),(x+.17,-.37,.69),.014,'metal')
    tube(c,'Cart_Pushbar',[(-.74,.31,.97),(-.84,.31,1.05),(-.84,-.22,1.05),(-.74,-.22,.97)],.024,'metal')

c=asset('HuTieuCart','Xe bán hủ tiếu','vendor',service_anchor=[0,-.65,0],work_anchor=[0,.8,0])
cart_base(c,'red');text(c,'HỦ TIẾU',(0,-.462,1.80),.17)
box(c,'Stove',( -.36,.04,1.03),(.52,.48,.08),'black',.01);soup_pot(c,(-.36,.04,1.07))
box(c,'Prep_Glass',( .36,.19,1.30),(.66,.015,.55),'glass',.004)
for x in (.035,.685):beam(c,'Display_Upright',(x,.19,1.03),(x,.19,1.64),.025,'metal')
for i in range(3):vessel(c,'Stacked_Bowl',(.40,-.10,1.005+i*.05),.092,.065,'white',.008)
bowl(c,(.12,-.19,1.005));bottles(c,1.005,.37)
box(c,'Herb_Tray',(.41,.21,1.02),(.32,.20,.035),'cream',.005)
for i in range(8):sphere(c,'Fresh_Herb',(.29+(i%4)*.065,.19+(i//4)*.065,1.052),(.036,.028,.017),'leaf')

c=asset('BanhMiCart','Xe bánh mì tủ kính','vendor',service_anchor=[0,-.70,0],work_anchor=[0,.85,0])
cart_base(c,'yellow');text(c,'BÁNH MÌ',(0,-.462,1.80),.17)
box(c,'Glass_Front',(0,-.30,1.39),(1.38,.018,.72),'glass',.005)
for x in (-.69,.69):box(c,'Glass_Side',(x,0,1.39),(.018,.60,.72),'glass',.005)
for x in (-.71,.71):beam(c,'Showcase_Post',(x,-.32,1.01),(x,-.32,1.78),.032,'metal')
for z in (1.08,1.39):
    box(c,'Bread_Tray',(0,.01,z),(1.25,.49,.025),'cream',.008)
    for x in (-.44,-.14,.17,.46):
        sphere(c,'Baguette',(x,.01,z+.072),(.135,.052,.057),'bread')
        for dx in (-.04,.04):beam(c,'Bread_Score',(x+dx,-.025,z+.12),(x+dx+.025,.025,z+.12),.006,'cream')
box(c,'Prep_Board',(0,.42,1.025),(.75,.22,.035),'wood',.012)

c=asset('MiQuangCounter','Quầy mì Quảng','vendor',service_anchor=[0,-.70,0],work_anchor=[0,.85,0])
cart_base(c,'blue');text(c,'MÌ QUẢNG',(0,-.462,1.80),.16)
soup_pot(c,(-.38,.10,1.015))
for x in (.12,.42):bowl(c,(x,-.15,1.005),'miquang')
box(c,'Ingredient_Tray',(.34,.17,1.03),(.54,.23,.035),'steel',.006)
for i in range(5):sphere(c,'Rice_Cracker',(.14+i*.075,.17,1.054),(.045,.060,.006),'cream')
bottles(c,1.005,-.10)

def tea_details(c,x=0,y=0,z=0):
    box(c,'Ice_Cooler',(x-.27,y+.08,z+.24),(.48,.36,.48),'blue',.035)
    box(c,'Cooler_Lid',(x-.27,y+.08,z+.49),(.51,.39,.045),'cream',.018)
    tube(c,'Cooler_Handle',[(x-.42,y+.08,z+.52),(x-.42,y+.08,z+.59),(x-.12,y+.08,z+.59),(x-.12,y+.08,z+.52)],.012,'metal')
    lathe(c,'Tea_Pot',(x+.24,y,z),[(0,0),(.10,0),(.15,.07),(.15,.22),(.11,.26),(0,.26)],'steel')
    tube(c,'Tea_Pot_Handle',[(x+.15,y+.02,z+.24),(x+.12,y+.11,z+.34),(x+.35,y+.11,z+.34),(x+.34,y+.02,z+.24)],.015,'metal')
    tube(c,'Tea_Pot_Spout',[(x+.36,y,z+.11),(x+.44,y,z+.19),(x+.48,y,z+.25)],.022,'steel')
    cylinder(c,'Tea_Lid',(x+.24,y,z+.28),.10,.025,'steel')
    box(c,'Cup_Tray',(x+.15,y-.32,z+.027),(.48,.27,.05),'cream',.015)
    for i in range(4):
        cx=x-.015+(i%2)*.16;cy=y-.37+(i//2)*.12
        vessel(c,'Tea_Glass',(cx,cy,z+.053),.038,.092,'glass',.005)
        cylinder(c,'Iced_Tea',(cx,cy,z+.106),.031,.066,'broth',bevel=.001)
        for j in range(2):box(c,'Ice_Cube',(cx+j*.018-.009,cy,z+.14),(.018,.018,.018),'glass',.004)

c=asset('TeaShoulderPole','Gánh trà đá quang gánh','vendor',carry_anchor=[0,0,1.05])
for x in (-.65,.65):
    vessel(c,'Carry_Basket',(x,0,0),.29,.24,'wood',.018)
    for y in (-.19,.19):beam(c,'Basket_Hanger',(x,y,.22),(x*.85,y*.6,1.01),.018,'wood')
tube(c,'Shoulder_Pole',[(-1.02,0,.93),(-.70,0,1.03),(0,0,1.12),(.70,0,1.03),(1.02,0,.93)],.027,'wood')
for x in (-.65,.65):cylinder(c,'Basket_Tray',(x,0,.2375),.27,.025,'cream')
box(c,'Carry_Ice_Cooler',(-.65,0,.45),(.43,.33,.40),'blue',.03)
box(c,'Carry_Cooler_Lid',(-.65,0,.6725),(.45,.35,.045),'cream',.015)
lathe(c,'Carry_Tea_Pot',(.65,0,.25),[(0,0),(.10,0),(.13,.08),(.13,.22),(.10,.25),(0,.25)],'steel')
tube(c,'Carry_Kettle_Spout',[(.77,0,.37),(.83,0,.45),(.85,0,.49)],.018,'steel')
tube(c,'Carry_Kettle_Handle',[(.55,.02,.48),(.55,.10,.58),(.75,.10,.58),(.75,.02,.48)],.012,'metal')
cylinder(c,'Carry_Kettle_Lid',(.65,0,.512),.10,.025,'steel')
for i in range(3):vessel(c,'Spare_Cup',(.54+i*.10,-.17,.25),.032,.08,'cream',.005)
c=asset('TeaServiceSet','Bộ thùng đá, ấm trà và ly','vendor');tea_details(c)

def food_space(key,cart,label):
    c=asset(key,label,'vendor_space',clearance_polygon_m=[[-2,-2.25],[2,-2.25],[2,1.5],[-2,1.5]],
            service_anchor=[0,-.45,0],work_anchor=[0,1.25,0],passage_side='front',suggested_walkway_m=1.6)
    inst(c,cart,(0,.45,0));inst(c,'PlasticTableLow',(-.88,-1.05,0));inst(c,'PlasticTableLow',(.88,-1.05,0))
    for x in (-.88,.88):
        inst(c,'PlasticChairLow',(x,-1.66,0),math.pi);inst(c,'PlasticStoolLow',(x,-.44,0))
        bowl(c,(x,-1.05,.485),'miquang' if cart=='MiQuangCounter' else 'noodle') if cart!='BanhMiCart' else sphere(c,'Served_Bread',(x,-1.05,.55),(.13,.05,.05),'bread')
    inst(c,'StreetUmbrella',(1.50,.15,0))
food_space('HuTieuSpace','HuTieuCart','Không gian bán hủ tiếu')
food_space('BanhMiSpace','BanhMiCart','Không gian bán bánh mì')
food_space('MiQuangSpace','MiQuangCounter','Không gian bán mì Quảng')
c=asset('SidewalkTeaSpace','Gánh trà đá và chỗ ngồi vỉa hè','vendor_space',clearance_polygon_m=[[-2,-2.25],[2,-2.25],[2,1.5],[-2,1.5]],suggested_walkway_m=1.6)
inst(c,'TeaShoulderPole',(0,.50,0));inst(c,'TeaServiceSet',(-1.10,-.6,0));inst(c,'PlasticTableLow',(.60,-.75,0))
for at in [(.0,-.75,0),(1.20,-.75,0),(.60,-1.4,0),(.60,-.1,0)]:inst(c,'PlasticStoolLow',at)
inst(c,'StreetUmbrella',(1.60,.40,0))

# Merge manufactured parts by pigment, keeping separate materials/collection instances.
# This cuts draw calls and keeps real source meshes, UVs and bevels baked into geometry.
for c in ASSETS.values():
    grouped={}
    for ob in c.objects:
        if ob.type=='MESH':grouped.setdefault(ob.data.materials[0].name,[]).append(ob)
    for mat,parts in grouped.items():
        if len(parts)<2:continue
        bpy.ops.object.select_all(action='DESELECT')
        for ob in parts:ob.select_set(True)
        bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
        parts[0].name=c.name+'_'+mat
for c in ASSETS.values():
    for ob in c.objects:
        if ob.type=='MESH':
            bpy.context.view_layer.objects.active=ob;ob.select_set(True)
            bpy.ops.object.transform_apply(location=True,rotation=True,scale=True);ob.select_set(False)
            bm=bmesh.new();bm.from_mesh(ob.data)
            bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-6)
            bm.to_mesh(ob.data);bm.free()

def worldpoints(c,matrix=Matrix.Identity(4)):
    pts=[]
    for ob in c.objects:
        if ob.type=='MESH':pts.extend(matrix@ob.matrix_local@v.co for v in ob.data.vertices)
        elif ob.instance_collection:pts.extend(worldpoints(ob.instance_collection,matrix@ob.matrix_local))
    return pts
# Ground the manufactured pieces exactly; sloped plastic feet need a tiny offset.
for key,c in ASSETS.items():
    if not any(o.type=='EMPTY' and o.instance_collection for o in c.objects):
        ground_z=min(v.z for v in worldpoints(c))
        for ob in c.objects:
            if ob.type=='MESH':
                for v in ob.data.vertices:v.co.z-=ground_z
        for field in ('seat_height_m','table_height_m'):
            if field in META[key]:META[key][field]-=ground_z
        if 'seat_points' in META[key]:
            for point in META[key]['seat_points']:point[2]-=ground_z
import sys
sys.path.insert(0,str(ROOT.parent/'procedural_building'))
from city_low_poly import apply_city_style
from retro_low_poly import apply_retro_style,RETRO
apply_city_style()
apply_retro_style()
manifest={'schema':'sandtable.street-props.v1','revision':REV,'visual_profile':RETRO,'units':'metre','axes':{'up':'+Z','front':'-Y'},'pivot':'ground-center','blend':'street_retro_kit.blend','glb_export_run':False,
    'building_kit':'../procedural_building/retro/modular_building_kit.blend','provenance':'Original authored procedural meshes, seed 7261; city-style flat materials. Arial system font for embedded sign geometry.', 'assets':[]}
for key,c in ASSETS.items():
    pts=worldpoints(c);lo=[min(v[i] for v in pts) for i in range(3)];hi=[max(v[i] for v in pts) for i in range(3)]
    if META[key]['category']=='vendor_space':
        META[key]['clearance_polygon_m']=[[lo[0]-.2,lo[1]-.2],[hi[0]+.2,lo[1]-.2],[hi[0]+.2,hi[1]+.2],[lo[0]-.2,hi[1]+.2]]
    manifest['assets'].append({'id':'street/'+key,'collection':c.name,'bounds_m':{'min':lo,'max':hi},'dimensions_m':[hi[i]-lo[i] for i in range(3)],'collision':{'type':'box','center':[(lo[i]+hi[i])/2 for i in range(3)],'size':[hi[i]-lo[i] for i in range(3)],'recommendation':'coarse placement only; split legs/supports for walkable vendor spaces'},**META[key]})
    # Keep asset sources local; showroom instances do the display placement.
    scene.collection.children.unlink(c);c.use_fake_user=True

# A compact catalog of normalized display instances; source dimensions remain in metres.
display=bpy.data.collections.new('ST_Catalog_Display');scene.collection.children.link(display)
for index,(key,c) in enumerate(ASSETS.items()):
    row,col=divmod(index,5);dims=manifest['assets'][index]['dimensions_m'];factor=2.5/max(max(dims),1)
    ob=inst(display,key,(col*4.0,row*4.0,0));ob.scale=(factor,)*3
    text(display,META[key]['label_vi'],(col*4.0,row*4.0-1.45,.02),.15,'black')
ground=bpy.data.collections.new('ST_Review_Stage');scene.collection.children.link(ground)
box(ground,'Catalog_Floor',(8,8,-.10),(22,24,.20),'pavement',.02)

# Street review has metre-scale instances and a protected pedestrian corridor.
demo=bpy.data.scenes.new('Street_Retro_Sidewalk');demo.unit_settings.system='METRIC';demo.unit_settings.scale_length=1
layout=bpy.data.collections.new('ST_Sidewalk_Layout');demo.collection.children.link(layout)
box(layout,'Sidewalk',(0,0,-.09),(25,9,.18),'pavement',.03)
box(layout,'Road',(0,-6.3,-.18),(25,3.7,.18),'black',.02)
box(layout,'Curb',(0,-4.45,-.04),(25,.14,.22),'cream',.02)
placements=[]
for key,x in [('HuTieuSpace',-9),('BanhMiSpace',-3),('MiQuangSpace',3),('SidewalkTeaSpace',9)]:
    inst(layout,key,(x,.85,0));placements.append({'asset_id':'street/'+key,'position_m':[x,.85,0],'rotation_z_degrees':0})
for key,at,a in [('StoneBenchBack',(-10,3.60,0),0),('PublicBenchBack',(-6,3.60,0),0),('TrashBinWheelie',(-12,2.7,0),0),('TrashBinRound',(12,2.7,0),0),('UtilityPole',(-12,-4.1,0),0),('StreetLampSingle',(-6,-4.1,0),0),('StreetLampDouble',(6,-4.1,0),0),('FireHydrant',(12,-4.1,0),0)]:
    inst(layout,key,at,a);placements.append({'asset_id':'street/'+key,'position_m':list(at),'rotation_z_degrees':math.degrees(a)})
layoutdata={'schema':'sandtable.street-layout.v1','revision':REV,'visual_profile':RETRO,'units':'metre','placements':placements,
    'walkway':{'bounds_xy_m':[[-11.5,-3.50],[11.5,-1.90]],'width_m':1.60,'keep_clear':True},
    'rules':{'vendor_spacing_m':6,'align_service_front_to_road':True,'hydrant_clearance_m':.65,'wire_endpoints_from_asset_sockets':True,'seating_faces_table_or_service':True}}

def setup(s,location,target,scale):
    world=bpy.data.worlds.new(s.name+'_World');world.use_nodes=True;world.node_tree.nodes.get('Background').inputs[0].default_value=(.72,.78,.83,1);world.node_tree.nodes.get('Background').inputs[1].default_value=.65;s.world=world
    data=bpy.data.cameras.new(s.name+'_Camera');data.type='ORTHO';data.ortho_scale=scale;ob=bpy.data.objects.new(data.name,data);s.collection.objects.link(ob);ob.location=location;ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler();s.camera=ob
    for name,at,power,size in [('Key',(-6,-8,15),2200,8),('Fill',(12,3,12),1700,8)]:
        data=bpy.data.lights.new(s.name+name,'AREA');data.energy=power;data.shape='DISK';data.size=size;light=bpy.data.objects.new(data.name,data);s.collection.objects.link(light);light.location=at;light.rotation_euler=(Vector(target)-light.location).to_track_quat('-Z','Y').to_euler()
    s.render.engine='CYCLES';s.cycles.samples=24;s.cycles.use_denoising=True;s.view_settings.view_transform='AgX'
    s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG'
setup(scene,(29,-27,32),(8,9,1),30)
setup(demo,(28,-32,25),(0,0,1.1),31)
scene.render.resolution_x=2100;scene.render.resolution_y=1900
demo.render.resolution_x=2000;demo.render.resolution_y=1300
for im in bpy.data.images:
    if im.has_data:im.pack()
for fontdata in bpy.data.fonts:
    if fontdata.filepath and fontdata.filepath!='<builtin>':fontdata.pack()
(ROOT/'asset_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
(ROOT/'sidewalk_layout.json').write_text(json.dumps(layoutdata,ensure_ascii=False,indent=2),encoding='utf8')
bpy.context.window.scene=demo
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_rotation=demo.camera.rotation_euler.to_quaternion();area.spaces.active.region_3d.view_location=(0,0,1);area.spaces.active.region_3d.view_distance=30
bpy.data.texts.new('Street_Kit_README').write('Street retro kit. Asset Browser > Current File. Scene Street_Retro_Catalog / Street_Retro_Sidewalk. Dimensions and placement anchors: asset_manifest.json. No GLB exported.')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'street_retro_kit.blend'))
print('STREET_KIT_SAVED',len(ASSETS),'assets',flush=True)
