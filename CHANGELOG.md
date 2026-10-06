# Changelog

Versions follow [semver](https://semver.org). Before 1.0 the public API may
change between MINOR versions. The number lives in `src/engine/api/njin_version.h`.

To release: edit that header, add a section here, commit, then
`git tag -a vX.Y.Z -m "njin X.Y.Z"` and push the tag.

## Unreleased

### Fixed

- A character standing on a light dynamic body (a board lying on the floor)
  no longer makes it shake. Its weight was put on the body as an impulse
  straight into the body's velocity outside the solver; it is now a force
  the solver takes in with the body's other contacts.

### Added

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
