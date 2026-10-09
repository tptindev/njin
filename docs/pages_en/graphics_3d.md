# 3D graphics {#graphics_3d}

njin draws 3D worlds: a perspective camera, primitives, smooth SDF shapes, glTF models with
skeletal animation, lighting with shadows, materials, effects, particles, instancing, physics and
collision, 3D entities, mouse picking and gizmos for debugging. Everything goes through `njin.h` (declared in `njin_3d.h`, `njin_physics3d.h`
and `njin_gizmo.h`), no raylib or physics library needed.

Read first: @ref drawing, @ref game_loop and @ref rendering. Run `njin_fps` (a first-person shooter),
`njin_sokoban` (a 2.5D box-pusher) and `njin_platformer3d` (a third-person platformer) to see
everything below in a real game.

@image html graphics_3d.png "Mesh shapes (cube, sphere), SDF shapes (rounded box, capsule, torus) and 8 instanced cubes under a shadow-casting sun, with 2D and 3D gizmos"

@include draw_3d.cpp

## One 3D draw

Every 3D draw call sits between begin_3d() and end_3d(), inside `phase_render`. njin::camera3d is a
perspective camera: `position`, `target`, `up` and the vertical field of view `fovy`. Y axis up,
right-handed like glTF: looking down `-z`, `+x` is to the right. The aspect ratio comes from
screen_size(), so a virtual screen size still works.

Draw calls are **recorded** and actually drawn at end_3d(), in the order they were called: the
engine first computes shadows from everything, then draws each shape. So the order of
material3d_set(), fx3d_set() and a draw call still matters as usual, while light3d_add() lights every
shape of that draw whether it was called before or after.

2D drawn before begin_3d() sits under the 3D; 2D drawn after end_3d() (still in `phase_render`) sits
over it. UI in `phase_post_render` is always on top. A frame can have several 3D draws (a small scene
inside a UI panel, for example).

## Primitives

| Function | What it is | When to use it |
|---|---|---|
| draw_cube3d(), draw_sphere3d(), draw_plane3d(), draw_cylinder3d(), draw_capsule3d() | Triangle mesh | Many, cheap: walls, floors, bullets |
| draw_shape3d() with njin::shape3d | SDF shape: sphere, rounded box, capsule, rounded cylinder, torus | Something that needs to look smooth up close: characters, items |
| draw_sdf_blend() with njin::sdf_part | Several rounded cones melted into one solid (smooth min) | A clay character built from head, body and limbs, seamless as it moves |
| draw_instanced3d() | Thousands of mesh shapes in one draw call | Forests, crowds, floor tiles |
| draw_model(), draw_model_anim() | glTF/OBJ model loaded with model_load() | Props, characters made in Blender |
| model_create() with njin::mesh3d_data | Model from a triangle mesh the game builds (positions, per-vertex colours, indices) | Terrain grown from a seed, shapes put together at run time |

To draw thousands of small things with draw_instanced3d(), use `mesh3d_sphere_low` and
`mesh3d_cylinder_low`: the same shapes as `mesh3d_sphere` and `mesh3d_cylinder` with far fewer
faces, since each copy is a few pixels on screen and every triangle is drawn once more for the shadows.

An SDF shape is computed per pixel (sphere tracing inside its bounding box), so its edge is always
round at any size and it can be rounded, but it costs more than a mesh shape. It still receives
lighting, casts and receives shadows, and uses njin::material3d and njin::fx3d like any other shape.

## Models and materials

model_load() loads `.glb`, `.gltf` or `.obj`, keeping each material's colour and texture from the
file. model_material_get() and model_material_set() read and change each part's material:

@code
njin::model_material m = njin::model_material_get(ctx, crate, 0);
m.albedo = njin::texture_load(ctx, "assets/crate_wood.png");  // colour image
m.normal = njin::texture_load(ctx, "assets/crate_wood_n.png"); // normal map
m.emission = njin::texture_load(ctx, "assets/crate_glow.png"); // self-lit image
m.shader = my_shader;                                          // this part's own shader
m.surface.specular = 0.1f;                                     // a matte surface
njin::model_material_set(ctx, crate, 0, m);                    // -1 for every part
@endcode

A normal map needs no tangent in the file: the shader builds one from screen derivatives.

A material marked `doubleSided` in the glTF (leaves, paper, single-layer cloth) is drawn on both
faces: `model_material::double_sided`, changeable with model_material_set().

### Culling out of view and levels of detail {#model_lod}

draw_model(), draw_model_anim() and njin::model3d leave out a model whose bounding box is
outside the camera's view (frustum culling); that model still casts its shadow into the
scene. njin::render_info_get() counts `models3d` (drawn) and `models3d_culled` (left out). A
big mesh such as terrain or roads is best split into several models by area (one
model_create() per area), so the areas out of view are left out.

model_lod_build() makes levels of detail for a model: simplified copies with fewer
triangles, drawn in its place when the model is small on screen. Level 1 is used when the
model is less tall than `screen` (a quarter by default) of the screen's height, each next
level at half the previous. A model with bones keeps its bones and animations at every
level.

@code
const njin::model_handle person = njin::model_load(ctx, "assets/person.glb");
njin::model_lod_build(ctx, person); // 3 levels, each about half the triangles of the one before
@endcode

Simplifying uses the [meshoptimizer](https://github.com/zeux/meshoptimizer) library (MIT). It
keeps the borders between colours and between UV pieces, so a flat mesh of alternating colours
(like a checkerboard) can hardly be simplified; model_lod_build() then returns 0.
draw_instanced3d() and ray3d_model() always use the original model.

## Model animation

A glTF with a skin (bones) carries its animations: model_load() loads them with the model.
model_anim_find() finds an animation by its action name in Blender; model_anim_count(),
model_anim_name() and model_anim_duration() list them. draw_model_anim() draws the model in a
njin::model_pose: animation `anim` at second `time`, optionally blended with `blend_anim` by weight
`blend` to move smoothly between two motions.

@code
// Idle blended into run by speed, moving over 0.2 seconds.
blend = njin::move_toward(blend, moving ? 1.0f : 0.0f, dt / 0.2f);
njin::draw_model_anim(ctx, robot, {.position = pos, .rotation = {0, yaw, 0}},
                      {.anim = idle, .time = t, .blend_anim = run, .blend_time = t, .blend = blend});
@endcode

| Point | Detail |
|---|---|
| Bone computation | On the GPU, at most 128 bones, 4 bones per vertex |
| Shadows | Follow the pose |
| Many copies | Each draw has its own pose: the same model, a different motion for each one |
| Uniforms | model_material_find() finds a material by name; draw_model_anim() with a njin::model_recolor recolours it for one draw |
| Motions that hold their last frame | Each clip's last frame is taken from the one before it (raylib returns the clip's first pose at exactly its end time) |
| Uses the rest pose | Parts drawn with a game shader, draw_instanced3d(), ray3d_model() |
| No armature | The root bone has no parent node in the glTF: the engine warns, the model has no animations |

With an entity, njin::model3d holds the pose and the engine advances its time every frame (@ref entities_3d).

### Characters assembled from parts {#model_skinned}

When each character is a combination of parts (head, body, arms, hair, hat) chosen by a set of
genes, exporting a file for every combination is not an option. model_create_skinned() makes a
skinned model from a mesh the game assembles (njin::skinned_mesh3d_data: positions, four bones and
four weights per vertex, indices), split into parts, one material each. The new model copies the
skeleton of a loaded skinned model and **shares** its animations: one clip library, loaded once for
every character.

@code
// The clip library loaded once; one model per combination of parts.
const njin::model_handle clips = njin::model_load(ctx, "assets/shared_animations.glb");
const njin::model_handle body = njin::model_create_skinned(
    ctx, {.positions = pos.data(), .vertex_count = n, .joints = joints.data(), .weights = weights.data(),
          .indices = idx.data(), .index_count = (njin::u32)idx.size(), .skeleton = clips});
// Each person their own pose and palette on the same mesh.
const njin::model_recolor palette[] = {{.material = 0, .color = skin}, {.material = 1, .color = shirt}};
njin::draw_model_anim(ctx, body, at, {.anim = njin::model_anim_find(ctx, body, "Walk_Loop"), .time = t},
                      njin::colors::white, palette, 2);
@endcode

| Task | Function |
|---|---|
| Bone indices by name, to bind each part to the right bones | model_bone_find(), model_bone_name(), model_bone_count() |
| One bone in a pose (props in a hand, hit zones on bones) | model_bone_pose(): position and three axes in model space, as draw_model_anim() places it |
| Several recoloured materials in one draw | draw_model_anim() with an array of njin::model_recolor |
| The clip library freed first | The assembled model keeps only its rest pose |

### Morph targets {#model_morph}

Morph targets (blend shapes, shape keys in Blender) are other shapes of the same mesh: a smile, closed
eyes, a bulging muscle. model_load() loads them with the model, by the names in the glTF's
`extras.targetNames` (Blender writes the shape key names there). Targets of the same name in several
meshes, such as a blink on the face and on the lashes, are **one** morph.

The weights of each draw are worked out in three layers:

1. The default weights in the file (`mesh.weights`).
2. The njin::model_pose's animation (`anim`, blended with `blend_anim`) sets the morphs its clip has
   curves for (the glTF `weights` channel). A model without bones that has such clips gets them as its
   animations (model_anim_count()).
3. `model_pose::morph_weights` is added on top, an array indexed by model_morph_find(): the game blinks
   or talks while the clip moves the rest.

@code
const njin::i32 blink = njin::model_morph_find(ctx, hero, "Blink");
njin::f32 morphs[16] = {};
morphs[blink] = blinking ? 1.0f : 0.0f;
njin::draw_model_anim(ctx, hero, at, {.anim = talk, .time = t, .morph_weights = morphs, .morph_count = 16});
@endcode

| Point | Detail |
|---|---|
| Where it is blended | On the CPU, into the mesh's vertex buffers, right before each draw whose weights differ from the last, then the bones bend it on the GPU. So it works with every shader, the game's own included, and shadows follow the shape |
| Why not on the GPU | Only three vertex attribute slots are left (a face has dozens of morphs), and every built-in shader and the game's own would need changing |
| Cost | Each mesh with morphs copies its vertices to the GPU once per pass (shadow passes included) when the weights change; drawing again with the same weights costs nothing |
| Reading the weights | model_morph_weights() returns exactly the weights a draw will use |
| Drawing many copies | draw_instanced3d() has a version taking a njin::model_pose: **every** instance of one call has the same shape. The version without it draws the file's default shape |
| Several shapes in one crowd | Split the instances into a few groups, one call each with its own `pose` (per-instance weights would need a second instance buffer and an offset texture, not done) |
| Rays | ray3d_model() has a version taking a njin::model_pose, testing the morphed shape (the old one tests the file's shape) |
| Level of detail | model_lod_build() keeps the morphs at every level: each target's offsets go through the same simplification, and the simplifier weighs the offsets, so a flat surface a morph bends keeps enough edges |
| Merged meshes | `model_load_desc::merge` keeps the morphs of the meshes it merges |
| Tangents | A target with `TANGENT` blends the tangents too, so normal maps follow the shape |

@code{.cpp}
// A whole crowd smiling: one draw call for every instance.
njin::f32 smile[16] = {};
smile[njin::model_morph_find(ctx, face, "Smile")] = 1.0f;
const njin::model_pose happy{.morph_weights = smile, .morph_count = 16};
njin::draw_instanced3d(ctx, face, crowd, 0, 500, happy);
// Hit the swollen cheek, not the one in the file.
const njin::ray3d_hit hit = njin::ray3d_model(ctx, shot, face, at, happy);
@endcode

### Spring bones {#spring3d}

Hair, tails, whiskers, the flaps of a cape: bones the animation does not move, but which must lag behind
with inertia as the character runs, swing, then come back. spring3d_create() takes the chains
(njin::spring3d_chain: a bone and every bone below it) and the colliders (njin::spring3d_collider: a
sphere or capsule on a bone, such as the head and body) the springs do not pass through. The parameters
are those of VRM spring bones.

Each frame, spring3d_update() takes the animation's pose (or the bones the game set), runs the springs in
the world following the draw's transform, and writes the final pose into an array to draw with
`model_pose::bones`:

@code
njin::bone_pose3d bones[128];
const njin::model_pose run{.anim = run_clip, .time = t};
njin::spring3d_update(ctx, hair, run, at, njin::delta(ctx), bones, 128);
njin::draw_model_anim(ctx, hero, at, {.anim = run_clip, .time = t, .bones = bones});
@endcode

| Field of njin::spring3d_chain | Meaning |
|---|---|
| `stiffness` | Pull back to the animation's pose; larger is stiffer |
| `drag` | 0..1, damping: 0 swings forever, 1 only lags then returns |
| `gravity`, `gravity_dir` | Gravity pulling the bone tips, in the world |
| `radius` | Collision radius of each bone's tip |

Each character needs its own springs, since they remember where the bone tips are. The springs run in
steps of 1/60 s; a `dt` of 0 (game paused) holds them. When teleporting a character, call
spring3d_reset() so the hair does not fly along the whole way.

### Playing animation on another skeleton {#retarget3d}

One set of clips (from Mixamo, say) for many characters whose skeletons have other names and
proportions: retarget3d_create() matches the target model's bones with the source model's, and
retarget3d_pose() gives the target model's pose to draw with `model_pose::bones`.

- **Matching bones:** the name pairs the game gives (`retarget3d_desc::pairs`) first, then by the standard
  names of bone_humanoid_name(), which understands the naming of Mixamo (`mixamorig:LeftForeArm`), Unreal
  (`lowerarm_l`, `spine_02`), Unity and VRM (`LeftLowerArm`) and Blender (`forearm.L`), then by the same
  name. retarget3d_source_bone() tells which bone a bone was matched with.
- **Rotations:** a target bone turns by exactly the angle its source bone turned away from its own rest
  pose. The two models need the same axes and similar rest poses (both a T or both an A). Bones left
  unmatched (a ponytail, a weapon) follow their parent as at rest.
- **Hips:** moved with the source's hips, times the ratio of the two models' hip heights, so a
  short-legged character takes shorter steps and the feet do not slide.

@code
const njin::retarget3d_handle to_dwarf = njin::retarget3d_create(ctx, {.source = hero, .target = dwarf});
njin::bone_pose3d bones[128];
njin::retarget3d_pose(ctx, to_dwarf, {.anim = run_clip, .time = t}, bones, 128);
njin::draw_model_anim(ctx, dwarf, at, {.bones = bones});
@endcode

A complete example: springy hair, a blink by morph, and a dwarf running with the hero's clip.

@include anim3d.cpp

### Feet on the ground {#foot3d}

Animations are made on a flat floor: standing on stairs or a slope, one foot floats and the other sinks into the
ground. foot3d_create() and foot3d_update() set each foot on the ground right under it:

1. Probe straight down under each ankle (physics3d_raycast(), or a njin::foot3d_ground function of the game for
   terrain or a tile grid) to find how high the ground is against the draw's origin.
2. Lower the whole body so the lower foot reaches (standing on a step's edge, one leg hanging down).
3. Two-bone IK (thigh, shin) for each leg: the ankle goes exactly where it should, the knee bending the way the
   animation bends it, or towards `knee_forward` when the leg is straight.
4. The sole tilts to the ground (no more than `max_tilt`, so a step's edge does not tip the foot over).

A foot the animation lifts (in a step) stays that much above the ground under it, so walking up stairs is still
stepping. Heights are smoothed by `smoothing` (up twice as fast as down, so a foot does not sink into a step it just
climbed); foot3d_reset() after a teleport. Bring `weight` down to 0 when the character leaves the ground (jumping,
falling).

Without `legs`, foot3d_create() finds the two human legs by bone_humanoid_name(). The draw's origin is where the
character stands: a character3d's feet (character3d_position()). The probe rays do not hit character3d, only bodies
and terrain.

The order when used together: retarget3d_pose(), then foot3d_update() with `pose.bones` set to that result, then
spring3d_update() with `pose.bones` set to foot3d's result (hair swings with the lowered body), then draw.

@include foot3d.cpp

Measured by a harness with a 65-bone person and an idle animation: on a 0.2 m step, on a step's edge and on 20 degree
slopes (rising ahead, across, and at 45 degrees), the ankles are as high above the ground as on a flat floor, 6 mm off
at most, with no toe in the ground; on a flat floor the pose is exactly the animation.

## Lighting

| Part | Set with | Notes |
|---|---|---|
| Sun, ambient light | light3d_set() with njin::light3d | Takes effect from the next begin_3d() |
| The sun's shadow | `light3d::shadows`, `shadow_range`, `shadow_size`, `shadow_softness` | A shadow box around where the camera looks; 3x3 soft edge |
| Fog | `light3d::fog_color`, `fog_density` | By distance to the camera |
| Point, spot lights | light3d_add() with njin::light3d_source per draw, or an entity (@ref entities_3d) | Up to njin::light3d_max (16) |
| Point and spot light shadows | `light3d_source::shadows`, `light3d::source_shadow_size` | Up to njin::light3d_shadow_max (4) per draw; later lights light without casting shadows |
| Surface | material3d_set() with njin::material3d | Shininess, emission, rim light, `unlit`, `texture`, `cast_shadows` |

Lighting is Lambert plus a Blinn-Phong specular highlight, plus ambient light. `emission` adds colour
after lighting: turn on bloom (post_fx_set()) so a self-lit shape glows into its surroundings.

Point and spot light shadows are costly: every shadow caster is drawn 6 more times for a point light
(the 6 faces of a cube around the light) and once for a spotlight, each face with a shadow map of
`source_shadow_size` pixels. Turn them on only for a few important lights (a flashlight, a lamp hanging
in the middle of a room); the light's `radius` is also the shadow's range.

@code
// The player's flashlight: a shadow-casting spotlight.
njin::light3d_add(ctx, {.kind = njin::light3d_spot, .position = eye, .direction = forward,
                        .intensity = 2.0f, .radius = 25.0f, .cone = 40.0f, .shadows = true});
@endcode

## Effects

The shared effects of @ref particles all work for 3D too:

| Effect | With 3D |
|---|---|
| camera_shake() | Shakes begin_3d()'s camera: the offset becomes a turn of the view, the tilt a roll of the camera |
| hitstop(), screen_flash(), post_fx_set(), scene transitions | Same as 2D, nothing extra needed |
| Colour flash, dissolve | fx3d_set() with njin::fx3d: the equivalent of njin::flash_fx and njin::dissolve_fx for sprites |
| Particles | particles3d_spawn() reuses a 2D emitter (njin::fx::explosion(), sparks(), dust()...) |

A shape drawn with a call (draw_sphere3d(), draw_shape3d(), draw_model()...) is not an entity: the
game keeps the effect's own time and sets its level every frame. An entity with njin::shape3d_render or
njin::model3d sets that component's `fx` field instead of calling fx3d_set() (@ref entities_3d). With a
draw call:

@code
// A target that was just hit: flashes white, then dissolves over 0.4s.
const float t = age / 0.4f;
njin::fx3d_set(ctx, {.flash = {1, 1, 1, 1 - t * 3}, .dissolve = t});
njin::draw_sphere3d(ctx, pos, 0.6f, njin::colors::red);
njin::fx3d_set(ctx, {});
@endcode

3D particles face the camera, stop during hitstop, and `particles3d_desc::scale` converts an emitter
preset's pixel units to 3D world units.

Effects worked out on the 3D image from its depth (wall corners that darken, polished floors that reflect,
bullet holes stuck to surfaces, motion blur, light shafts, lens flare) have a page of their own: @ref post_3d.

## Instancing

draw_instanced3d() draws `count` copies of a built-in shape or a model with one draw call, each
instance's data sitting in an instance buffer as with 2D's draw_instanced(). With no shader, the
built-in one reads:

| Attribute | Content |
|---|---|
| `instance0` | Position `xyz`, uniform scale `w` (0 is 1) |
| `instance1` (from 8 floats) | Colour `rgba` |
| `instance2` (from 12 floats) | Rotation `xyz`, degrees |
| `instance3` (16 floats) | Scale along x, y, z (0 is 1) |

For tens of thousands of instances of which the camera sees only part (a forest, a city), put them in cells and draw
only the cells the camera sees: @ref spatial_batch.

## A game's own shader

shader_begin() before a 3D draw call makes that shape draw with the game's shader. The engine sets the
`vec3` uniforms `lightDir`, `lightColor`, `ambient`, `viewPos` when the shader declares them, plus
`mvp`, `matModel`, `matNormal`, `colDiffuse` under raylib's standard names. An SDF shape always uses
the engine's own shader.

## Physics and collision {#physics3d}

`njin_physics3d.h` is built on [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (MIT licence, pulled
in by the engine's build, never seen by the game). A body's shape follows the same convention as
njin::shape3d, so an object and the shape that draws it share their numbers.

| Part | Created with | Used for |
|---|---|---|
| Static body | body3d_create() with `body3d_static` | Floors, walls, fixed platforms |
| Kinematic body | `body3d_kinematic`, then body3d_move_kinematic() every step | Moving platforms, lifts, doors: carry and push others |
| Dynamic body | `body3d_dynamic` | Crates, balls, debris: fall, collide, roll, get pushed; body3d_add_impulse() for a blast |
| Character | character3d_create() | The player, enemies: a capsule that walks on floors, steps up, slides along walls, pushes dynamic bodies |
| Body from a model | `body3d_desc::model` and `scale` | Floors, slopes, caves made in Blender (static, kinematic); convex props (dynamic) |
| Sensor | `body3d_desc::sensor` | Pickup zones, checkpoints, traps, goals: no collision, only contact reports |
| Contact events | physics3d_contact_count(), physics3d_contact() | Knowing what started or stopped touching what |
| Joint | joint3d_create() | Hinged doors, seesaws, chains, pistons |
| Ragdoll | ragdoll3d_create() | A character falling or hit: the model's skeleton follows physics |
| Soft body | softbody3d_create() | Rubber ball, mattress, jelly block: deforms on impact, held round by pressure; soft bodies collide with each other, characters can stand on them |
| Cloth | cloth3d_create() | Flag, curtain, cape: pinned in place, blown by the wind |
| Wheeled vehicle | vehicle3d_create() | Car: engine, automatic gearbox, steering, brakes, suspension |
| Motorcycle | motorcycle3d_create() | Two-wheeler: keeps its balance, leans into turns |
| Tracked vehicle | tracked3d_create() | Tank, excavator: steers with its two tracks, turns on the spot |
| Ray | physics3d_raycast() | Bullets, line of sight, a camera that does not go through walls; returns the body hit, passes through sensors; the version with njin::soft3d_hit hits soft bodies too |

The engine simulates in `phase_fixed_update`, **right after** the game's systems in that phase: the game
sets a velocity or a target position, then physics runs in the same step. A character is driven by
velocity, and **the game adds gravity and jumps itself** (to tune the jump's feel: coyote time, a
double jump...):

@code
// Every fixed step:
const bool on_ground = njin::character3d_grounded(ctx, player);
vertical = on_ground ? 0.0f : vertical - gravity * dt;
if (jump_pressed && on_ground)
  vertical = jump_speed;
// Standing on a moving platform adds its velocity.
const njin::vec3 ground = njin::character3d_ground_velocity(ctx, player);
njin::character3d_set_velocity(ctx, player, walk + ground + njin::vec3{0, vertical, 0});
@endcode

Characters block each other: they do not walk through one another but slide round, as along a
wall. The engine tests each character only against the ones near it (a grid), so even a hundred
characters stay cheap; what costs is that each character switched on needs a collision pass every
step (some tens of microseconds). A big crowd should switch on only the people the camera sees,
with character3d_set_active(), and the game moves the far ones along their paths itself:

@code
bool seen = false;
njin::camera3d_to_screen(ctx, cam, feet, &seen); // or however the game knows
if (seen != njin::character3d_active(ctx, walker)) {
  if (seen)
    njin::character3d_set_position(ctx, walker, feet); // back on where the game moved it
  njin::character3d_set_active(ctx, walker, seen);
}
@endcode

Draw a dynamic body exactly where and how physics placed it with body3d_transform():

@code
const njin::transform3d t = njin::body3d_transform(ctx, crate);
njin::draw_shape3d(ctx, {.kind = njin::shape3d_box, .position = t.position, .rotation = t.rotation,
                         .size = {1, 1, 1}}, {0.7f, 0.5f, 0.3f, 1.0f});
@endcode

An entity with the njin::body3d component does not need this step: the engine writes its transform
(@ref entities_3d).

`body3d_desc::user` attaches a number of the game's to a body (an array index, an entity id): read it back
with body3d_user() from the body that physics3d_raycast(), character3d_ground_body() or
physics3d_contact() returns.

### Bodies from a model

With `body3d_desc::model`, the body's shape comes from the model's triangle mesh, placed the way
draw_model() draws it with the same `position`, `rotation` and `scale`. So one model is both what is
drawn and the ground walked on:

@code
const njin::model_handle hill = njin::model_load(ctx, "assets/hill.glb");
njin::body3d_create(ctx, {.position = hill_pos, .model = hill}); // static: the exact triangles
@endcode

| Body kind | Shape from the model |
|---|---|
| Static, kinematic | The exact triangles: slopes, steps and caves can all be walked |
| Dynamic | The convex hull of the vertices (the smallest convex shape around the model): hollows are filled |

### Sensors and contact events

A body with `sensor = true` does not collide: bodies and characters pass through it, so does
physics3d_raycast(), and the engine reports contact events. After each simulation step,
physics3d_contact_count() and physics3d_contact() give njin::contact3d events: two bodies (`a`, `b`),
or a body `a` and a character `character` (then `b` is invalid); `began` tells whether they started or
stopped touching, `sensor` whether one side is a sensor, plus the contact point and normal.

A pair is reported once when it starts touching and once when it stops, even if it touches at several
points. A destroyed body gets no stopped-touching event. Read the events in `phase_fixed_update`: each
step the game sees exactly the events of the previous step, none missed, none repeated.

@code
// In a phase_fixed_update system: the player touching a coin (a sensor) picks it up.
entt::registry &reg = njin::world(ctx);
for (njin::i32 i = 0; i < njin::physics3d_contact_count(ctx); i++) {
  const njin::contact3d c = njin::physics3d_contact(ctx, i);
  if (!c.began || !c.sensor || c.character.id != player.id)
    continue;
  const auto e = (entt::entity)njin::body3d_user(ctx, c.a); // user = the coin's entity id
  if (reg.valid(e) && reg.all_of<coin>(e))
    reg.destroy(e); // the body3d component destroys its body too
}
@endcode

### Joints

joint3d_create() connects two bodies, or a body and a fixed point of the world (`b` invalid). The
attachment point `anchor` and the axis `axis` are in world coordinates, at the time the joint is
created. Destroying a body also destroys its joints.

| Kind | What it does | Example |
|---|---|---|
| `joint3d_fixed` | Welds: keeps the relative position and angle | Fixing two pieces into one object |
| `joint3d_point` | Ball joint: rotates freely around `anchor` | Chains, pendulums |
| `joint3d_hinge` | Hinge: rotates around `axis` through `anchor` | Doors, seesaws, wheels |
| `joint3d_slider` | Slides along `axis`, no rotation | Pistons, drawers, sliding doors |
| `joint3d_distance` | Keeps the distance between `anchor` and `anchor_b` in `[min, max]` | Ropes, rods |

`min` and `max` are the limits: angle, degrees (hinge); travel from the position at creation
(slider); distance (njin::joint3d_distance). For hinges and sliders, `min >= max` means no limit.
A `motor_force` above 0 gives a hinge or slider a motor: joint3d_set_motor() sets its speed, and
joint3d_position() reads the current angle or travel.

@code
// A seesaw: a dynamic plank on a hinge, tipping at most 18 degrees each way.
const njin::body3d_handle plank = njin::body3d_create(
    ctx, {.position = pivot, .size = {4, 0.2f, 1}, .motion = njin::body3d_dynamic, .mass = 25});
njin::joint3d_create(ctx, {.kind = njin::joint3d_hinge, .a = plank, .anchor = pivot, .axis = {0, 0, 1},
                           .min = -18, .max = 18});
@endcode

### Ragdoll {#ragdoll3d}

ragdoll3d_create() turns a model with bones into a ragdoll: each chosen bone becomes a dynamic capsule,
joined to its parent part by a joint with angle limits (Jolt's Ragdoll), falling and colliding like any
body. Parts of the same ragdoll do not collide with each other. Bones that are not chosen (fingers, the root) follow the
nearest part above them. The angle limits count from the file's rest pose (T or A pose), so a ragdoll can
start from any animation frame: usually the pose just drawn, when the character is hit or falls.

Each frame, ragdoll3d_bones() reads the ragdoll's pose into an array, and `model_pose::bones` draws the
model from that array instead of an animation. model_bone_pose() with that pose also returns the
ragdoll's bones (so the camera can follow the head, for example). ragdoll3d_body() returns the body of
one part, to push it with body3d_add_impulse() or to tell which part physics3d_raycast() hit.

| Field of njin::ragdoll3d_bone | Meaning |
|---|---|
| `name`, `radius`, `length` | The bone and the capsule along its y axis; `radius`, `length` 0 measure it from the model's skin |
| `swing`, `twist` | Ball joint: the largest swing and twist angles, degrees |
| `bend_min`, `bend_max` | A hinge around the bone's x axis instead of a ball joint (knees, elbows) |

@code
// The player falls: from the pose being drawn, keeping the running velocity.
rag = njin::ragdoll3d_create(ctx, {.model = man, .transform = at, .pose = pose, .bones = parts,
                                   .bone_count = std::size(parts), .velocity = velocity});
// Each frame, in phase_render:
static njin::bone_pose3d bones[128];
njin::ragdoll3d_bones(ctx, rag, at, bones, 128);
njin::draw_model_anim(ctx, man, at, {.bones = bones});
@endcode

A `parts` list for the Quaternius mannequin is in ragdoll3d_create().

### Soft bodies and cloth {#softbody3d}

A soft body is a cloud of points (vertices) joined by springs that deforms each physics step under gravity,
collisions and the pressure inside it (Jolt's soft body). softbody3d_create() builds one from a ready-made
shape or from the game's mesh; cloth3d_create() builds a rectangular sheet of cloth. Both return a
njin::softbody3d_handle and share the `softbody3d_*` functions.

| Shape (njin::softbody3d_kind) | Built as | Example |
|---|---|---|
| `softbody3d_box` | A solid lattice of points inside the box `size`, keeping its volume | Mattress, jelly block, rubber block |
| `softbody3d_sphere` | A hollow sphere of radius `radius`; set `pressure` to inflate it | Ball, bubble |
| `softbody3d_mesh` | The surface of `mesh` or of every mesh of `model` (vertices at the same place are merged) | Pillow, inflatable toy |

`stiffness` (0..1) is how stiff the edges are: 1 does not stretch. `bend` is how stiff it is to fold.
`pressure` is the pressure inside while the body has its starting shape, in Pa: squeeze it and the pressure
rises, like a balloon. A 1 kg ball of radius 0.5 needs about 50 (soft, sinks in when it lands) to 300 (taut);
more than that and the body swells beyond its starting size.

Soft bodies collide with every body, and with the other soft bodies that have `collide_soft` on too (it is on
by default): two cloths lying on each other, a cloth dropped on a hammock, a ball dropped on a mattress. Every
vertex keeps the thickness of both bodies (a cloth's `thickness`) off the other's faces. Jolt has no collisions
between two soft bodies, so the engine works them out itself before each physics step: a vertex about to go into
the other body's face is held back by its velocity, and its momentum goes into the face it meets (a hammock sags
under the cloth on it). Two bodies far apart cost one bounding-box test; turn `collide_soft` off for soft bodies
that never meet another.

Characters **stand** on the upper side of a soft body that has `walkable` on (it is on by default): a mattress
sinks in under the character's weight (njin::character3d_desc::mass), a trampoline sags. On an upright face (a
curtain) the character still walks through, pushing the vertices aside, so a curtain does not block the way.
character3d_ground_soft() says which soft body the character stands on (character3d_ground_body() is invalid
then), and character3d_ground_velocity() is the velocity of the soft surface under its feet: add it to a jump to
bounce higher while the trampoline throws up.

physics3d_raycast() and the `physics3d_*_push`, `_cast` functions pass through soft bodies. The versions of
physics3d_raycast(), physics3d_box_cast() and physics3d_hull_cast() that take a njin::soft3d_hit hit soft bodies
too: a shot at a curtain hits the curtain, and tells which soft body, which triangle and the vertex nearest the
hit.

@code
// A shot at a curtain: the spot hit is pushed along the shot.
njin::soft3d_hit soft;
const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f, nullptr, &soft);
if (hit.hit && soft.soft.id != 0)
  njin::softbody3d_add_impulse(ctx, soft.soft, shot.direction * 0.5f);
@endcode

Draw a soft body with softbody3d_model(): a model that always has the body's current shape, in world space,
updated by the engine after each physics step. Draw it with draw_model() and the default transform, change its
colour and textures with model_material_set(); the model belongs to the soft body and softbody3d_destroy()
destroys it. A body with many vertices (over 65535, cloth counting twice for its back faces) is split into
several meshes of the same model. To draw it yourself, read softbody3d_vertices(), softbody3d_normals() and
softbody3d_indices(); softbody3d_draw_debug() draws the edges and the pinned vertices with gizmos.

The cloth vertex at row `r`, column `c` has index `r * (columns + 1) + c`; row 0 is the top edge. Cloth lies
in its own x–y plane (upright like a flag); `rotation.x = 90` lays it flat. Pin edges with `pin_edges`, other
vertices with `pinned` or softbody3d_pin(); softbody3d_nearest() finds the vertex nearest a point.

| Function | What it does |
|---|---|
| softbody3d_pin() | Pins or unpins a vertex: a pinned vertex stays put |
| softbody3d_move_pinned() | Moves a pinned vertex to a new place after the next step, pulling the rest along (a cape pinned to the shoulders) |
| softbody3d_set_wind() | Wind blowing through: each face across it is pushed by `drag` and its area |
| softbody3d_add_impulse() | Pushes the whole body at once, shared among its vertices: kicking a ball |
| softbody3d_position() | The body's centre, for a camera to follow |

@code
// A cape: cloth pinned along its top edge, its two top corners pulled to the shoulders every fixed step.
cape = njin::cloth3d_create(ctx, {.position = back, .size = {0.6f, 1.0f}, .columns = 8, .rows = 12,
                                  .pin_edges = njin::cloth3d_top});
// Every fixed step:
njin::softbody3d_move_pinned(ctx, cape, 0, shoulder_left);
njin::softbody3d_move_pinned(ctx, cape, 8, shoulder_right);
@endcode

### Wheeled vehicles {#vehicle3d}

vehicle3d_create() makes a car on Jolt's vehicle controller: the chassis is a dynamic body shaped as the box
`size` (or the convex hull of `model`), each wheel is a suspension that probes the ground, with an engine, an
automatic gearbox, differentials, brakes and a handbrake. The car's nose points +z: a car with `rotation` 0
drives along the z axis. By default it has four wheels at the bottom corners of the chassis: the two front
wheels steer, all four are driven, the two rear ones have the handbrake; `wheels` gives the game's own wheels
(a three-wheeler, a six-wheeled truck).

Drive it with vehicle3d_set_input() every fixed step: throttle (-1 reverse .. 1 forward), steering (-1 left
.. 1 right), brake and handbrake. A negative throttle while rolling forward brakes first, and reverses only
once the car has stopped, as a driver would. vehicle3d_body() is the chassis (body3d_transform() to draw it,
body3d_velocity() for the speedometer), vehicle3d_wheel_transform() is where each wheel is and how it is turned
right now: its y axis is the axle, so a wheel draws as a cylinder. vehicle3d_rpm() and vehicle3d_gear() are for
the engine sound and the gauges.

An `engine_torque` beyond what the tyres can grip spins the wheels, and the gearbox does not shift up while
they spin.

The scene below has a flag blowing in the wind, a ball, and a car driven with the arrow keys:

@include physics3d_soft.cpp

### Motorcycles and tracked vehicles {#vehicle3d_kinds}

motorcycle3d_create() makes a motorcycle on Jolt's motorcycle controller: one front wheel steering on a raked
fork, one driven rear wheel, and a spring that holds the bike upright and leans it into turns from the speed and
the steering. Standing still it does not fall; at speed the engine lowers the steering angle so the bike does not
slide out (the turn gets wider); `max_lean` is the largest lean into a turn. The defaults are a 240 kg bike with
its rider, which rides well untouched.

tracked3d_create() makes a tracked vehicle (a tank, an excavator): on each side a track runs over
`wheels_per_side` wheels, and it turns by running the two tracks at different speeds. vehicle3d_set_input()
drives it like a car; when it is about stopped and only steers with no throttle, the two tracks run opposite ways
and it turns on the spot. vehicle3d_set_tracks() drives each track directly (-1 reverse .. 1 forward),
vehicle3d_track_speed() gives a track's speed to scroll its texture.

Both return a njin::vehicle3d_handle, so every `vehicle3d_*` function works: vehicle3d_body() for the chassis,
vehicle3d_wheel_transform() for each wheel, vehicle3d_rpm(), vehicle3d_gear(). vehicle3d_draw_debug() draws the
chassis, the wheels, the suspension and where the wheels touch the ground with gizmos, for every kind of vehicle.

@include physics3d_vehicles.cpp

## 3D entities {#entities_3d}

Instead of issuing draw calls and reading bodies every frame, an entity (@ref ecs) can carry 3D
components. The engine draws and updates them from the entity's njin::transform3d, and the inspector
shows all of these components.

| Component | What the engine does |
|---|---|
| njin::transform3d | The entity's position, rotation and scale; the components below read or write it |
| njin::model3d | Draws `model` at the transform; advances `pose.time` and `pose.blend_time` every frame (by delta(), times `speed`) |
| njin::shape3d_render | Draws the SDF shape `shape` at the transform (`shape.position` and `shape.rotation` are ignored) |
| njin::light3d_source | A light placed at `transform3d::position` (`position` is ignored), lighting every 3D draw |
| njin::body3d | Dynamic: after each step, writes the body's position and rotation to the transform. Kinematic: before each step, moves the body to the transform. Static: nothing |
| njin::character3d | After each step, writes the character's feet position to `transform3d::position` (the rotation is set by the game) |

Removing njin::body3d or njin::character3d, or destroying the entity, destroys the body or character
too. njin::model3d and njin::shape3d_render have `fx` (flash and dissolve, as for fx3d_set()) and
`visible` to hide without removing the component. Entities are drawn at end_3d() of every draw with
`camera3d::entities` (on by default), along with the game's draw calls; turn it off for a secondary
scene, like a model spinning in a menu.

@code
// A crate: physics sets the transform, shape3d_render draws it right there.
entt::registry &reg = njin::world(ctx);
const auto crate = reg.create();
reg.emplace<njin::transform3d>(crate);
reg.emplace<njin::shape3d_render>(crate, njin::shape3d_render{
    .shape = {.kind = njin::shape3d_box, .size = {0.9f, 0.9f, 0.9f}, .rounding = 0.05f},
    .color = {0.7f, 0.5f, 0.3f, 1.0f}});
reg.emplace<njin::body3d>(crate, njin::body3d_create(ctx, {.position = {0, 3, 0}, .size = {0.9f, 0.9f, 0.9f},
                                                           .motion = njin::body3d_dynamic, .mass = 8}));

// The player: a physics character and an animated robot.
const auto hero = reg.create();
reg.emplace<njin::transform3d>(hero);
reg.emplace<njin::character3d>(hero, njin::character3d_create(ctx, {.position = start}));
reg.emplace<njin::model3d>(hero, njin::model3d{.model = robot, .pose = {.anim = idle, .blend_anim = run}});
// Every frame the game only changes the blend weight: reg.get<njin::model3d>(hero).pose.blend = speed / max_speed;
@endcode

## Mouse picking

| Function | What it does |
|---|---|
| camera3d_ray() | A ray from the camera through a screen point, e.g. mouse_pos() |
| camera3d_to_screen() | A 3D point projected to the screen, to place a label or a health bar |
| ray3d_box(), ray3d_sphere(), ray3d_plane() | A ray against a box, sphere, plane |
| ray3d_shape() | A ray against an SDF shape, exactly as draw_shape3d() draws it |
| ray3d_model() | A ray against each triangle of a model |
| physics3d_raycast() | A ray against every physics body, returns the nearest body hit |

@code
const njin::ray3d ray = njin::camera3d_ray(ctx, cam, njin::mouse_pos(ctx));
const njin::ray3d_hit hit = njin::ray3d_shape(ray, player_shape);
if (hit.hit && njin::mouse_pressed(ctx, njin::mouse_left))
  select_player();
@endcode

In 2D, use scr2w() on the mouse position, then collision_overlap_point() or collision_raycast()
(@ref collision).

## Gizmos and the inspector

The `gizmo_*` functions (njin_gizmo.h) draw debug shapes: lines, arrows, boxes, spheres, axes, points,
text labels, in both 2D and 3D. Callable in any phase, drawn on top of everything, and kept for
`duration` seconds if a trail is wanted. gizmos_set_visible() turns them all on or off. See also
@ref debug.

While njin_inspector is connected, its World panel automatically switches to 3D while the game draws
in 3D: every draw call (each instance included) is a coloured point, along with the lights, the sun's
direction, the game camera's frustum and gizmos. Drag with the right mouse button to orbit, the
middle button to pan, scroll to zoom.

@image html inspector_world_3d.png "njin_inspector's World panel while njin_sokoban runs: the floor, walls, crates and character are points, the blue frame is the game's camera, the yellow line is the sun's direction"

## Sample games

| Game | What to look at |
|---|---|
| `njin_fps` | A first-person camera, a glTF model (the gun), shadows, fog, two coloured lamps and a flashlight (a spotlight, F key) that all cast shadows, a muzzle light, glowing tracers with bloom, sparks and an explosion from 3D particles, a hit target that flashes then dissolves, camera shake, hitstop, `ray3d_box`, gizmos (G key) |
| `njin_sokoban` | A 2.5D camera, the floor and walls drawn with draw_instanced3d(), a textured crate, an SDF capsule character with a rim light, a point light over each goal, dust and sparkle particles, fading in on level start, gizmos (G key) |
| `njin_platformer3d` | A third-person camera that does not clip through walls (physics3d_raycast), the player as an entity (njin::character3d and njin::model3d: a glTF robot with idle, run and jump animations; idle and run blended by speed) with coyote time and a double jump, static platforms, a moving platform that carries you (kinematic body), pushable crates and a seesaw plank as entities (njin::body3d, njin::shape3d_render), the seesaw a hinge limited to ±18°, a grassy hill that is a triangle-mesh body from `assets/hill.glb`, six coins that are sensors (touching one is a contact event, the coin entity is destroyed), a sensor goal, checkpoints, dissolving out and back in after a fall, a glowing goal ring, gizmos (G key) |
