# Changelog

Versions follow [semver](https://semver.org). Before 1.0 the public API may
change between MINOR versions. The number lives in `src/engine/api/njin_version.h`.

To release: edit that header, add a section here, commit, then
`git tag -a vX.Y.Z -m "njin X.Y.Z"` and push the tag.

## Unreleased

### Fixed

- A character standing on a light dynamic body (a board lying on the floor)
  no longer makes it shake, and its whole weight now counts. It was put on
  the body as an impulse straight into the body's velocity outside the
  solver. Now, for each step's solve, the body carries the character: it
  is as heavy as both together, with the character's inertia where it
  stands and the turn its weight gives there, shared among every point the
  character stands on. A board on the floor stays put, standing across two
  boards keeps both still, a seesaw tips, and a board leant on a wall slips
  out from under a character (with a light board it used to hold, as the
  weight was only a few times the board's own).
- A character no longer shoves aside what it stands on: resting across two
  boards' edges it wedged them apart every step and was thrown up and down.
  What it walks into is still pushed.
- A kinematic body moved with `body3d_move_kinematic()` no longer keeps
  drifting once the game stops giving it a new target each step.
  `MoveKinematic` sets a velocity for that one step and Jolt kept it running;
  a door opened once would fly on for ever and eventually land outside the
  broadphase's range, crashing far from the cause. It now stops exactly where
  it got to.
- `physics3d` no longer crashes on a NaN or infinite position, rotation, size
  or velocity (`body3d_create`, `body3d_set_position`,
  `body3d_move_kinematic`, `body3d_set_velocity`, `body3d_add_impulse`,
  `character3d_create`, `character3d_set_velocity`,
  `character3d_set_position`): Jolt's broadphase would read past its bounds a
  step later, far from the bad value. The call is now refused, with a warning
  once per call site, instead.
- A glTF material whose base colour reads the model's second UV set
  (texCoord 1, a texture baked onto a fresh unwrap while the source UVs stay)
  now samples the right UVs: raylib samples every map with a material's first
  UV set only, so the mesh's two sets are swapped to match what the shader
  expects.

### Added

- 3D sound (`njin_audio.h`): `sound_play3d()` and `sound_loop3d()` play a
  sound at a point or on an entity's `transform3d`, heard from a listener that
  follows the last on-screen `begin_3d()` camera by default
  (`audio_set_listener3d()`, `audio_listener3d_follow_camera()`,
  `audio_listener3d_get()`). Each 3D sound has its own voice whose volume, pan
  and pitch are recomputed every frame: distance (`min_distance`,
  `max_distance`, inverse, linear or exponential `rolloff`), left and right
  from the listener's facing (`spread`), Doppler from velocities the engine
  measures or the game sets (`voice3d_set_velocity()`,
  `audio_set_speed_of_sound()`), a speaker cone, and optional occlusion by a
  physics ray (volume only, no muffling). The sound's own volume and its bus
  still apply. `voice3d_set_position()`, `voice3d_attach()`,
  `voice3d_set_desc()`, `voice3d_desc()`, `voice3d_stop()`,
  `voice3d_playing()`, and `voice3d_state()` for what was computed. Up to 64
  at once; `sound_stop()` also stops a sound's 3D voices. Computed by the
  engine and applied through raylib's per-sound volume, pan and pitch, since
  raylib mixes each sound itself and miniaudio's spatializer is not in that
  path.
- Soft bodies (`njin_physics3d.h`), on Jolt's soft bodies:
  `softbody3d_create()` makes a solid box lattice that keeps its volume, a
  hollow sphere, or the surface of a game mesh or a loaded model (vertices at
  the same place merged), with `stiffness`, `bend`, `pressure` (Pa at the
  starting shape, so a ball stays round and squashes when it lands), friction,
  damping and pinned vertices. They collide with every body, not with each
  other. Characters push them aside through a kinematic capsule that only soft
  bodies meet, so a curtain parts instead of blocking the way; raycasts and the
  `physics3d_*_push`/`_cast` queries pass through them.
- Cloth: `cloth3d_create()` makes a rectangular sheet (a flag, a curtain, a
  cape, a tablecloth) as a soft body, with edges pinned by `pin_edges`
  (`cloth3d_top`, ...) or single vertices by `pinned`, long-range attachments to
  the pins so it does not stretch, and a `thickness` that keeps it off surfaces.
- For every soft body: `softbody3d_model()`, a model of its current shape
  updated after each physics step (cloth gets back faces), for draw_model();
  `softbody3d_vertices()`, `softbody3d_normals()` and `softbody3d_indices()` to
  draw it yourself; `softbody3d_pin()`, `softbody3d_move_pinned()` (a cape
  pinned to moving shoulders), `softbody3d_nearest()`;
  `softbody3d_set_wind()`, air pushing on each face by its area and
  `drag`; `softbody3d_add_impulse()`, `softbody3d_position()`,
  `softbody3d_user()`, `softbody3d_destroy()`.
- Wheeled vehicles: `vehicle3d_create()` makes a car on Jolt's
  `WheeledVehicleController`, a dynamic chassis (a box, or a model's convex
  hull, with a lowered centre of mass) on suspended wheels, with an engine, an
  automatic gearbox, differentials per axle, anti-roll bars, brakes and a
  handbrake. Four wheels by default (front steering, four-wheel drive, rear
  handbrake), or the game's own `vehicle3d_wheel` list. `vehicle3d_set_input()`
  takes throttle, steering, brake and handbrake; a reversed throttle brakes to
  a stop before reversing. `vehicle3d_body()`, `vehicle3d_wheel_transform()`
  (its y axis is the axle, to draw a cylinder), `vehicle3d_wheel_grounded()`,
  `vehicle3d_rpm()`, `vehicle3d_gear()`, `vehicle3d_destroy()`. The tyres use
  the same longitudinal grip as Jolt's own vehicle sample; with Jolt's plain
  limit the wheels slipped every other step and the gearbox never left first
  gear.
- `body3d_carry()`: for the next step a dynamic body carries an extra weight
  at a point (someone hanging on it or climbing it), the same way it carries
  a character standing on it: a board leant on a wall slips as a climber goes
  up it, and a light board does not shake.
- Ragdolls (`njin_physics3d.h`), on Jolt's `Ragdoll`: `ragdoll3d_create()`
  turns the chosen bones of a skinned model into dynamic capsules joined by
  swing-twist joints or hinges (`ragdoll3d_bone::bend_min`/`bend_max`), with
  limits counted from the rest pose. Each capsule is fitted to the skin the
  bone moves (`radius` and `length` 0, the default), and parts of one
  ragdoll never collide with each other.
  `ragdoll3d_bones()` reads the pose back for drawing, `ragdoll3d_body()`
  gives each part's body for impulses, raycasts and contacts,
  `ragdoll3d_shape()` its collision shape for debug drawing, and
  `ragdoll3d_destroy()` removes it.
- `model_pose::bones`: draw a skinned model from bone frames the game sets
  (model space, as `model_bone_pose()` returns them) instead of an
  animation. nullptr, the default, keeps the animation as before.
- `model_bone_parent()`: a bone's parent, to walk a skeleton's chains for IK.
- `physics3d_capsule_push()`, `physics3d_box_push()`: the minimum
  translation vector that takes a capsule or a box out of the bodies it
  overlaps, so a character's limbs and feet can move aside from walls and
  boards instead of passing through them.
- `physics3d_box_cast()`: moves a box and reports the first thing it meets,
  to drop a foot onto uneven ground and find where it rests.
- Convex hulls for collision queries: `physics3d_hull_create()` from points
  (`model_bone_points()` gives a bone's skin), `physics3d_hull_push()` and
  `physics3d_hull_cast()` like the box ones, `physics3d_hull_lines()` for
  debug drawing; to fit a foot's or a hand's collider to its true shape.
- `model_bone_bounds()`: the box around the skin a bone moves (optionally
  with the bones below it), in the rest pose along the model's axes from the
  bone's origin, to fit colliders to a model's real shape.
  (puzzle: a hard hit, a long fall or standing over nothing drops the player
  into a ragdoll, who stands up where it lies; feet are set on the ground by
  two-bone IK.)

## 0.3.0

Compatible with 0.2.0: existing games build unchanged. One default changes:
`lighting_desc::occluder_lod` is on (1 pixel), so far-off occluder shapes
are simplified; set it to 0 to keep them exact.

### Added

- `lighting_desc::occluder_lod` (screen pixels, default 1): each frame the
  shape of every occluder is simplified to within that many pixels
  (Douglas-Peucker), and occluders smaller than it on screen are dropped, so
  a scene seen from far out keeps room for every shadow. **Changes the
  default:** existing games get it on; set it to 0 for the shapes as before.
- `mesh3d_data::texcoords`: per-vertex UVs for a mesh the game builds, so a
  piece cut from a textured mesh keeps the material's images. nullptr is
  (0, 0) everywhere, colour only, as before.
- `character3d_desc::push`: two characters that both have it no longer block
  each other; after each step overlapping pairs are moved apart on the
  horizontal, shared by mass, so a crowd slips past itself. Walls, bodies and
  other characters still block as before.

### Fixed

- Half-transparent shapes no longer come out darker on screen. The finished
  frame (with a virtual size or `render_scale`) and the world after its post
  shader were blended by their own alpha, which such shapes leave below 1;
  they are now copied as they are.
- Sun (`light_directional`) shadows no longer shrink to a stripe through the
  middle of the screen when there are many occluders, and no longer flicker
  as the camera moves. A sun strip holds 128 edges (64 for other lights) and,
  when full, keeps the longest, choosing among equal lengths by position so
  the same ones stay from frame to frame; a sun whose strips overflow is drawn
  in up to 64 bands along its rays, each with buckets of its own.

### Changed

- `draw_circle()` uses as many sides as its size on screen needs (6 to 36)
  instead of always 36, so thousands of small circles seen from far cost far
  less.

## 0.2.0

- `model_create_skinned()` builds a skinned model from a game-assembled mesh
  (`skinned_mesh3d_data`: positions, four bone indices and weights per vertex,
  split into parts, indices), copying the skeleton of an already-loaded model
  and sharing its animations, so one clip library serves many characters built
  from parts (hair, head, body) picked per instance. `model_bone_find()`,
  `model_bone_name()`, `model_bone_count()` look up bones by name;
  `model_bone_pose()` gives a bone's world-space pose for the current
  `draw_model_anim()` call (attaching props, hit boxes). `draw_model_anim()`
  also takes an array of `model_recolor` to recolour several materials in one
  draw (shared mesh, per-character palette).
- Game shaders used in 3D draws (`shader_begin()`, a model material's own
  `model_material::shader`, including on a part drawn with `draw_instanced3d()`)
  now get `fogColor`/`fogDensity` uniforms when declared, and the extra
  textures set with `shader_set_texture()` are bound for them too. A model
  material's own shader is now honoured in `draw_instanced3d()`, drawing that
  part with it instead of the call's shader, as `draw_model()` already did.
- `model_material_find()` finds a model's material by (the start of) its glTF
  name; `draw_model_anim()` takes a `model_recolor` to draw one material in
  another colour for that draw only (a uniform on a shared character model).
- `model_material::double_sided`: a glTF material marked `doubleSided` is drawn
  on both faces (leaves made of single sheets).
- Fix: a skinned clip's last frame was its first pose (raylib's glTF sampling
  at exactly the clip's end), so motions held at their end (a fall, a kick)
  snapped back to the start.

- `material3d::world_uv`: images mapped by world position (walls with v up,
  floors on x/z), a tile every so many 3D units, with no visible tiling (two
  offsets blended by a slow noise); works on meshes without UVs and on
  instanced boxes. `material3d::normal` gives shapes and `draw_instanced3d()` a
  normal map, and `draw_instanced3d()` now uses a model material's
  game-set `normal`. `material3d::under`, `under_normal`, `under_amount`: a
  second layer (brick under render) showing in worn patches.
  `filter_mipmap`: mipmaps and trilinear filtering for such repeated images.
  (sandtable: aged concrete and brick on the town's walls.)

- `window_set_close_intercept()` and `window_close_requested()`: the window's
  [x], Alt+F4 and the exit key can be handed to the game, which cleans up and
  calls `quit()` itself.

## 0.1.0

Initial public release from the current source tree.

- C++20 engine for 2D and 3D games, with ECS, rendering, audio, input, UI,
  tilemaps, physics, debugging and editor tools.
- Small examples build independently against an exported njin engine build.
- Serious game projects are maintained in separate repositories.
