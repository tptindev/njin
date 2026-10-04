# Changelog

Versions follow [semver](https://semver.org). Before 1.0 the public API may
change between MINOR versions. The number lives in `src/engine/api/njin_version.h`.

To release: edit that header, add a section here, commit, then
`git tag -a vX.Y.Z -m "njin X.Y.Z"` and push the tag.

## Unreleased

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
