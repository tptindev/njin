# Changelog

Versions follow [semver](https://semver.org). Before 1.0 the public API may
change between MINOR versions. The number lives in `src/engine/api/njin_version.h`.

To release: edit that header, add a section here, commit, then
`git tag -a vX.Y.Z -m "njin X.Y.Z"` and push the tag.

## 0.1.0

First numbered version. What the engine has:

- **Core**: EnTT ECS behind `njin.h`, raylib 6.0 hidden, modules and named
  systems in nine phases, scenes with fades and prefabs, fixed-step physics.
- **2D building blocks**: sprites and Aseprite animation with a state machine,
  chunked tilemaps with tile shapes (one-way, 45 and 22.5 degree slopes) and
  animated tiles, Tiled and LDtk levels, box/circle collision with layers and
  raycast, parent-child transforms, particles, screen FX, post-processing,
  a virtual resolution for pixel art.
- **Gameplay**: `platformer_body` (coyote time, jump buffer, wall jump, double
  jump), `topdown_body` (8-way, dash), moving platforms, camera follow with
  deadzone and level bounds, y-sorted drawing, A* navigation, timers and tweens.
- **Game plumbing**: UI (buttons, sliders, popups, toasts, key rebinding, gamepad
  navigation, custom skins and shaders), dialogue, localization, audio buses
  with music crossfade, settings file, JSON, save path, hot reload.
- **Tools**: `njin_inspector`, a separate process over a local socket: entities,
  world view, log, time control, and CPU/RAM/GPU consumption per system,
  component, entity and asset.
- **Samples**: `njin_platformer`, `njin_topdown`, `njin_debug_demo`; release
  packaging with `njin_package()`.

Known gaps: not tried with a real gamepad, not built on Linux or macOS (the
inspector reads GPU figures on Windows only).
