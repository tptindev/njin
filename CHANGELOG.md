# Changelog

Versions follow [semver](https://semver.org). Before 1.0 the public API may
change between MINOR versions. The number lives in `src/engine/api/njin_version.h`.

To release: edit that header, add a section here, commit, then
`git tag -a vX.Y.Z -m "njin X.Y.Z"` and push the tag.

## Unreleased

### Fixed

- Rain no longer collapses into a flat sheet every few seconds: each drop's
  start height and its fall speed came from the same random number, so every
  ~4.9 s (when the spread of speeds had added up to the 22 m box) all drops
  lined up at one height, and for a while rain showed only high above the
  camera. Snow had the same fault every ~44 s. The speed now has its own
  random number.
- Navmesh paths and crowd agents sit on the ground over hilly terrain. Points
  took Detour's detail height, which samples the ground only every few cells
  (path corners up to 0.54 m off), and straight segments between corners cut
  through hills (up to 2.4 m under a hilltop). Over terrain added with
  `navmesh3d_add_terrain()`, `navmesh3d_path()` points and
  `nav3d_agent_position()` now take `terrain3d_height()`, and a segment that
  would leave the ground by more than 5 cm gets points in between: on the
  batch-7 hills, corners are exact and segments within 5.3 cm.
- `physics3d_raycast()` with a `body` out-parameter no longer trips Jolt's
  lock check (two "lock of same or higher priority" asserts in Debug): it read
  the hit body's handle through the body interface, locking the body a second
  time while it was already locked for the surface normal. The handle is now
  read from the locked body.
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

- Lua state in save games: `script_save_state()` returns a `json_value` with
  the `self` of every entity script that has a save name
  (`script_set_save_id()`, or `self.save_id` from Lua), keyed by that name
  since entities get new numbers when a game is loaded; numbers, booleans,
  strings, `vec2`/`vec3` and nested tables are kept exactly (integer keys as
  `"#n"`), and functions, other userdata, cycles and non-finite numbers are
  left out with a warning giving their path. `script_load_state()` puts the
  fields back into the recreated entities' scripts and calls their new
  `on_load(self)`; `script_save_id()` reads a name back.
- Feet on the ground (`njin_anim3d.h`): `foot3d_create()` takes a model's legs
  (found by their humanoid names when none are given) and `foot3d_update()`
  probes the ground under each ankle (`physics3d_raycast()`, or a game's
  `foot3d_ground` function for terrain), lowers the hips for the lower foot,
  bends each leg by two-bone IK (knees on the animation's side, or
  `knee_forward`), tilts the soles to the ground up to `max_tilt` and keeps a
  lifted foot lifted over the ground under it, smoothed, with a blend
  `weight`; it writes a pose for `model_pose::bones` and runs between
  `retarget3d_pose()` and `spring3d_update()`. `foot3d_hip_offset()`,
  `foot3d_reset()`, `foot3d_destroy()`. On a 0.2 m step, a step's edge and
  20 degree slopes the ankles stay within 6 mm of their flat-floor height.
- Temporal anti-aliasing: `post3d::taa` (off by default, so existing games
  draw exactly as before) jitters the projection of the frame's first 3D
  pass into the world by a Halton (2, 3) sub-pixel offset and blends it with
  a half-float history reprojected by the depth and the camera's motion
  (Catmull-Rom history fetch, YCoCg variance clipping, faster blending while
  the camera moves), then sharpens it back (`post3d::taa_sharpen`, 0.25).
  There is no per-object velocity buffer: the history is dropped where the
  depth it was drawn with does not hold the point, so a moving object leaves
  no trail but its edges stay aliased. Camera cuts, a new size, or TAA
  turned off and on start the history over. It runs in end_3d() after
  glass, water and particles and before light shafts, lens flare and motion
  blur; 2D drawn after end_3d() goes over the resolved image. Against a 2x2
  supersampled reference, edge error drops from 24.7 to 15.4 and stair-step
  pixels from 2725 to 1084 on a still camera. On an RTX 3050 Laptop
  (Release, 1280x720) it costs 0.3 to 0.4 ms.

- Morph targets reach the rest of the 3D pipeline. `draw_instanced3d()` has a
  version taking a `model_pose`: every instance of the call gets that morphed
  shape (one weight set per call; for a crowd with several faces, one call per
  group). `ray3d_model()` has a version taking a `model_pose` that tests the
  morphed shape. `model_lod_build()` keeps morphs at every level: each
  target's offsets go through the same welding and remaps, and the simplifier
  weighs the first ten targets' offsets so a flat surface a morph bends keeps
  its edges (meshoptimizer's `simplifyWithAttributes`); a morphed mesh used to
  be drawn at full detail only. A model loaded with `model_load_desc::merge`
  keeps the morphs of the meshes it merges (it dropped them, with a warning).
  Targets with `TANGENT` offsets blend the tangents too. The calls without a
  pose draw and test exactly as before.
- Rain and snow stay out from under roofs (`weather3d::cover_auto`, on by
  default): while it rains or snows the engine draws the depth of every
  opaque shadow caster straight down over 36 m round the camera, refreshed
  every 8 frames or after 2 m of camera movement, and hides drops under a
  surface; the terrain under it is not darkened by `wetness`. No measurable
  frame cost on an RTX 3050 Laptop (within 0.1 ms). `weather3d_cover_set()`
  adds up to 16 covered boxes of the game's own (porches that cast no shadow,
  or with `cover_auto` off), `weather3d_cover_count()`.
- Lua `on_fixed_update(self, dt)`: entity scripts run at the engine's fixed
  step, in `phase_fixed_update` right before the 3D physics step (a velocity
  set there moves the body in that step), with `dt` the fixed step. Nothing
  runs at the fixed rate while no loaded script has the function.
- 3D pathfinding (new `njin_nav3d.h`), on Recast and Detour (zlib, fetched,
  private to the engine). `navmesh3d_create()` takes the agent's radius,
  height, climb and slope; geometry comes from `navmesh3d_add_box()`,
  `navmesh3d_add_mesh()`, `navmesh3d_add_model()` and
  `navmesh3d_add_terrain()` (read again at each build), with
  `navmesh3d_add_link()` for jumps, ladders and drops. `navmesh3d_build()`
  builds it in tiles and `navmesh3d_rebuild()` rebuilds only an area after a
  change. Queries: `navmesh3d_path()` (corner points; stops at the nearest
  reachable point when the target cannot be reached), `navmesh3d_nearest()`,
  `navmesh3d_raycast()`, `navmesh3d_random_point()`,
  `navmesh3d_random_point_near()` (sampled inside the circle among the
  polygons reachable within it, as Detour's own only bounds the polygons it
  visits), `navmesh3d_draw_debug()`. Crowds: `nav3d_agent_add()` agents that
  find their path, steer round each other and keep apart
  (`nav3d_agent_set_target()`, `_stop()`, `_teleport()`, `_position()`,
  `_velocity()`, `_arrived()`, `_remove()`, `nav3d_agent_count()`), moved in
  `phase_post_update`; an agent can drive a `character3d`, which then falls by
  `physics3d_gravity()` and feeds its real position back. Lua gets
  `njin.nav3d_path`, `nav3d_set_target`, `nav3d_stop`, `nav3d_position`,
  `nav3d_velocity` and `nav3d_arrived` by handle id.
- Splines (new `njin_spline.h`), 2D and 3D with the same names: centripetal
  Catmull-Rom (through every point, no knots) and cubic Bezier, open or
  closed. `spline_point()`/`spline_tangent()` by parameter, and after
  `spline_bake()` an arc-length table for `spline_point_at()`,
  `spline_tangent_at()`, `spline_t_at()` and `spline_length()` (equal
  distances within 1% at the default 64 samples per segment);
  `spline_nearest()`; `spline_follow()` with a `spline_follower` that stops,
  loops or ping-pongs at the end; `spline_draw_debug()`. Written directly: a
  library would have been more than the hundred lines it takes.
- Video playback (new `njin_video.h`), MPEG-1 video with MP2 sound decoded by
  pl_mpeg (MIT, single header, fetched pinned to a commit, private to the
  engine). `video_open()` decodes in real time in `phase_post_update` (it keeps
  playing while the game is paused), with the sound on a raylib audio stream
  on a chosen bus, in step with the picture. `video_play()`, `video_pause()`,
  `video_seek()`, `video_set_loop()`, `video_set_volume()`, `video_time()`,
  `video_duration()`, `video_finished()`, `video_playing()`, `video_size()`,
  `video_framerate()`, `video_has_audio()`, `video_frame_count()`,
  `video_close()`. The current frame is a texture (`video_texture()`) for 3D
  screens, and `video_draw()`/`video_draw_fit()` draw it in 2D, fitted with
  bars. Other formats are refused with the ffmpeg command that converts them.
- Lua scripting (new `njin_script.h`), on Lua 5.4.9 and sol2 3.5, private to
  the engine. `script_run_file()`, `script_run_string()`, `script_call()`,
  `script_set_global()` and `script_get_global()` run code and call it from
  C++; `script_register()` puts a game's C++ function under a Lua name, with
  its parameter and return types converted (bool, numbers, strings, vec2,
  vec3, entities). `script_attach()` gives an entity a script file returning a
  table, whose `on_start`, `on_update(dt)`, `on_render`, `on_destroy` and
  `on_reload` run with a per-entity `self`; `script_field()` and
  `script_set_field()` read and write it from C++. Lua errors never stop the
  game: they are logged with file and line, once per message. Scripts are
  sandboxed (no `os.execute`, `io.popen`, C libraries or bytecode; no file
  access unless `script_desc::allow_io`), and `require` finds scripts like any
  asset. A game without scripts creates no Lua state and pays nothing.
- The `njin` Lua module: thin bindings over the existing API for time,
  randomness, entities, 2D and 3D transforms, keys, mouse, actions and axes,
  timers and tweens, 2D and 3D sound, scenes, 2D drawing, `platformer_body`
  and `topdown_body` inputs and state, `collision_move`, overlap queries and
  2D raycasts, sprite flip, tint and animator clips, `camera_follow`, tilemap
  cells, 2D particles, 3D raycasts, body velocity and impulses, and
  `character3d` movement; `vec2` and `vec3` with arithmetic.
- Hot reload covers scripts: with `hot_reload_enable()` on, a changed script
  is run again and its functions replace the old ones for every entity using
  it while `self` keeps its data; a broken file keeps the old functions.
  `asset_reloaded::script` (a new field at the end) marks those events.
- 3D screen effects (new `njin_post3d.h`), all off by default so existing
  games draw exactly as before: `post3d_set()` with `post3d::ssao`
  (ambient occlusion from the depth, half or full resolution, depth-aware
  blur, multiplied into the image), `post3d::ssr` (screen-space reflections
  marched over the depth, on surfaces with the new `material3d::reflect`,
  fading to the fog colour where the ray leaves the screen),
  `post3d::motion_blur` (camera motion blur from the depth and the last
  pass's view-projection; a still camera leaves the image untouched),
  `post3d::shafts` (light shafts: the sky near the sun blurred towards it, so
  objects in front of the sun cut rays) and `post3d::flare` (lens flare
  ghosts, halo and glow, faded by how much of the sun's disc shows). They
  run in end_3d() on passes into the world, not into render textures. No
  TAA: it would need every projection jittered, the 2D drawn into the world
  image included, and per-object velocities. On an RTX 3050 Laptop (Release,
  1280x720) each costs 0.15 to 0.4 ms (full-resolution SSAO 0.54 ms), all
  of them with 50 decals about 1 ms.
- Decals: `decal3d_add()` projects an image (or a soft round spot) from a box
  onto every opaque surface inside it, walls, models and terrain alike, by
  the depth; `decal3d_multiply` darkens and keeps the surface's light and
  shadows, `decal3d_paint` lays lit colour over. A lifetime in game time
  with a fade, a fade on surfaces slanting from the projection axis, and a
  pool of 256 by default where the oldest is replaced (`decal3d_set_max()`).
  `decal3d_rotation()` turns a decal onto a hit's normal, `decal3d_remove()`,
  `decal3d_clear()`, `decal3d_count()`.
- Outdoor world (new `njin_world3d.h`). Terrain: `terrain3d_create()` builds a
  square height grid from an array, a height map (grey image or 16-bit
  `.r16`/`.raw`) or noise, with up to four surface layers covering ground by
  height and slope rules (`auto_splat`) or a splat map, tiled without visible
  repetition and projected from three sides on cliffs. It is drawn in chunks
  culled by view, with distance levels of detail and skirts so no cracks
  show, lit, shadowed and fogged like other 3D shapes. `terrain3d_height()`,
  `terrain3d_normal()` and `terrain3d_layer_weight()` answer for any point,
  the same triangles as the drawn grid and the Jolt height field body
  (`terrain3d_body()`) characters, vehicles and bodies stand on.
  `terrain3d_edit()` raises, lowers, flattens or smooths a round area and
  `terrain3d_paint()` paints a layer; the mesh, collision, automatic layers,
  grass and scattered objects follow, and bodies resting on raised ground
  are lifted with it.
- Grass and scattered objects: `grass3d_create()` grows instanced blades on
  a terrain layer near the camera only, thinning and shrinking with
  distance, swaying in the wind (`wind3d_set()`). `scatter3d_create()`
  places a model (rocks, trees) by density, spacing, height, slope, layer and
  noise clusters, leaning with the ground, drawn per area with a far model
  and a draw distance; `scatter3d_transforms()` gives the placements for
  collision.
- Water: `water3d_create()` makes a lake or an open sea following the camera,
  with up to eight Gerstner waves, depth colour, clarity and shore foam from
  an attached terrain, crest foam, ripples, sky reflection with fresnel and
  sun glitter, in the translucent pass. `water3d_height()` and
  `water3d_normal()` match the shader's waves; `water3d_float()` gives a
  dynamic body buoyancy and drag each physics step (Jolt's
  `ApplyBuoyancyImpulse`).
- Sky and weather: `draw_sky3d()` draws a sky by time of day, latitude and
  season behind everything (sun, halo, sunset glow, drifting clouds, stars and
  moon) and, by default, sets the pass's light from it: sun or moon direction
  and colour, ambient from the sky, fog colour from the horizon
  (`sky3d_light()` to do it yourself). `weather3d` adds cloud cover, fog,
  rain, snow, wind and wet ground; `weather3d_preset()` has clear, overcast,
  rain, snow and fog, `weather3d_lerp()` blends them. Rain and snow fall in a
  box round the camera computed on the GPU. On an RTX 3050 Laptop (Release,
  960x540) a 1 km² terrain with grass, about 9000 rocks, a lake, sky and sun
  shadows draws in about 1.7 ms a frame.
- Morph targets (blend shapes) from glTF: model_load() reads each mesh's
  targets (positions and normals, sparse accessors included) with their
  names from `extras.targetNames`; targets of the same name in several meshes
  are one morph. `model_morph_count()`, `model_morph_name()`,
  `model_morph_find()`, and `model_morph_weights()` for the weights a draw
  will use. A draw's weights are the file's defaults, then the playing clip's
  weight curves (the glTF `weights` channel, linear, step or cubic spline,
  blended with `blend_anim`), then `model_pose::morph_weights` added on top.
  They are blended on the CPU into the mesh's vertex buffers right before a
  draw whose weights differ from what the buffers hold, then skinned on the
  GPU as before, so morphs work with every shader, shadows included. A model
  without a skin whose clips only move weights gets those clips as its
  animations (`model_anim_count()` and the other clip functions). The new
  fields are at the end of `model_pose`, so existing games build unchanged.
- Spring bones (`njin_anim3d.h`): `spring3d_create()` takes chains (a bone
  and every bone below it, with VRM's stiffness, drag, gravity and radius)
  and sphere or capsule colliders on bones; `spring3d_update()` runs them in
  the world from the draw's transform, on top of an animation or bones the
  game set, and writes a pose for `model_pose::bones`. `spring3d_reset()`
  after a teleport, `spring3d_destroy()`.
- Retargeting: `retarget3d_create()` matches a target skeleton's bones with a
  source skeleton's, by name pairs the game gives, then by standard humanoid
  names, then by the same name; `retarget3d_pose()` turns each matched bone
  by the angle its source bone turned from its rest pose, lets unmatched
  bones follow their parent, and moves the hips scaled by the ratio of hip
  heights. `retarget3d_source_bone()`, `retarget3d_destroy()`.
  `bone_humanoid_name()` reads Mixamo, Unreal, Unity/VRM and Blender bone
  names.
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
  damping and pinned vertices. They collide with every body, and with each
  other (`softbody3d_desc::collide_soft`, `cloth3d_desc::collide_soft`, on by
  default): Jolt has no soft-soft collisions, so before each step the engine
  keeps every vertex both thicknesses off the other body's faces, by velocity
  (a cloth dropped on a hammock stays on it; two 40 x 40 cloths lying on each
  other cost about 4 to 5 ms a step on an RTX 3050 Laptop's CPU, Release).
  Characters stand on their upper side (`walkable`, on by default), pressing
  the vertices under their feet with their weight, and still walk through an
  upright face (a curtain parts, through a kinematic capsule that only soft
  bodies meet); `character3d_ground_soft()` gives the soft body stood on and
  `character3d_ground_velocity()` its surface's velocity. Raycasts and the
  `physics3d_*_push`/`_cast` queries pass through them; new overloads of
  `physics3d_raycast()`, `physics3d_box_cast()` and `physics3d_hull_cast()`
  taking a `soft3d_hit` hit them and report the soft body, face and nearest
  vertex.
- Cloth: `cloth3d_create()` makes a rectangular sheet (a flag, a curtain, a
  cape, a tablecloth) as a soft body, with edges pinned by `pin_edges`
  (`cloth3d_top`, ...) or single vertices by `pinned`, long-range attachments to
  the pins so it does not stretch, and a `thickness` that keeps it off surfaces.
- For every soft body: `softbody3d_model()`, a model of its current shape
  updated after each physics step (cloth gets back faces; past 65535
  vertices it is split into several meshes of one model), for draw_model();
  `softbody3d_draw_debug()` draws its edges and pins with gizmos;
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
- Motorcycles and tracked vehicles: `motorcycle3d_create()` on Jolt's
  `MotorcycleController` (a raked steering front wheel, a driven rear one, a
  lean spring that holds the bike up and leans it into turns; defaults after
  Jolt's 240 kg sample) and `tracked3d_create()` on its
  `TrackedVehicleController` (two tracks over `wheels_per_side` wheels; steering
  alone while about stopped turns it on the spot). Both are `vehicle3d_handle`s
  driven by `vehicle3d_set_input()`; `vehicle3d_set_tracks()` drives each track,
  `vehicle3d_track_speed()` gives its speed. `vehicle3d_draw_debug()` draws any
  vehicle's chassis, wheels, suspension and ground contacts with gizmos.
- Cloth takes up to 512 x 512 cells (was 180).
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

### Changed

- The model editor tool is replaced by an animation editor:
  `src/tools/model_editor` is now `src/tools/anim_editor` (target
  `njin_anim_editor`, `run_anim_editor.bat`, CMake option
  `NJIN_BUILD_ANIM_EDITOR`). It opens a skinned `.glb`/`.gltf` and animates
  its skeleton: the file's own clips open for editing, bones are posed with
  ImGuizmo on a keyframe timeline, projects are saved as `.anim.json` (the
  model path and the clips, keys naming bones), and **Export glTF** writes a
  `.glb` with the model, its skin, materials, textures and every clip as glTF
  animations that `model_load()` plays as they were authored. Channels a clip
  does not edit (scale, morph weights, nodes outside the skin) are kept from
  the source animation of the same name. Writing uses cgltf_write (MIT),
  vendored at the cgltf commit whose parser raylib 6.0 bundles.

### Removed

- The SDF modelling part of the model editor: shapes, CSG, the live SDF
  preview, OBJ export and the shape-built humanoid. Old `.model.json` projects
  are refused with a message, as they have no glTF rig.

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
