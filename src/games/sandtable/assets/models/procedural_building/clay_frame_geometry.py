"""Continuous rounded frames: one solid mesh, no four-piece corner joints."""
import math
import bpy,bmesh


def rounded_loop(left,right,bottom,top,radius,segments=10):
    result=[]
    for cx,cz,start in ((right-radius,bottom+radius,-math.pi/2),
                        (right-radius,top-radius,0),
                        (left+radius,top-radius,math.pi/2),
                        (left+radius,bottom+radius,math.pi)):
        for i in range(segments+1):
            angle=start+i*math.pi/(2*segments)
            result.append((cx+radius*math.cos(angle),cz+radius*math.sin(angle)))
    return result


def continuous_frame(collection,style,role,width,height,sill,curvature=None):
    closed=role=='Window'
    left,right=1-width/2-.09,1+width/2+.09
    inner_left,inner_right=1-width/2+.05,1+width/2-.05
    bottom,top=sill-.07,sill+height+.07
    inner_bottom,inner_top=sill+.01,sill+height-.03
    front,back=-.145,.035
    vertices=[];faces=[]
    if closed:
        outer=rounded_loop(left,right,bottom,top,.105)
        inner=rounded_loop(inner_left,inner_right,inner_bottom,inner_top,.035)
        n=len(outer)
        for y in (front,back):
            vertices.extend((x,y,z) for x,z in outer+inner)
        for i in range(n):
            j=(i+1)%n
            faces += [(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),
                      (i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)]
    else:
        outline=[(left,0)]
        for cx,cz,a0,a1 in ((left+.105,top-.105,math.pi,math.pi/2),
                           (right-.105,top-.105,math.pi/2,0)):
            for i in range(11):
                a=a0+(a1-a0)*i/10;outline.append((cx+.105*math.cos(a),cz+.105*math.sin(a)))
        outline.extend([(right,0),(inner_right,0)])
        for cx,cz,a0,a1 in ((inner_right-.035,inner_top-.035,0,math.pi/2),
                           (inner_left+.035,inner_top-.035,math.pi/2,math.pi)):
            for i in range(11):
                a=a0+(a1-a0)*i/10;outline.append((cx+.035*math.cos(a),cz+.035*math.sin(a)))
        outline.append((inner_left,0));n=len(outline)
        for y in (front,back):vertices.extend((x,y,z) for x,z in outline)
        faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]
        faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    data=bpy.data.meshes.new('PBK_Continuous_Frame_'+style+'_'+role)
    data.from_pydata(vertices,[],faces);data.materials.append(bpy.data.materials['PBK_'+style+'_trim'])
    ob=bpy.data.objects.new(data.name,data);collection.objects.link(ob)
    bm=bmesh.new();bm.from_mesh(data);bm.normal_update()
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    edges=[e for e in bm.edges if len(e.link_faces)==2 and e.calc_face_angle(0)>.5
           and not all(abs(v.co.z)<1e-6 for v in e.verts)]
    bmesh.ops.bevel(bm,geom=edges,offset=.012,segments=4,profile=.5,affect='EDGES',
                    clamp_overlap=True,loop_slide=True,material=-1,miter_inner='SHARP',miter_outer='SHARP')
    # Subdivide before bending: the top/bottom border follows the facade arc.
    if curvature:
        for x in (j/24 for j in range(1,48)):
            bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
                dist=1e-6,plane_co=(x,0,0),plane_no=(1,0,0),clear_inner=False,clear_outer=False)
        radius,angle=curvature
        for v in bm.verts:
            x,y,z=v.co;theta=-math.pi/2+x*angle/2
            v.co=((radius-y)*math.cos(theta),radius+(radius-y)*math.sin(theta),z)
    bm.normal_update()
    for f in bm.faces:f.smooth=True
    for e in bm.edges:e.smooth=len(e.link_faces)==2 and e.calc_face_angle(0)<math.radians(40)
    bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
    bm.to_mesh(data);bm.free();data.update()
    uv=data.uv_layers.new(name='ClayUV')
    for face in data.polygons:
        axis=max(range(3),key=lambda i:abs(face.normal[i]));axes=((1,2),(0,2),(0,1))[axis]
        for loop in face.loop_indices:
            p=data.vertices[data.loops[loop].vertex_index].co;uv.data[loop].uv=(p[axes[0]]*4,p[axes[1]]*4)
    data['clay_processed']=True
    ob['continuous_frame']=True;ob['frame_role']=role;ob['glass_grid']=False
    ob['frame_clear_width_m']=width-.10;ob['frame_closed_ring']=closed
    return ob
