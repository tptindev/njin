"""Reproducible Blender 4.4 character factory. No external Python dependencies.
blender --background --factory-startup --python generate.py -- --output DIR --seed 37
Coordinates: Z up, -Y forward; exported glTF: Y up, +Z forward. Metres.
"""
import argparse
import hashlib
import json
import math
import random
import sys
from pathlib import Path

import bpy
from mathutils import Vector, Euler

CLIPS = {
    'Idle_Loop': (2.4, True), 'Idle_Talking_Loop': (2.4, True),
    'Walk_Loop': (1.0, True), 'Jog_Fwd_Loop': (.72, True),
    'Sprint_Loop': (.56, True), 'Punch_Jab': (.6, False),
    'Punch_Cross': (.75, False), 'Hit_Chest': (.65, False),
    'Death01': (1.5, False), 'Sitting_Idle_Loop': (2.4, True),
    'Crouch_Idle_Loop': (2.4, True), 'Pistol_Idle_Loop': (2.0, True),
    'Pistol_Shoot': (.4, False),
}
PRESETS = [
    dict(name='reference', width=1.0, head=1.0, shirt=[.48,.105,.08], pants=[.065,.20,.28], skin=[.55,.29,.115], accessory='none'),
    dict(name='lookout', width=.85, head=.95, shirt=[.13,.28,.20], pants=[.11,.14,.16], skin=[.42,.205,.08], accessory='cap'),
    dict(name='collector', width=1.12, head=1.04, shirt=[.62,.39,.13], pants=[.12,.19,.25], skin=[.63,.36,.18], accessory='satchel'),
    dict(name='enforcer', width=1.32, head=1.08, shirt=[.20,.16,.28], pants=[.09,.105,.13], skin=[.46,.23,.10], accessory='hair'),
]

def material(name, color):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Roughness'].default_value = .88
    return m

class Builder:
    def __init__(self, cfg, seed):
        self.cfg, self.rng = cfg, random.Random(seed)
        self.vertices, self.faces, self.weights, self.mats = [], [], [], []
        self.palette = []
        for name, rgb in [('shirt',cfg['shirt']),('pants',cfg['pants']),('skin',cfg['skin']),('shoe',[.065,.055,.05]),('accent',[.72,.59,.32])]:
            for shade in [1, .94, 1.045]:
                self.palette.append(material(cfg['name']+'_'+name+str(shade), [c*shade for c in rgb]))

    def face(self, ids, mat):
        self.faces.append(ids)
        self.mats.append(mat*3)

    def tube(self, centers, radii, weights, mat, sides=8):
        base = len(self.vertices)
        axis = (Vector(centers[-1])-Vector(centers[0])).normalized()
        u = Vector((0,1,0)).cross(axis).normalized()
        v = axis.cross(u).normalized()
        for c, r, w in zip(centers,radii,weights):
            for i in range(sides):
                a = 2*math.pi*i/sides + math.pi/8
                self.vertices.append(Vector(c)+u*(math.cos(a)*r[0])+v*(math.sin(a)*r[1]))
                self.weights.append(w)
        self.face(tuple(base+i for i in reversed(range(sides))),mat)
        for j in range(len(centers)-1):
            for i in range(sides):
                a=base+j*sides+i; b=base+j*sides+(i+1)%sides
                self.face((a,b,b+sides,a+sides),mat)
        self.face(tuple(base+(len(centers)-1)*sides+i for i in range(sides)),mat)

    def ico(self, center, scale, bone, mat, subdivisions=2):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=1, location=center)
        obj=bpy.context.object
        base=len(self.vertices)
        self.vertices.extend(Vector(center)+Vector((v.co.x*scale[0],v.co.y*scale[1],v.co.z*scale[2])) for v in obj.data.vertices)
        self.weights.extend([{bone:1}] * len(obj.data.vertices))
        for p in obj.data.polygons: self.face(tuple(base+i for i in p.vertices),mat)
        bpy.data.objects.remove(obj,do_unlink=True)

    def build(self):
        w=self.cfg['width']; h=self.cfg['head']
        bones = [('root',(0,0,0),(0,0,.15),None),
                 ('pelvis',(0,0,1.08),(0,0,1.2),'root'),
                 ('spine',(0,0,1.2),(0,0,1.43),'pelvis'),
                 ('chest',(0,0,1.43),(0,0,1.61),'spine'),
                 ('neck',(0,0,1.61),(0,0,1.69),'chest'),
                 ('head',(0,0,1.69),(0,0,1.80),'neck')]
        self.tube([(0,0,z) for z in [1.065,1.13,1.28,1.45,1.57,1.62]],
                  [(a*w,b) for a,b in [(.112,.068),(.104,.062),(.088,.053),(.094,.056),(.117,.064),(.061,.045)]],
                  [{'pelvis':1},{'pelvis':.7,'spine':.3},{'spine':1},{'chest':.7,'spine':.3},{'chest':1},{'chest':1}],0)
        self.tube([(0,0,1.59),(0,0,1.71)],[(.023,.023)]*2,[{'neck':1}]*2,2)
        self.ico((0,0,1.76),(.067*h,.063*h,.07), 'head',2)
        self.tube([(0,0,1.03),(0,0,1.11)],[(.094*w,.058)]*2,[{'pelvis':1}]*2,1)
        for side, s in [('L',1),('R',-1)]:
            hip=(s*.068*w,0,1.08); knee=(s*.10*w,-.012,.59); ankle=(s*.14*w,0,.08)
            shoulder=(s*.102*w,0,1.565); elbow=(s*.20*w,0,1.28); wrist=(s*.295*w,-.008,1.005)
            def nm(n): return n+'.'+side
            bones.extend([(nm('thigh'),hip,knee,'pelvis'),(nm('shin'),knee,ankle,nm('thigh')),
                          (nm('foot'),ankle,(ankle[0],-.10,.045),nm('shin')),
                          (nm('upper_arm'),shoulder,elbow,'chest'),(nm('forearm'),elbow,wrist,nm('upper_arm')),
                          (nm('hand'),wrist,(wrist[0]+s*.012,-.012,.97),nm('forearm'))])
            self.tube([hip, (s*.088*w,-.006,.81),knee,(s*.122*w,-.006,.32),ankle],
                      [(.044*w,.044),(.032*w,.033),(.027*w,.029),(.025*w,.026),(.024*w,.024)],
                      [{nm('thigh'):1},{nm('thigh'):1},{nm('thigh'):.5,nm('shin'):.5},{nm('shin'):1},{nm('shin'):1}],1,6)
            self.ico((ankle[0],-.034,.039),(.036*w,.083,.044),nm('foot'),3,1)
            sleeve_end=Vector(shoulder).lerp(Vector(elbow),.43)
            self.tube([shoulder,sleeve_end],[(.050*w,.052),(.038*w,.040)],[{nm('upper_arm'):1}]*2,0,6)
            self.tube([sleeve_end,elbow,wrist],[(.016,.017),(.014,.015),(.011,.013)],
                      [{nm('upper_arm'):1},{nm('upper_arm'):.5,nm('forearm'):.5},{nm('forearm'):1}],2,6)
            self.ico(Vector(wrist)+Vector((s*.006,0,-.018)),(.020,.018,.026),nm('hand'),2,1)
        accessory=self.cfg['accessory']
        if accessory in ('cap','hair'):
            self.ico((0,.003,1.805),(.069*h,.065*h,.036),'head',1 if accessory=='cap' else 3,1)
            if accessory=='cap': self.ico((0,-.055,1.795),(.067,.065,.009),'head',1,1)
        if accessory=='satchel':
            self.tube([(-.094*w,-.07,1.53),(.093*w,-.075,1.13)],[(.012,.007)]*2,[{'chest':1},{'pelvis':1}],3,4)
            self.ico((.12*w,-.012,1.16),(.049,.051,.068),'pelvis',4,1)
        mesh=bpy.data.meshes.new(self.cfg['name']+'_mesh')
        mesh.from_pydata(self.vertices,[],self.faces); mesh.update()
        obj=bpy.data.objects.new(self.cfg['name'],mesh); bpy.context.collection.objects.link(obj)
        for m in self.palette: mesh.materials.append(m)
        for p,idx in zip(mesh.polygons,self.mats): p.material_index=idx
        for name,*_ in bones: obj.vertex_groups.new(name=name)
        for i,weight in enumerate(self.weights):
            for name,value in weight.items(): obj.vertex_groups[name].add([i],value,'REPLACE')
        arm=bpy.data.armatures.new(self.cfg['name']+'_skeleton')
        rig=bpy.data.objects.new(self.cfg['name']+'_rig',arm); bpy.context.collection.objects.link(rig)
        bpy.context.view_layer.objects.active=rig; rig.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        for name,head,tail,parent in bones:
            b=arm.edit_bones.new(name); b.head=head; b.tail=tail
            if parent: b.parent=arm.edit_bones[parent]
        bpy.ops.object.mode_set(mode='OBJECT')
        mod=obj.modifiers.new('Skin','ARMATURE'); mod.object=rig
        obj.parent=rig; rig.show_in_front=True
        rig.scale=(self.cfg.get('height',1.83)/1.83,)*3
        rig['generator']='clay_people/generate.py'; rig['identity']=json.dumps(self.cfg)
        return obj,rig

def animate(rig):
    # Authored FK animation, sampled at 30 Hz for portable glTF playback.
    # Long limbs and small amplitude preserve the reference silhouette at RTS scale.
    rig.animation_data_create()
    for name,(duration,loop) in CLIPS.items():
        action=bpy.data.actions.new(name); action.use_fake_user=True
        rig.animation_data.action=action
        count=round(duration*30)
        for f in range(count+1):
            t=f/count; wave=math.sin(t*math.tau); pulse=math.sin(math.pi*t)**2
            for b in rig.pose.bones:
                b.rotation_mode='QUATERNION'; b.rotation_quaternion=(1,0,0,0); b.location=(0,0,0)
            def rot(b,x=0,y=0,z=0):
                bone=rig.data.bones[b]
                # Convert a world-axis bend to bone-local coordinates; default
                # Blender rolls differ for left/right slanted limbs.
                sign=-1 if (bone.tail_local-bone.head_local).z<0 else 1
                rest=bone.matrix_local.to_quaternion()
                delta=Euler((sign*x,y,z)).to_quaternion()
                rig.pose.bones[b].rotation_quaternion=rest.inverted() @ delta @ rest
            root=rig.pose.bones['root']
            rot('chest',.012*wave)
            if name in ('Walk_Loop','Jog_Fwd_Loop','Sprint_Loop'):
                amp={'Walk_Loop':.30,'Jog_Fwd_Loop':.48,'Sprint_Loop':.68}[name]
                root.location.y=.012*(1-math.cos(t*math.tau*2))
                rot('spine',.03 if amp==.30 else .11)
                for side,phase in [('L',0),('R',math.pi)]:
                    v=math.sin(t*math.tau+phase)
                    rot('thigh.'+side,amp*v)
                    rot('shin.'+side,-max(0,-v)*amp*1.7)
                    rot('foot.'+side,max(0,-v)*amp*.45)
                    rot('upper_arm.'+side,-v*amp*.7)
                    rot('forearm.'+side,.18 if amp==.3 else .85)
            elif name=='Idle_Talking_Loop':
                rot('head',.035*wave,0,.10*wave)
                rot('upper_arm.R',.20+.12*wave,0,-.12)
                rot('forearm.R',.65+.18*wave)
            elif name in ('Punch_Jab','Punch_Cross'):
                side='L' if name=='Punch_Jab' else 'R'
                strike=max(0,1-abs(t-.38)/.22)
                rot('chest',0,0,(-1 if side=='L' else 1)*.3*pulse)
                rot('upper_arm.'+side,1.45*strike)
                rot('forearm.'+side,.8*pulse*(1-strike))
                rot('forearm.'+('R' if side=='L' else 'L'),.75*pulse)
            elif name=='Hit_Chest': rot('spine',-.30*pulse); rot('head',-.20*pulse)
            elif name=='Death01':
                fall=min(1,t/.72); fall=fall*fall*(3-2*fall)
                rot('root',-math.pi/2*fall)
                root.location=rig.data.bones['root'].matrix_local.to_quaternion().inverted() @ Vector((0,.2*fall,.065*fall))
                rot('upper_arm.L',0,0,.25*fall); rot('upper_arm.R',0,0,-.25*fall)
            elif name=='Sitting_Idle_Loop':
                root.location.y=-.49
                for side in ['L','R']:
                    rot('thigh.'+side,math.pi/2); rot('shin.'+side,-math.pi/2)
                    rot('upper_arm.'+side,.25); rot('forearm.'+side,.65)
            elif name=='Crouch_Idle_Loop':
                root.location.y=-.29
                rot('spine',.30)
                for side in ['L','R']:
                    rot('thigh.'+side,.85); rot('shin.'+side,-1.5); rot('foot.'+side,.65)
            elif name.startswith('Pistol_'):
                recoil=.18*pulse if name=='Pistol_Shoot' else .01*wave
                rot('upper_arm.R',1.4-recoil); rot('forearm.R',.12+recoil)
                rot('upper_arm.L',1.2,0,.30); rot('forearm.L',.35)
            # Keep the lowest sole on the floor during grounded locomotion.
            if name in ('Walk_Loop','Jog_Fwd_Loop','Sprint_Loop','Sitting_Idle_Loop','Crouch_Idle_Loop'):
                bpy.context.view_layer.update()
                obj=next(o for o in rig.children if o.type=='MESH')
                ev=obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
                low=min((obj.matrix_world @ v.co).z for v in ev.data.vertices)
                root.location.y-=low/rig.scale.z
            for b in rig.pose.bones:
                b.keyframe_insert('rotation_quaternion',frame=f+1,group=b.name)
                if b.name=='root': b.keyframe_insert('location',frame=f+1,group=b.name)
        action['loop']=loop
    rig.animation_data.action=bpy.data.actions['Idle_Loop']

def setup_scene():
    scene=bpy.context.scene
    for o in list(scene.objects): bpy.data.objects.remove(o,do_unlink=True)
    scene.render.engine='CYCLES'; scene.cycles.samples=24; scene.cycles.use_denoising=True
    scene.render.resolution_x=1200; scene.render.resolution_y=1000; scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'; scene.render.fps=30
    scene.world.color=(.30,.30,.30)
    bpy.ops.mesh.primitive_plane_add(size=200)
    ground=bpy.context.object; ground.name='STUDIO_Ground'
    ground.data.materials.append(material('Studio ivory',[.72,.69,.62]))
    bpy.ops.object.camera_add(location=(2.8,-5,2.6))
    cam=bpy.context.object; cam.name='STUDIO_Camera'
    cam.rotation_euler=(Vector((0,0,.95))-cam.location).to_track_quat('-Z','Y').to_euler()
    cam.data.type='ORTHO'; cam.data.ortho_scale=2.35; scene.camera=cam
    for name,loc,power,size in [('Key',(-3,-4,6),500,4),('Fill',(4,-1,3),260,3),('Rim',(1,3,5),420,3)]:
        bpy.ops.object.light_add(type='AREA',location=loc)
        light=bpy.context.object; light.name='STUDIO_'+name; light.data.energy=power; light.data.shape='DISK'; light.data.size=size
        light.rotation_euler=(Vector((0,0,1))-light.location).to_track_quat('-Z','Y').to_euler()
    return scene

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--output',default=str(Path(__file__).parent/'generated'))
    parser.add_argument('--seed',type=int,default=37); parser.add_argument('--config',help='JSON array of preset dictionaries')
    parser.add_argument('--count',type=int,help='Generate N deterministic identities instead of the four art presets')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    out=Path(args.output).resolve(); out.mkdir(parents=True,exist_ok=True)
    configs=json.loads(Path(args.config).read_text()) if args.config else PRESETS
    if args.count:
        rng=random.Random(args.seed); configs=[]
        for i in range(args.count):
            cfg=dict(rng.choice(PRESETS)); cfg.update(name='citizen_%04d'%i,width=rng.uniform(.82,1.32),head=rng.uniform(.92,1.10),height=rng.uniform(1.68,1.94),accessory=rng.choice(['none','cap','hair','satchel']))
            cfg['shirt']=[c*rng.uniform(.8,1.15) for c in cfg['shirt']]
            configs.append(cfg)
    if not configs or len({c['name'] for c in configs})!=len(configs): raise ValueError('Need unique, nonempty preset names')
    for c in configs:
        if not c['name'].replace('_','').isalnum(): raise ValueError('Use alphanumeric/underscore names')
        if not .65<=c['width']<=1.6 or not .8<=c['head']<=1.2: raise ValueError('Proportions exceed validated deformation range')
    scene=setup_scene(); records=[]; people=[]
    for index,cfg in enumerate(configs):
        # Keep action names identical in each self-contained file.
        for action in list(bpy.data.actions): bpy.data.actions.remove(action)
        obj,rig=Builder(cfg,args.seed+index).build(); animate(rig)
        scene.frame_set(1)
        bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); rig.select_set(True)
        bpy.context.view_layer.objects.active=rig
        dest=out/(cfg['name']+'.glb')
        bpy.ops.export_scene.gltf(filepath=str(dest),export_format='GLB',use_selection=True,
            export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True,
            export_frame_range=False,export_anim_slide_to_zero=True,export_yup=True,export_skins=True,export_def_bones=True)
        mesh=obj.data; mesh.calc_loop_triangles()
        records.append(dict(name=cfg['name'],file=dest.name,sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),
            vertices=len(mesh.vertices),triangles=len(mesh.loop_triangles),bones=len(rig.data.bones),parameters=cfg))
        # Each .blend preserves the rig, editable mesh and all 13 actions.
        bpy.ops.wm.save_as_mainfile(filepath=str(out/(cfg['name']+'.blend')))
        scene.render.filepath=str(out/(cfg['name']+'.png')); bpy.ops.render.render(write_still=True)
        people.append((obj,rig))
        obj.hide_render=True; obj.hide_set(True); rig.hide_set(True)
    # Contact sheet, with the source proportions shown beside identity variants.
    for i,(obj,rig) in enumerate(people):
        obj.hide_render=False; obj.hide_set(False); rig.hide_set(False)
        rig.animation_data_clear(); rig.location.x=(i-(len(people)-1)/2)*.85
        for b in rig.pose.bones: b.rotation_quaternion=(1,0,0,0); b.location=(0,0,0)
    scene.camera.location=(2.5,-8,3.2)
    scene.camera.rotation_euler=(Vector((0,0,.98))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera.data.ortho_scale=max(4.3,len(people)*.9)
    scene.render.resolution_x=1600; scene.render.resolution_y=1000
    scene.render.filepath=str(out/'lineup.png'); bpy.ops.render.render(write_still=True)
    (out/'manifest.json').write_text(json.dumps(dict(generator='generate.py',seed=args.seed,units='metres',
        forward='+Z in glTF',height=1.83,clips={name:[round(seconds*30)/30,loop] for name,(seconds,loop) in CLIPS.items()},variants=records),indent=2))
    print('CLAY_PEOPLE_COMPLETE',out)

if __name__=='__main__': main()
