# 3D graphics {#graphics_3d}

njin draws 3D worlds: a perspective camera, primitives, smooth SDF shapes, glTF models, lighting
with shadows, materials, effects, particles, instancing, physics and collision, mouse picking and
gizmos for debugging. Everything goes through `njin.h` (declared in `njin_3d.h`, `njin_physics3d.h`
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
| draw_instanced3d() | Thousands of mesh shapes in one draw call | Forests, crowds, floor tiles |
| draw_model() | glTF/OBJ model loaded with model_load() | Props, characters made in Blender |

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

## Lighting

| Part | Set with | Notes |
|---|---|---|
| Sun, ambient light | light3d_set() with njin::light3d | Takes effect from the next begin_3d() |
| The sun's shadow | `light3d::shadows`, `shadow_range`, `shadow_size`, `shadow_softness` | A shadow box around where the camera looks; 3x3 soft edge |
| Fog | `light3d::fog_color`, `fog_density` | By distance to the camera |
| Point, spot lights | light3d_add() with njin::light3d_source, per draw | Up to njin::light3d_max (16), cast no shadow |
| Surface | material3d_set() with njin::material3d | Shininess, emission, rim light, `unlit`, `texture`, `cast_shadows` |

Lighting is Lambert plus a Blinn-Phong specular highlight, plus ambient light. `emission` adds colour
after lighting: turn on bloom (post_fx_set()) so a self-lit shape glows into its surroundings.

## Effects

The shared effects of @ref particles all work for 3D too:

| Effect | With 3D |
|---|---|
| camera_shake() | Shakes begin_3d()'s camera: the offset becomes a turn of the view, the tilt a roll of the camera |
| hitstop(), screen_flash(), post_fx_set(), scene transitions | Same as 2D, nothing extra needed |
| Colour flash, dissolve | fx3d_set() with njin::fx3d: the equivalent of njin::flash_fx and njin::dissolve_fx for sprites |
| Particles | particles3d_spawn() reuses a 2D emitter (njin::fx::explosion(), sparks(), dust()...) |

A 3D shape is not an entity, so the game keeps the effect's own time and sets its level every frame:

@code
// A target that was just hit: flashes white, then dissolves over 0.4s.
const float t = age / 0.4f;
njin::fx3d_set(ctx, {.flash = {1, 1, 1, 1 - t * 3}, .dissolve = t});
njin::draw_sphere3d(ctx, pos, 0.6f, njin::colors::red);
njin::fx3d_set(ctx, {});
@endcode

3D particles face the camera, stop during hitstop, and `particles3d_desc::scale` converts an emitter
preset's pixel units to 3D world units.

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
| Ray | physics3d_raycast() | Bullets, line of sight, a camera that does not go through walls; returns the body hit |

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

Draw a dynamic body exactly where and how physics placed it with body3d_transform():

@code
const njin::transform3d t = njin::body3d_transform(ctx, crate);
njin::draw_shape3d(ctx, {.kind = njin::shape3d_box, .position = t.position, .rotation = t.rotation,
                         .size = {1, 1, 1}}, {0.7f, 0.5f, 0.3f, 1.0f});
@endcode

`body3d_desc::user` attaches a number of the game's to a body (an array index, an entity id): read it back
with body3d_user() from the body that physics3d_raycast() or character3d_ground_body() returns.

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

## Not there yet

- Model skeletal animation (glTF skins): a model draws in its bind pose.
- Shadows from point and spot lights: only the sun casts one.
- Joints, bodies from a model's triangle mesh, collision events: not in `njin_physics3d.h` yet.
- 3D components in the ECS: 3D shapes and physics bodies are used directly, not as entities.

## Sample games

| Game | What to look at |
|---|---|
| `njin_fps` | A first-person camera, a glTF model (the gun), shadows, fog, coloured lights, a flashlight (a spotlight), a muzzle light, glowing tracers with bloom, sparks and an explosion from 3D particles, a hit target that flashes then dissolves, camera shake, hitstop, `ray3d_box`, gizmos (G key) |
| `njin_sokoban` | A 2.5D camera, the floor and walls drawn with draw_instanced3d(), a textured crate, an SDF capsule character with a rim light, a point light over each goal, dust and sparkle particles, fading in on level start, gizmos (G key) |
| `njin_platformer3d` | A third-person camera that does not clip through walls (physics3d_raycast), a physics character (character3d) with coyote time and a double jump, static platforms, a moving platform that carries you (kinematic body), pushable crates (dynamic bodies), checkpoints, dissolving out and back in after a fall, a glowing goal ring, gizmos (G key) |
