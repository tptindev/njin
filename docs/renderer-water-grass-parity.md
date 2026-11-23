# Water and grass parity continuation

Apple/Web work is deferred by the user's current scope. This continuation
targets the existing Windows, Linux and Android SDL3/bgfx configurations.

## Added paths

- `water3d_desc::pour`, `pour_at`, `pour_reach`: legacy radial normal gradients,
  noise, churn and foam, with derivative-based filtering for subpixel rings.
- `water3d_splash`: existing eight-slot CPU records become immutable render
  packets; the shader computes wave-train propagation, spreading, decay and
  transient churn. Expired records stop affecting subsequent draws.
- `ice` and `ice_snow`: shallow-first ragged frozen coverage, frost and snow
  drift colors, flat detail normals, opaque frozen coverage and suppressed foam.
  The mesh now damps displacement/normals exactly as the existing queries do.
  Previously only the query side applied the ice calming factor.
- `grass3d_desc::far_cover`: the first matching field modifies terrain albedo
  with legacy tip/tint color, distance fade, splat-layer weight, slope and minimum
  height, including terrain drawn before any blade draw call. Degenerate slope
  and minimum-height sentinel cases avoid undefined `smoothstep` denominators.

Pour/splash remain visual normal effects, consistent with the public API;
they do not alter collision, buoyancy or water height queries. Ice uses the
portable lighting path; matching the legacy sky reflection and glint appearance
depends on the remaining reflection/sky port.

`RenderWaterEffects` contains only CPU numeric data. Its immutable shared
snapshot is retained with each `MeshDraw`; the bgfx adapter owns its uniform.
The extension is appended to preserve aggregate initializer prefixes. Rebuild
engine libraries and applications together after the packet layout change.
The Android APK builder already rebuilds all imported archives for this reason.

## Verification

- Windows **16/16**, 172.93 s: world, advanced materials, renderer batch and GPU
  particles on Vulkan, bgfx OpenGL, D3D12 and Vulkan validation.
  Log: `build/water-grass-windows-tests.log`.
- Linux software Vulkan/OpenGL/validation **12/12**, 370.95 s, same four families.
  Log: `build/water-effects-linux-tests.log`.
- BGFX-OFF legacy OpenGL **4/4**, 41.78 s.
  Log: `build/water-effects-legacy-tests.log`.
- Android world fixture **2/2**, Vulkan and GLES: both native assertions and
  controller checks pass, including both contexts. Logs:
  `build/water-effects-world-{vulkan,gles}.log`; captures/status files:
  `build/android-device-captures/water-effects-world-{vulkan,gles}/`.

The world fixture now has 32 stages and repeats two contexts. It asserts pour
animation, splash propagation/expiry, opaque ice/snow coverage, flat frozen mesh
and query agreement with active wave descriptors, and far-cover layer/height
masks and reset behavior. All images come from Njin's own capture API.

```sh
cmake --build build --target njin_portable_world_test
ctest --test-dir build -R '^portable_world_' --output-on-failure
```

## Water optics

Water uses the opaque HDR color and depth that the OIT pass already produces.
Use `post3d.oit` for transparent world water. `refract` bends the view through
the surface and applies `absorb` per metre to submerged color. `reflect` traces
the opaque depth buffer and fades to the IBL environment or clear color on a
miss. `caustics` adds moving patterns at the reconstructed bed, modulated by
sun color, depth and shadow. Ice attenuates the optical effects.

Screen-space optics can see only the opaque layer in the current view.
Transparent objects do not appear in the optical snapshot. Reflections beyond
the screen use the environment fallback. The open sea reaches the horizon
(see the world port scope). See the complete
[world port scope](renderer-world-port.md) and
[feature ledger](renderer-port-backlog.md) for the other pending families.

The earlier complete Windows 159-case run in the integration-batch report
predates this continuation; the targeted results above apply to these changes.
