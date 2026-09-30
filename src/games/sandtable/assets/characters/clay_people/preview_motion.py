"""Render a small walk-loop preview from the delivered rig."""
import bpy
import math
from pathlib import Path
root=Path(__file__).parent/'generated'
bpy.ops.wm.open_mainfile(filepath=str(root/'reference.blend'))
scene=bpy.context.scene
rig=bpy.data.objects['reference_rig']
rig.animation_data.action=bpy.data.actions['Walk_Loop']
rig.animation_data.action_slot=rig.animation_data.action.slots[0]
scene.render.resolution_x=480; scene.render.resolution_y=480
scene.cycles.samples=8; scene.cycles.use_denoising=True
folder=root/'walk_frames'; folder.mkdir(exist_ok=True)
for i in range(20):
    frame=1+i*1.5
    scene.frame_set(math.floor(frame),subframe=frame%1)
    scene.render.filepath=str(folder/('%03d.png'%i))
    bpy.ops.render.render(write_still=True)
print('MOTION_PREVIEW_COMPLETE')
