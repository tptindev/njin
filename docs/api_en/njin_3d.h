#pragma once
#include "_math.h"
#include "njin_particles.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_3d
/// @{

/// Perspective camera for begin_3d().
///
/// Y axis up, right-handed (like glTF and raylib): looking down `-z`, `+x` is
/// to the right.
struct camera3d {
  vec3 position{0.0f, 0.0f, 0.0f};  ///< Eye position.
  vec3 target{0.0f, 0.0f, -1.0f};   ///< Point the camera looks at.
  vec3 up{0.0f, 1.0f, 0.0f};        ///< The camera's up direction.
  f32 fovy = 60.0f;                 ///< Vertical field of view, degrees.
  f32 near_plane = 0.05f;           ///< Nearest distance still drawn.
  f32 far_plane = 1000.0f;          ///< Farthest distance still drawn.
};

/// Starts 3D drawing through `camera`. Ends with end_3d().
///
/// Only call inside `phase_render`. Between the two, the `draw_*3d` functions
/// and draw_model() draw with depth test, over whatever was already drawn this
/// frame. The aspect ratio comes from screen_size(), so virtual size stays
/// correct. Nesting, or calling outside `phase_render`, is ignored, with a
/// warning.
///
/// camera_shake() also shakes this camera: `shake_config::max_offset` (screen
/// pixels) becomes a turn of the view, `shake_config::max_angle` a tilt. Other
/// shared effects apply to 3D as to 2D, with nothing extra needed: hitstop(),
/// screen_flash(), post_fx_set() and camera_set_post_shader().
/// @param ctx Engine context.
/// @param camera Camera for this draw.
void begin_3d(njin_ctx &ctx, const camera3d &camera);

/// Ends 3D drawing and returns to 2D drawing in world space.
///
/// Does nothing without a matching begin_3d(). If the game forgets to call it,
/// the engine closes it at the end of `phase_render`, with a warning.
/// @param ctx Engine context.
void end_3d(njin_ctx &ctx);

/// The 3D scene's shared lighting: the sun (directional light), ambient
/// light, the sun's shadow and fog. Point and spot lights are added with
/// light3d_add().
///
/// Each face is lit by the angle between its normal and the direction to the
/// light (Lambert), plus a Blinn-Phong specular highlight per
/// njin::material3d, plus `ambient` so a face in shadow is not fully black.
struct light3d {
  vec3 direction{-0.4f, -1.0f, -0.3f};  ///< Direction the sun shines from (need not have length 1).
  rgba color{1.0f, 1.0f, 1.0f, 1.0f};   ///< Colour and intensity of the sun. Black turns it off.
  rgba ambient{0.35f, 0.35f, 0.4f, 1.0f}; ///< Ambient light, hitting every face equally.
  /// The sun casts shadows. Every opaque shape (colour with `a` equal to 1)
  /// and model shadows the others, unless njin::material3d turns
  /// `cast_shadows` off.
  bool shadows = false;
  /// Shadows are computed in a box `2 * shadow_range` on a side, around where
  /// the camera looks. Smaller gives sharper shadows but nothing far away
  /// gets one.
  f32 shadow_range = 30.0f;
  i32 shadow_size = 2048;       ///< Side of the shadow map, pixels. Bigger is sharper, costs more memory.
  f32 shadow_softness = 1.0f;   ///< Softness of the shadow edge, in shadow map pixels. 0 is sharp.
  rgba fog_color{0.6f, 0.65f, 0.75f, 1.0f}; ///< Fog colour, usually the same as the clear colour.
  /// Fog density by distance to the camera: transparency left is
  /// `exp(-(density * d)^2)`. 0 is no fog.
  f32 fog_density = 0.0f;
};

/// Sets the shared lighting. Takes effect from the next begin_3d().
/// @param ctx Engine context.
/// @param light New lighting.
void light3d_set(njin_ctx &ctx, const light3d &light);

/// The shared lighting in use.
/// @param ctx Engine context.
/// @return The value set by light3d_set(), or njin::light3d's default.
light3d light3d_get(const njin_ctx &ctx);

/// Kind of light for njin::light3d_source.
enum light3d_kind {
  light3d_point, ///< Point light: spreads equally in every direction (a bulb, a torch).
  light3d_spot,  ///< Spotlight: shines along `direction` inside a cone (a flashlight, a stage light).
};

/// A point or spot light. Brightness falls to 0 at `radius`. Casts no shadow.
struct light3d_source {
  light3d_kind kind = light3d_point; ///< Kind of light.
  vec3 position{0.0f, 0.0f, 0.0f};   ///< Position.
  vec3 direction{0.0f, -1.0f, 0.0f}; ///< Direction it shines, spotlights only.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour.
  f32 intensity = 1.0f;              ///< Brightness, multiplied into the colour. Can be above 1.
  f32 radius = 10.0f;                ///< Reach, world units.
  f32 cone = 60.0f;                  ///< Cone opening angle, degrees (both sides), spotlights only.
  f32 softness = 0.25f;              ///< Share of the cone's edge that fades out, 0..1, spotlights only.
};

/// Maximum lights in one 3D draw. Lights added past this are ignored.
inline constexpr i32 light3d_max = 16;

/// Adds a light for the currently open 3D draw. Call every frame, between
/// begin_3d() and end_3d() (order relative to draw calls does not matter:
/// every light lights every shape of that draw).
/// @param ctx Engine context.
/// @param light Light.
void light3d_add(njin_ctx &ctx, const light3d_source &light);

/// The surface of 3D shapes drawn after material3d_set().
struct material3d {
  f32 specular = 0.25f;  ///< Strength of the specular highlight. 0 is a matte surface (wood, cloth).
  f32 shininess = 32.0f; ///< Sharpness of the specular highlight: higher is a smoother surface (plastic, metal).
  /// Self-lit colour, added after lighting; `a` is the strength. Turn on
  /// bloom (post_fx_set()) so this colour spreads a glow around it.
  rgba emission{0.0f, 0.0f, 0.0f, 0.0f};
  /// Rim light at the edge of the shape, where the surface turns away from
  /// the camera; `a` is the strength. Makes an object look soft and stand
  /// apart from the background (plastic, cartoon characters).
  rgba rim{0.0f, 0.0f, 0.0f, 0.0f};
  bool unlit = false;        ///< Ignores lighting, keeps the flat colour (tracers, laser beams, in-world UI).
  /// Image pasted onto a primitive (draw_cube3d(), draw_plane3d()...),
  /// multiplied by the draw colour. Invalid means no image. An image packed
  /// into an atlas is ignored.
  texture_handle texture{};
  bool cast_shadows = true;  ///< Casts a shadow when njin::light3d has `shadows` on.
};

/// Sets the surface for 3D shapes drawn after this call, until the next call
/// or end_3d(). `material3d_set(ctx, {})` resets to the default. Only the
/// engine's built-in shader uses this value.
/// @param ctx Engine context.
/// @param material Surface.
void material3d_set(njin_ctx &ctx, const material3d &material);

/// Draws a solid box, edges parallel to the axes.
///
/// Like every 3D draw function, only has an effect between begin_3d() and
/// end_3d(): the call is recorded and actually drawn at end_3d() (after
/// shadows are computed), in the order called. The shape is lit per
/// light3d_set(), light3d_add() and material3d_set(). If the game has bound
/// its own shader with shader_begin(), the shape draws with it; the engine
/// sets the `vec3` uniforms `lightDir`, `lightColor`, `ambient` and `viewPos`
/// (camera position) when the shader declares them. Attributes and the
/// `mvp`, `matModel`, `matNormal`, `colDiffuse` uniforms follow raylib's
/// standard names. Because the shape draws at end_3d(), a uniform the game
/// sets with `shader_set_*` keeps its last value before end_3d().
/// @param ctx Engine context.
/// @param center Box centre.
/// @param size Size along x, y, z.
/// @param color Colour.
void draw_cube3d(const njin_ctx &ctx, vec3 center, vec3 size, rgba color);

/// Draws a solid sphere.
/// @param ctx Engine context.
/// @param center Centre.
/// @param radius Radius.
/// @param color Colour.
void draw_sphere3d(const njin_ctx &ctx, vec3 center, f32 radius, rgba color);

/// Draws a horizontal plane (parallel to the xz plane), top face facing `+y`.
/// @param ctx Engine context.
/// @param center Centre.
/// @param size Size along x and z.
/// @param color Colour.
void draw_plane3d(const njin_ctx &ctx, vec3 center, vec2 size, rgba color);

/// Draws a solid cylinder joining two points (triangle mesh). For tracers,
/// ropes, poles.
/// @param ctx Engine context.
/// @param from Centre of the first base.
/// @param to Centre of the second base.
/// @param radius Radius.
/// @param color Colour.
void draw_cylinder3d(const njin_ctx &ctx, vec3 from, vec3 to, f32 radius, rgba color);

/// Draws a solid capsule with a triangle mesh: a cylinder joining two points,
/// with rounded ends. Joins any two points; when a smooth round edge is
/// needed up close, use draw_shape3d() with njin::shape3d_capsule.
/// @param ctx Engine context.
/// @param from Centre of the first hemisphere.
/// @param to Centre of the second hemisphere.
/// @param radius Radius.
/// @param color Colour.
void draw_capsule3d(const njin_ctx &ctx, vec3 from, vec3 to, f32 radius, rgba color);

/// Shape kind of njin::shape3d.
enum shape3d_kind {
  shape3d_sphere,   ///< Sphere: `radius`.
  shape3d_box,      ///< Box: `size`, edges rounded by `rounding`.
  shape3d_capsule,  ///< Capsule along the y axis: `radius`, `height` (both ends rounded).
  shape3d_cylinder, ///< Cylinder along the y axis: `radius`, `height`, edges rounded by `rounding`.
  shape3d_torus,    ///< Torus lying on the xz plane: `radius` (to the tube's centre), `thickness` (tube radius).
};

/// A primitive drawn with an SDF (signed distance function), used with
/// draw_shape3d().
///
/// Unlike draw_sphere3d() or draw_capsule3d() (a triangle mesh, faceted up
/// close), an SDF shape is computed per pixel, so its edge stays perfectly
/// round at any size, and it can be rounded. It still receives lighting,
/// shadows (both casting and receiving), njin::material3d and njin::fx3d
/// like any other shape. Costs more than a mesh shape, so use it for
/// characters and things that need to look good near the camera, not
/// thousands of bullets.
struct shape3d {
  shape3d_kind kind = shape3d_sphere; ///< Shape kind.
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Shape centre.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Rotation, degrees, same order as njin::transform3d.
  vec3 size{1.0f, 1.0f, 1.0f};        ///< Box size along x, y, z.
  f32 radius = 0.5f;                  ///< Radius (sphere, capsule, cylinder) or ring radius (torus).
  f32 height = 1.0f;                  ///< Overall height along y (capsule, cylinder).
  f32 thickness = 0.15f;              ///< Tube radius of a torus.
  f32 rounding = 0.0f;                ///< Edge rounding radius (box, cylinder). 0 is a sharp edge.
};

/// Draws a smooth SDF shape. Like the other shapes, only has an effect
/// between begin_3d() and end_3d(), following material3d_set() and
/// fx3d_set() at the time it is called. Always drawn with the engine's own
/// shader (a game shader bound with shader_begin() does not apply), and
/// takes no image.
///
/// @code
/// // A capsule-shaped character, plastic-looking, with a soft blue rim light.
/// njin::material3d_set(ctx, {.specular = 0.6f, .shininess = 60.0f, .rim = {0.55f, 0.75f, 1.0f, 0.45f}});
/// njin::draw_shape3d(ctx, {.kind = njin::shape3d_capsule, .position = pos, .radius = 0.28f, .height = 0.9f},
///                    njin::colors::blue);
/// njin::material3d_set(ctx, {});
/// @endcode
/// @param ctx Engine context.
/// @param shape Shape.
/// @param color Colour.
void draw_shape3d(const njin_ctx &ctx, const shape3d &shape, rgba color);

/// Loads a 3D model from a glTF (`.glb`, `.gltf`) or OBJ file.
///
/// The path resolves like texture_load(). The file's material colours and
/// textures are kept, and the model is lit per light3d_set().
/// @param ctx Engine context.
/// @param path File path.
/// @return Handle of the model, or an invalid handle if the file is missing
/// or broken.
model_handle model_load(njin_ctx &ctx, const char *path);

/// Frees a model along with its textures. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Model to free.
void model_unload(njin_ctx &ctx, model_handle handle);

/// Position, orientation and scale of a 3D object.
///
/// Applied in order: scaled by `scale`, rotated around `z` (roll), then
/// around `x` (pitch), then around `y` (yaw), and finally moved to
/// `position`. This is also how a first-person camera's orientation is built
/// from yaw and pitch.
struct transform3d {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Position.
  vec3 rotation{0.0f, 0.0f, 0.0f}; ///< Rotation around x, y, z, degrees.
  vec3 scale{1.0f, 1.0f, 1.0f};    ///< Scale along x, y, z.
};

/// The material of one part of a model: each glTF/OBJ file has one or more
/// materials, each mesh using one. Read with model_material_get(), changed
/// and set back with model_material_set().
///
/// A blank texture (invalid handle) means keeping the file's texture (if
/// any). A normal map needs no tangent in the file: the shader builds one
/// from screen derivatives.
struct model_material {
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Base colour, multiplied with the albedo image. Defaults to the file's colour.
  texture_handle albedo{};   ///< Colour image.
  texture_handle normal{};   ///< Normal map (tangent space, green pointing up, as in glTF).
  texture_handle emission{}; ///< Self-lit image, multiplied by `emission_color`.
  rgba emission_color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour multiplied into the emission image (the file's, or `emission`).
  /// This part's own shader, like shader_begin() for a primitive. Invalid
  /// means the engine's built-in shader.
  shader_handle shader{};
  material3d surface{}; ///< Shininess, emission, unlit, shadow casting. `surface.texture` is unused here.
};

/// Number of materials of a model.
/// @param ctx Engine context.
/// @param handle Model.
/// @return Number of materials, 0 if the handle is invalid.
i32 model_material_count(const njin_ctx &ctx, model_handle handle);

/// The `index`th material of a model.
/// @param ctx Engine context.
/// @param handle Model.
/// @param index 0..model_material_count() - 1.
/// @return The material, or the default if the handle or `index` is invalid.
model_material model_material_get(const njin_ctx &ctx, model_handle handle, i32 index);

/// Sets the `index`th material of a model, for every draw from then on.
///
/// @code
/// njin::model_material m = njin::model_material_get(ctx, crate, 0);
/// m.albedo = njin::texture_load(ctx, "assets/crate_wood.png");
/// m.normal = njin::texture_load(ctx, "assets/crate_wood_n.png");
/// m.surface.specular = 0.1f;
/// njin::model_material_set(ctx, crate, 0, m);
/// @endcode
/// @param ctx Engine context.
/// @param handle Model.
/// @param index 0..model_material_count() - 1, or -1 for every material.
/// @param material New material.
void model_material_set(njin_ctx &ctx, model_handle handle, i32 index, const model_material &material);

/// Draws a model at `transform`, with its own material (model_material_set()).
/// material3d_set() does not apply to a model; fx3d_set() does. A game
/// shader bound with shader_begin() replaces the shader of every part that
/// has no shader of its own.
/// @param ctx Engine context.
/// @param handle Model from model_load(). An invalid handle is ignored.
/// @param transform Position, orientation and scale.
/// @param tint Colour multiplied into the model's colour. White keeps it as is.
void draw_model(const njin_ctx &ctx, model_handle handle, const transform3d &transform,
                rgba tint = colors::white);

/// A ray in the 3D world, used to pick something under the mouse, fire a
/// shot, check line of sight.
struct ray3d {
  vec3 origin{0.0f, 0.0f, 0.0f};     ///< Origin.
  vec3 direction{0.0f, 0.0f, -1.0f}; ///< Direction, length 1.
};

/// Result when a ray hits something.
struct ray3d_hit {
  bool hit = false;                ///< Whether the ray hit.
  f32 distance = 0.0f;             ///< Distance from the ray's origin to the hit point.
  vec3 point{0.0f, 0.0f, 0.0f};    ///< Hit point.
  vec3 normal{0.0f, 0.0f, 0.0f};   ///< Surface normal at the hit point.
};

/// A ray from the camera through a point on the screen, to pick what is under
/// the mouse: `camera3d_ray(ctx, cam, mouse_pos(ctx))`. Uses the same aspect
/// ratio as begin_3d() (screen_size()), so it is correct with a virtual size
/// too.
/// @param ctx Engine context.
/// @param camera Camera already (or about to be) used for begin_3d().
/// @param screen Point on the screen, pixels (as mouse_pos() gives it).
/// @return A ray with its origin at the camera's position.
ray3d camera3d_ray(const njin_ctx &ctx, const camera3d &camera, vec2 screen);

/// Screen position of a 3D point, to place a label or a health bar over a
/// character's head.
/// @param ctx Engine context.
/// @param camera Camera of the draw.
/// @param point World point.
/// @param visible If not nullptr, set to `false` when the point is behind the
/// camera (the returned position is then meaningless).
/// @return Position, screen pixels (same system as mouse_pos()).
vec2 camera3d_to_screen(const njin_ctx &ctx, const camera3d &camera, vec3 point, bool *visible = nullptr);

/// A ray against a box with edges parallel to the axes. A ray whose origin is
/// inside the box hits the face it exits through.
/// @param ray Ray.
/// @param center Box centre.
/// @param size Size along x, y, z.
/// @return The nearest hit, if any.
ray3d_hit ray3d_box(const ray3d &ray, vec3 center, vec3 size);

/// A ray against a sphere.
/// @param ray Ray.
/// @param center Centre.
/// @param radius Radius.
/// @return The nearest hit, if any.
ray3d_hit ray3d_sphere(const ray3d &ray, vec3 center, f32 radius);

/// A ray against an infinite plane through `point`, normal `normal`. Hits
/// either side.
/// @param ray Ray.
/// @param point A point of the plane.
/// @param normal Normal (need not have length 1).
/// @return The hit, if the ray is not parallel and the plane is ahead.
ray3d_hit ray3d_plane(const ray3d &ray, vec3 point, vec3 normal);

/// A ray against an SDF shape, exactly as draw_shape3d() draws it (rotation
/// and rounding included).
/// @param ray Ray.
/// @param shape Shape.
/// @return The nearest hit, if any.
ray3d_hit ray3d_shape(const ray3d &ray, const shape3d &shape);

/// A ray against every triangle of a model placed at `transform`, as
/// draw_model() draws it. Costs with the number of triangles: for a handful
/// of models, not thousands.
/// @param ctx Engine context.
/// @param ray Ray.
/// @param model Model from model_load().
/// @param transform Position, orientation and scale.
/// @return The nearest hit, or no hit if the handle is invalid.
ray3d_hit ray3d_model(const njin_ctx &ctx, const ray3d &ray, model_handle model, const transform3d &transform);

/// Built-in mesh to draw many copies of at once with draw_instanced3d().
enum mesh3d_kind {
  mesh3d_cube,     ///< 1 x 1 x 1 box, centred on the origin.
  mesh3d_sphere,   ///< Radius-1 sphere, centred on the origin.
  mesh3d_plane,    ///< 1 x 1 plane on the xz plane, facing `+y`.
  mesh3d_cylinder, ///< Radius-1 cylinder, base at the origin, height 1 along `+y`.
};

/// Draws `count` copies of a built-in shape with **one** draw call, each
/// placed by its own data in an instance buffer (instance_buffer_create(),
/// instance_buffer_upload(), as with 2D's draw_instanced()). For a forest, a
/// crowd, debris, thousands of bricks.
///
/// With no `shader`, the engine's built-in shader reads each instance as
/// follows, so lighting, shadows, njin::material3d (its `texture` included)
/// and njin::fx3d at the time of the call still apply:
/// - `instance0`: position `xyz`, uniform scale `w` (0 is 1);
/// - `instance1` (from 8 floats per instance): colour `rgba`, multiplied with
///   the shape's colour;
/// - `instance2` (from 12 floats): rotation `xyz`, degrees, same order as
///   njin::transform3d;
/// - `instance3` (16 floats): scale along x, y, z (0 is 1), multiplied by
///   `instance0.w`.
///
/// With `shader`, that shader reads `instance0..3` its own way; the engine
/// sets `mvp` (camera, no model matrix) and the lighting uniforms as with
/// draw_cube3d(). Like any 3D shape, only has an effect between begin_3d()
/// and end_3d(), and draws at end_3d(): the buffer must keep its data until
/// then.
/// @param ctx Engine context.
/// @param mesh Shape.
/// @param buffer Buffer written with instance_buffer_upload().
/// @param first First instance.
/// @param count Number of instances, cut down if it exceeds what was written.
/// @param shader The game's shader, or invalid to use the built-in one.
void draw_instanced3d(const njin_ctx &ctx, mesh3d_kind mesh, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader = {});

/// Draws `count` copies of a model with one draw call per part of the model,
/// under the same instance data convention as the one above. Each part keeps
/// its own material's image and colour (model_material_set()); normal maps
/// and emission images are not used here.
/// @param ctx Engine context.
/// @param model Model from model_load().
/// @param buffer Buffer written with instance_buffer_upload().
/// @param first First instance.
/// @param count Number of instances, cut down if it exceeds what was written.
/// @param shader The game's shader, or invalid to use the built-in one.
void draw_instanced3d(const njin_ctx &ctx, model_handle model, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader = {});

/// Effect for 3D shapes drawn after fx3d_set(): a colour flash and dissolving
/// away, like njin::flash_fx and njin::dissolve_fx for sprites.
///
/// A 3D shape draws the instant it is called, not an entity, so the game
/// keeps its own time and sets the effect's level every frame. Only the
/// engine's built-in shader does this effect; a shape drawn with a game
/// shader (shader_begin()) is not affected.
struct fx3d {
  /// Colour painted over the shape, replacing each point's colour while
  /// keeping its shape (unlike `tint`, which only multiplies). `a` is the
  /// coverage: 0 is no flash, 1 is fully painted.
  rgba flash{1.0f, 1.0f, 1.0f, 0.0f};
  /// Dissolve level, 0..1. 0 is intact, 1 is fully gone. The shape drops
  /// patches according to each patch's fixed random value.
  f32 dissolve = 0.0f;
  rgba edge_color{1.0f, 0.55f, 0.1f, 1.0f}; ///< Colour of the burning edge where it is dissolving. `a` 0 removes the edge.
  f32 edge_width = 0.08f; ///< Edge thickness, on the 0..1 random scale.
  f32 grain = 0.1f;       ///< Size of each patch, world units.
  f32 seed = 0.0f;        ///< Changes the dissolve pattern, so two objects do not dissolve identically.
};

/// Sets the effect for 3D shapes drawn after this call, until the next call
/// or end_3d(). `fx3d_set(ctx, {})` turns it off. Only has an effect between
/// begin_3d() and end_3d().
///
/// @code
/// // A target that was just hit: flashes white for 0.1s, then dissolves over 0.4s.
/// njin::fx3d_set(ctx, {.flash = {1, 1, 1, 1 - t / 0.1f}, .dissolve = t / 0.4f});
/// njin::draw_sphere3d(ctx, pos, 0.6f, njin::colors::red);
/// njin::fx3d_set(ctx, {});
/// @endcode
/// @param ctx Engine context.
/// @param fx Effect.
void fx3d_set(njin_ctx &ctx, const fx3d &fx);

/// How to place a burst of particles in the 3D world, used with
/// particles3d_spawn().
struct particles3d_desc {
  /// Emission direction. An emitter with `spread` under 360 degrees emits
  /// inside a cone around this direction, half-angle `spread / 2`; the
  /// emitter's own `angle` is ignored.
  vec3 direction{0.0f, 1.0f, 0.0f};
  /// Converts the emitter's units (pixels, as the njin::fx presets use) to
  /// 3D world units: multiplied into speed, size, gravity and `area`.
  /// Gravity `{0, 400}` (falling down the screen) becomes falling along `-y`.
  f32 scale = 0.02f;
};

/// Spawns a burst of `count` particles at `pos`, reusing a 2D emitter: the
/// njin::fx presets (explosion(), sparks(), dust()...) or a self-configured
/// emitter.
///
/// Particles always face the camera, are simulated every frame per delta()
/// (so they stop during hitstop) and draw at end_3d(), after the other
/// shapes of that draw: a wall still hides particles, but particles do not
/// hide each other. Uses the emitter's circle, square or texture shape,
/// colour, size, gravity, drag, spin and blend mode. Bursts only: for
/// continuous emission (chimney smoke), call it every frame with a few
/// particles.
/// @param ctx Engine context.
/// @param emitter Particle configuration.
/// @param pos Spawn position.
/// @param count Number of particles.
/// @param desc Emission direction and the unit scale.
void particles3d_spawn(njin_ctx &ctx, const particle_emitter &emitter, vec3 pos, i32 count,
                       const particles3d_desc &desc = {});

/// Clears every flying 3D particle, e.g. on a scene change.
/// @param ctx Engine context.
void particles3d_clear(njin_ctx &ctx);

/// Number of live 3D particles.
/// @param ctx Engine context.
/// @return Particle count.
i32 particles3d_count(const njin_ctx &ctx);
/// @}
} // namespace njin
