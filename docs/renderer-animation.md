# Portable animation

The SDL3/bgfx path imports glTF/GLB skeletal animation, node animation and morph
targets through the existing Njin `model_*`, `model_pose`, `draw_model_anim`
and ECS `model3d` APIs. It does not call raylib's model loader or create an
OpenGL context. The existing raylib animation path remains independent.

Animation channels retain their original key times and STEP, LINEAR or
CUBICSPLINE interpolation. Quaternions use spherical interpolation for LINEAR
and normalized Hermite interpolation for CUBICSPLINE. Cubic tangents are scaled
by the actual key interval. Non-looping playback holds the last key; looping
and negative playback times wrap by clip duration. Cross-fades blend local
translation, rotation, scale and morph weights before evaluating the hierarchy.
Animated ancestors that are not skin joints still affect their descendants.

Skinning applies the mesh-node inverse transform, each glTF inverse-bind matrix
and the current joint world transform. Four influences are normalized per
vertex. Multiple skins share a union of up to 128 joints. Morph position,
normal and tangent deltas apply before skinning. Normals use inverse-transpose
transforms and tangents account for handedness. Independent mesh nodes retain
their own morph defaults and animated weights; the public named morph override
adds to each matching target. `model_bone_pose` and the existing bone name/parent
queries remain available. Procedural `model_pose::bones` overrides are supported.

## Preparation and ownership

Deformation currently runs on the **CPU**, using full float matrices. Each
prepared mesh owns an immutable vertex/index snapshot. The bgfx device uploads
that snapshot once per frame and reuses its GPU buffers for all shadow views,
the forward pass and positive/negative instance orientation groups. A shared
animated pose can therefore be drawn through actual GPU instancing. Ownership
is retained through submission and buffers use bgfx's deferred destruction.
`renderer_resource_stats().deformed_mesh_uploads` counts these uploads.

This supports animated rendering without introducing a second GPU abstraction
or consuming a large, platform-dependent uniform bone palette. It also has a
clear performance cost: CPU work and streaming bandwidth grow with animated
vertex count. GPU skinning/morph evaluation and pose caching across separate
draw calls remain optimization work. No crowd-performance claim has been made.

Import validates key ordering, accessors, quaternion values, joint indices,
weights, affine transforms and allocation budgets. Invalid input rolls back
new resources. Singular deformed transforms fail explicitly. The public
skeleton is backed by engine-owned CPU metadata, with no native GPU handles.

## Verification and remaining limits

`njin_animation_test` runs through the real Njin loop and engine captures. It
checks non-joint ancestors, root joints without parents, endpoints, STEP,
quaternions, non-unit-duration cubic morphs, scale and cross-fades, manual bones,
nonidentity inverse-bind transforms, two-joint weighting, independent node
morphs, mirrored instancing, ECS animation, animated shadows, upload reuse,
rollback, resource cleanup and two engine contexts.

```powershell
cmake --build build --target njin_animation_test
ctest --test-dir build -R "^animation_" --output-on-failure
```

Desktop controls have run on Vulkan, bgfx OpenGL, D3D12 and Vulkan validation;
consult the final build logs for the exact revision and platform results.
Android suites are selected with `--suite animation` in the device controller.

Limits: four skin influences per vertex, 128 distinct joints, 4,096 nodes,
1,024 clips, and a ten-million-float decoded animation budget. Eight-influence
skins report an error. Animation-aware camera/occlusion bounds, motion vectors,
animated LOD selection and portable recolor/custom-shader draw variants remain
unported. Programmatic `model_create_skinned` has not been migrated. This does
not claim every legacy animation utility or the entire renderer is ported.
