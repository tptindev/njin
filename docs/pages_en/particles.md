# Particles and effects {#particles}

The little things that give a game "impact": dust when landing, sparks when a slash connects, the camera shaking on
an explosion, the frame pausing for a beat when a hit lands. njin has all of it built in, plus a set of ready-made presets.

@include particles_fx.cpp

@image html particles_explosion.gif "A particle explosion in njin_render_demo: each Space press is an explosion at the mouse cursor, the particles spread out and then fade"

## Particles

Attach njin::particle_emitter to an entity that has njin::transform. The engine's particle module spawns and
updates particles in `phase_post_update` using delta() (so they stop on pause and hitstop, and slow down
with time_set_scale()); the sprite module draws them together with sprites, by `layer`. Within the same layer, particles
are drawn after sprites.

Three ways to emit:

| Way | How | Use for |
|---|---|---|
| Continuous | `rate` > 0, `emitting = true` | Fire, smoke, rain, trails |
| A burst | njin::particles_burst() on an existing emitter | Firing sparks from a gun |
| Once, then self-destroy | njin::particles_spawn() | Explosions, dust, blood: creates an entity, bursts, destroys itself when the particles run out |

Entities created by njin::particles_spawn() belong to the running scene, so they are gone when you leave the scene.

### Main fields

| Group | Fields |
|---|---|
| Emission | `rate`, `emitting`, `max_particles`, `area` (spawn region around the transform) |
| Motion | `life`, `speed` (a min–max range), `angle` + `spread` (direction and spread, in degrees), `gravity`, `drag`, `spin`, `local_space` |
| Visuals | `size_start` → `size_end`, `size_jitter`, `color_start` → `color_end`, `shape` or `texture` + `source`, `blend`, `layer` |

Direction: 0 degrees is right, 90 is **down** (the y axis points down), -90 is up. The transform's rotation is
added to `angle`, so rotating the entity also rotates the emission direction.

`local_space = true` makes particles follow the emitter (fire at the tail of a rocket); by default particles stay in the
world and leave a trail behind.

`blend_additive` makes overlapping particles glow brighter: use it for fire, sparks, magic.

### CPU or GPU

The engine chooses by itself when the game starts. A machine with a graphics card (OpenGL 3.3 or newer, not a software
renderer like llvmpipe, SwiftShader or Microsoft Basic Render Driver) runs particles on the
GPU; a machine without one runs them on the CPU, exactly as before. The game does not have to do anything, and both ways give
almost the same picture.

On the GPU, the CPU only records each particle's state at spawn time. Each emitter has its own vertex buffer,
written only when there are new particles or when dead particles are cleaned up (about every 0.25 seconds), so a frame
costs no per-particle work on the CPU. The vertex shader computes the current position with a formula
(acceleration and drag have closed-form solutions), and each emitter is drawn with **one instanced call** instead of
one draw call per particle. The result differs from the CPU by less than half a pixel at 60 FPS, because the CPU accumulates each
Euler step while the GPU computes it exactly.

Tested on an Intel Iris Xe, Release build, round particles, 10,000 particles alive at once: about 42 ms per
frame on the CPU, about 1 ms on the GPU (the test is capped at 1000 FPS, so that is a lower bound). Even
200 small emitters, 20 particles each, are still much faster on the GPU, so there is no "big enough emitter" threshold.

A few things to know:

- njin::particle_emitter::gpu tells you where the emitter is running. When it is `true`, `pos`,
  `velocity`, `rot` in `particles` hold the values at spawn time, and `age` is the **spawn time** on the
  emitter's own clock, not the age. `particles` also still holds dead particles that have not been cleaned up yet,
  so count live particles with the inspector (the `alive` field) or check `gpu` before reading directly.
- An emitter only switches where it runs when it has no particles, so particles in flight do not jump.
- The game's post shader (njin::camera_set_post_shader()) runs over the whole finished frame,
  so it still sees GPU particles like everything else. Round particles (no texture) on the GPU specifically have
  antialiased edges, while the CPU draws polygons without antialiasing.
- njin::particles_set_backend() with `particle_backend_cpu` forces the CPU, for comparing or
  hunting bugs. njin::particles_gpu_available() tells you whether this machine can use the GPU.

An emitter outside the camera is not drawn (the simulation still runs so particles are in the right place when you come back). The engine
knows the region particles can reach from where they spawn, their speed, lifetime and acceleration, so an emitter is
only skipped when it is certain that no particle is in the frame.

### Ready-made presets

In `namespace njin::fx`, each function returns an emitter that is already tuned; change it however you like before using it:

| Preset | Emission | Hint |
|---|---|---|
| njin::fx::explosion() | Once | 40–80 particles |
| njin::fx::sparks() | Once | 10–25 particles, adjust `angle`/`spread` to the hit direction |
| njin::fx::dust() | Once | 6–12 particles at the feet when running or landing |
| njin::fx::debris() | Once | 8–16 particles when a crate or rock breaks |
| njin::fx::splash() | Once | Blood or water splashing along the direction of the hit |
| njin::fx::sparkle() | Both | Picking up items, healing, magic |
| njin::fx::smoke() | Continuous | Chimneys, burning cars |
| njin::fx::fire() | Continuous | Torches, campfires |
| njin::fx::rain(), njin::fx::snow() | Continuous | Attach to an entity that follows the camera, with `area` as wide as the screen |
| njin::fx::trail() | Continuous | Attach to bullets, rockets, darts |

## Screen and time effects

| Function | What it does | Common values |
|---|---|---|
| njin::camera_shake() | Shakes the camera. Stacks up, fades on its own | 0.2 heavy footstep, 0.4 taking a hit, 0.8 explosion |
| njin::hitstop() | Freezes the frame: delta() is 0 for a moment | 0.03–0.12 seconds |
| njin::screen_flash() | Flashes the whole screen one color, then fades | white on an explosion, red when hurt |
| njin::sprite_flash() | Paints the sprite one color (usually white) | 0.1 seconds on a hit |
| njin::sprite_dissolve() | Makes the sprite dissolve away in patches, with a burning edge | 0.5–1 second when an enemy dies |

Camera shake only shifts **what is drawn**: the camera's transform, njin::camera_active(),
njin::w2scr() and njin::scr2w() do not change, so mouse clicks still land in the right place. The way it shakes is tuned with
njin::camera_shake_config().

Shake, screen flash and hitstop use **real time**, so the camera keeps shaking during hitstop.
Sprite flash uses delta(), so it "freezes" together with the game during hitstop, as you would expect.

Sprite flash is different from `sprite.tint`: tint only **multiplies** the color so it cannot make a sprite glow white;
flash replaces the color of every pixel but keeps the sprite's shape.

## Dissolve

njin::sprite_dissolve() makes a sprite dissolve away in small patches, with an orange burning edge where it is dissolving. Use it when an
enemy dies or an item disappears. The sprite keeps its shape and just loses patches, so you can tell right away what is dissolving.

@include sprite_dissolve.cpp

@image html sprite_dissolve.gif "Press Space: flash white, then dissolve with an orange edge. Press R: fade back in"

Each patch has a fixed random number computed from its pixel coordinates (with a hash function, **no noise image needed**);
any patch whose number is lower than the running threshold from 0 to 1 disappears. To tune it further, attach njin::dissolve_fx yourself:

| Field | Meaning |
|---|---|
| `duration` | Time to dissolve completely, in seconds. Uses delta(), so it **freezes during hitstop** |
| `edge_color`, `edge_width` | Color and thickness of the burning edge. `edge_color.a = 0` or `edge_width = 0` removes the edge |
| `grain` | Size of each patch, in pixels of the sprite image. 1 is single pixels, 4 is large patches, good for pixel art |
| `seed` | Changes the dissolve pattern. Give each enemy its own value so they do not dissolve identically |
| `reverse` | Appear gradually instead of dissolving away |
| `destroy_when_done` | Destroy the entity when it finishes dissolving (does not apply to `reverse`) |

When time runs out:

- **dissolving away**: the sprite is **fully hidden** and the component stays, so it does not reappear. Attach `destroy_when_done` to destroy the
  entity too, or remove the component yourself to make the sprite show again;
- **appearing gradually** (`reverse`): the component is removed automatically and the sprite is fully shown.

It works together with njin::sprite_flash(): call both and the sprite flashes and dissolves in the same draw. Calling
njin::sprite_dissolve() again while it is dissolving restarts it. Calling it with a destroyed entity or `entt::null` is ignored.

@note The dissolve pattern is taken from the pixel position **in the image** (atlas), so two enemies using the same frame will dissolve
identically if they have the same `seed`. Set different `seed` values, for example from the entity id.

See also @ref post_processing for full-screen effects such as a red vignette at low health.
