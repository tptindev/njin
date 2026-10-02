"""Original retro railing modules. Authoring only: never exports GLB."""
from pathlib import Path
import json,math
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parent
REV='retro-railings-v1'
STYLES=('Indochine','Modern','Brick')
DESIGNS=('StraightVertical','StraightHorizontal','SquareGrid','SquareFrame',
         'DiagonalCross','DiagonalSingle','Wave','Arch','Diamond','SolidParapet',
         'WoodFence','BridgeHeavy','RiversideBars','LakeLow','Corner90',
         'ArcR2A45','ArcR4A30','StairSlope','StairHandrail','WallHandrail',
         'EndPost','TransitionPost')

def route(design,u):
    if design.startswith('Arc'):
        r,a=(2,math.pi/4) if design=='ArcR2A45' else (4,math.pi/6)
        return Vector((r*math.sin(u*a),r*(1-math.cos(u*a)),0))
    if design=='Corner90':return Vector((4*u,0,0)) if u<=.5 else Vector((2,4*u-2,0))
    if design.startswith('Stair'):return Vector((0,3.75*u,3*u))
    if design in ('EndPost','TransitionPost'):return Vector((0,0,0))
    return Vector((2*u,0,0))

def tube(verts,faces,points,radius=.025,sides=6):
    """Shared rings give a continuous, capped low-poly rail through bends."""
    points=[Vector(p) for p in points];start=len(verts)
    previous_x=None
    for i,p in enumerate(points):
        tangent=(points[min(i+1,len(points)-1)]-points[max(i-1,0)]).normalized()
        ref=Vector((0,0,1)) if abs(tangent.z)<.95 else Vector((1,0,0))
        x=previous_x-tangent*previous_x.dot(tangent) if previous_x is not None else tangent.cross(ref)
        if x.length<1e-8:x=tangent.cross(ref)
        x.normalize();y=tangent.cross(x).normalized();previous_x=x
        for k in range(sides):
            a=2*math.pi*k/sides
            verts.append(tuple(p+radius*(x*math.cos(a)+y*math.sin(a))))
    faces.append(tuple(start+k for k in reversed(range(sides))))
    for i in range(len(points)-1):
        for k in range(sides):
            a=start+i*sides+k;b=start+i*sides+(k+1)%sides
            faces.append((a,b,b+sides,a+sides))
    faces.append(tuple(start+(len(points)-1)*sides+k for k in range(sides)))

def cuboid(verts,faces,lo,hi):
    start=len(verts)
    verts.extend((lo[0] if x==0 else hi[0],lo[1] if y==0 else hi[1],lo[2] if z==0 else hi[2])
                 for x,y,z in [(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)])
    faces.extend(tuple(start+i for i in f) for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])

def mesh(c,name,verts,faces,mat):
    if not verts:return
    data=bpy.data.meshes.new(name);data.from_pydata(verts,[],faces);data.materials.append(mat)
    data.update();data['retro_profile']='city-retro-v2-tight-joints'
    ob=bpy.data.objects.new(name,data);c.objects.link(ob)
    return ob

def build(style,design):
    c=bpy.data.collections.new('RAIL_'+style+'_'+design);c.asset_mark()
    c.asset_data.description='Retro modular railing: '+design+' / '+style
    for tag in ('retro','railings',style,design):c.asset_data.tags.new(tag)
    material_role='wood' if design=='WoodFence' else 'concrete' if design=='SolidParapet' else 'metal'
    mat=bpy.data.materials['PBK_'+style+'_'+material_role]
    height=.9 if design in ('LakeLow','WallHandrail','StairHandrail') else 1.1
    c['asset_id']='railings/'+style+'/'+design;c['revision']=REV
    body_v=[];body_f=[]
    is_post=design in ('EndPost','TransitionPost')
    is_wall=design in ('WallHandrail','StairHandrail')
    count=16 if design.startswith('Arc') else 2 if design=='Corner90' else 1
    samples=[route(design,i/count) for i in range(count+1)]
    span=sum((b-a).length for a,b in zip(samples,samples[1:]))
    radius=.045 if design=='BridgeHeavy' else .032 if design=='WoodFence' else .025
    square=design in ('SquareGrid','SquareFrame','WoodFence','BridgeHeavy')
    sides=4 if square else 6
    def bar(points,r=radius):tube(body_v,body_f,points,r,sides)
    def lifted(u,z):return route(design,u)+Vector((0,0,z))
    if not is_post:
        bar([p+Vector((0,0,height)) for p in samples])
        if not is_wall:
            bar([p+Vector((0,0,.18)) for p in samples])
        if design=='SolidParapet':
            cuboid(body_v,body_f,(0,-.09,0),(2,.09,height-.03))
        elif is_wall:
            for u in (.1,.5,.9):
                p=lifted(u,height-.015)
                offset=Vector((-.10,0,-.08)) if design.startswith('Stair') else Vector((0,.10,-.08))
                bar([p+offset,p+Vector((0,0,-.08)),p])
        elif design=='StraightHorizontal':
            for z in (.45,.75):bar([lifted(0,z),lifted(1,z)])
        elif design in ('SquareGrid','SquareFrame'):
            for u in (.25,.5,.75):bar([lifted(u,.18),lifted(u,height)])
            if design=='SquareGrid':bar([lifted(0,.64),lifted(1,.64)])
        elif design in ('DiagonalCross','DiagonalSingle'):
            for a,b in ((0,.5),(.5,1)):
                bar([lifted(a,.18),lifted(b,height)])
                if design=='DiagonalCross':bar([lifted(a,height),lifted(b,.18)])
        elif design=='Diamond':
            for u in (.25,.75):bar([lifted(u-.25,.64),lifted(u,height),lifted(u+.25,.64),lifted(u,.18),lifted(u-.25,.64)])
        elif design in ('Wave','Arch'):
            for a in (0,.5):
                points=[lifted(a+.5*i/12,.50+.30*math.sin(math.pi*i/12)) for i in range(13)] if design=='Arch' else [lifted(a+.5*i/12,.60+.17*math.sin(2*math.pi*i/12)) for i in range(13)]
                bar(points)
            for u in (.25,.5,.75):bar([lifted(u,.18),lifted(u,height)])
        else:
            # Vertical infill follows the same curve or incline as its rails.
            spacing=.25 if design=='WoodFence' else .20
            for i in range(1,max(2,math.ceil(span/spacing))):
                u=i/max(2,math.ceil(span/spacing));bar([lifted(u,.18),lifted(u,height)],.032 if design=='WoodFence' else .012)
    if not is_wall:
        positions=[0.] if is_post else [i/max(1,math.ceil(span/.65)) for i in range(math.ceil(span/.65)+1)]
        if design=='Corner90':positions=sorted(set(positions+[.5]))
        for i,u in enumerate(positions):
            v=[];f=[];p=route(design,u)
            # Feet of stair posts sit on the actual treads, not below them.
            base=math.ceil((p.y-1e-7)/.25)*.2 if design.startswith('Stair') and u>0 else p.z
            top=p.z+(1.3 if design=='TransitionPost' else height)+.02
            half=.055 if design=='BridgeHeavy' else .035
            cuboid(v,f,(p.x-half,p.y-half,base),(p.x+half,p.y+half,top))
            part='StartPost' if i==0 else 'EndPost' if i==len(positions)-1 else None
            if part:mesh(c,c.name+'_'+part,v,f,mat)
            else:
                shift=len(body_v);body_v.extend(v);body_f.extend(tuple(k+shift for k in face) for face in f)
    mesh(c,c.name+'_RailBody',body_v,body_f,mat)
    start=route(design,0);end=route(design,1)
    tangent0=route(design,.001)-start;tangent1=end-route(design,.999)
    def yaw(t):return math.degrees(math.atan2(t.y,t.x)) if t.length else 0
    points=[v.co for o in c.objects for v in o.data.vertices]
    entry={'id':c['asset_id'],'collection':c.name,'style':style,'design':design,'revision':REV,
           'path':'modules/'+style+'/'+design+'.glb','height_m':height,'span_m':round(span,6),
           'material_role':material_role,'bounds_m':{'min':[min(p[i] for p in points) for i in range(3)],'max':[max(p[i] for p in points) for i in range(3)]},
           'sockets':{'start':list(start),'end':list(end),'handrail_start':list(start+Vector((0,0,height))),'handrail_end':list(end+Vector((0,0,height)))},
           'connection_orientation':{'start_yaw_degrees':yaw(tangent0),'end_yaw_degrees':yaw(tangent1)},
           'boundary_post_nodes':{'start':c.name+'_StartPost' if not is_wall else None,'end':c.name+'_EndPost' if not is_wall and not is_post else None},
           'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in c.objects),
           'collision':'separate edge barrier; posts and rods do not define a filled volume',
           'animated':False}
    c['sockets_json']=json.dumps(entry['sockets']);return c,entry

def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    source=ROOT.parent/'procedural_building/retro/modular_building_kit.blend'
    with bpy.data.libraries.load(str(source),link=False) as (src,dst):
        dst.materials=['PBK_'+style+'_'+role for style in STYLES for role in ('metal','wood','concrete')]
    scene=bpy.context.scene;scene.name='Railings_Retro_Catalog'
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    entries=[]
    for si,style in enumerate(STYLES):
        for di,design in enumerate(DESIGNS):
            c,entry=build(style,design);entries.append(entry)
            ob=bpy.data.objects.new('Display_'+style+'_'+design,None);ob.instance_type='COLLECTION';ob.instance_collection=c
            ob.location=((di%6)*5,(di//6+si*4)*6,0);scene.collection.objects.link(ob)
    manifest={'schema':'sandtable.railings.v1','revision':REV,'visual_profile':'city-retro-v2-tight-joints',
              'units':'metre','axes':{'up':'+Z','route_direction':'+X; stair climbs +Y'},
              'blend':'railings_retro_kit.blend','pivot':'start socket at floor/path datum','assets':entries,'glb_export_run':False,
              'provenance':'Original authored procedural geometry; existing Sandtable retro material palette.'}
    (ROOT/'asset_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    (ROOT/'source_status.json').write_text(json.dumps({'revision':REV,'source_saved':True,'export_required':True,'glb_export_run':False},indent=2)+'\n')
    bpy.data.texts.new('build_railings.py').write(Path(__file__).read_text(encoding='utf8'))
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'railings_retro_kit.blend'))
    print('RAILINGS_SOURCE_SAVED',len(entries),'assets',sum(e['triangles'] for e in entries),'triangles',flush=True)

if __name__=='__main__':main()
