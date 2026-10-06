#pragma once
#include "_math.h"
#include "njin_particles.h"

namespace njin {
struct context;

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
  /// Also draw the 3D entities (njin::model3d, njin::shape3d_render, njin::light3d_source
  /// lights) in this draw. Turn it off for a secondary scene (e.g. a model spinning in a
  /// menu) that draws only what the game calls.
  bool entities = true;
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
void begin_3d(context &ctx, const camera3d &camera);

/// Like begin_3d() but draws this 3D pass into the render texture `target`
/// instead of the screen: a second eye in the same frame (a character's camera,
/// a rear-view mirror). Clears `target` with `clear` first; the aspect ratio comes
/// from its size. Also only called in `phase_render` and closed with end_3d();
/// several in one frame are fine, each a full pass (shadows included if
/// light3d_set() turns them on), so each costs as much as drawing the scene again.
///
/// camera_shake() does not shake this camera, gizmos are not drawn into it, and its
/// depth is not used by `post_fx::dof`. Lights (light3d_add()) must be added again
/// for this pass. The image in the render texture is stored upside down, like
/// every render texture.
/// @param ctx Engine context.
/// @param camera Camera for this draw.
/// @param target Render texture that receives the image. An invalid handle is
/// ignored, with a warning.
/// @param clear Colour `target` is cleared with before drawing.
void begin_3d(context &ctx, const camera3d &camera, render_texture_handle target, rgba clear);

/// Ends 3D drawing and returns to 2D drawing in world space.
///
/// Does nothing without a matching begin_3d(). If the game forgets to call it,
/// the engine closes it at the end of `phase_render`, with a warning.
/// @param ctx Engine context.
void end_3d(context &ctx);

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
  /// Shadow map side for point and spot lights with `light3d_source::shadows`, pixels, per
  /// face (a point light has 6 faces, a spotlight 1).
  i32 source_shadow_size = 512;
  rgba fog_color{0.6f, 0.65f, 0.75f, 1.0f}; ///< Fog colour, usually the same as the clear colour.
  /// Fog density by distance to the camera: transparency left is
  /// `exp(-(density * d)^2)`. 0 is no fog.
  f32 fog_density = 0.0f;
};

/// Sets the shared lighting. Takes effect from the next begin_3d().
/// @param ctx Engine context.
/// @param light New lighting.
void light3d_set(context &ctx, const light3d &light);

/// The shared lighting in use.
/// @param ctx Engine context.
/// @return The value set by light3d_set(), or njin::light3d's default.
light3d light3d_get(const context &ctx);

/// Kind of light for njin::light3d_source.
enum light3d_kind {
  light3d_point, ///< Point light: spreads equally in every direction (a bulb, a torch).
  light3d_spot,  ///< Spotlight: shines along `direction` inside a cone (a flashlight, a stage light).
};

/// A point or spot light. Brightness falls to 0 at `radius`.
///
/// Add it to one draw with light3d_add(), or use it as a component: an entity with
/// njin::light3d_source and njin::transform3d lights every 3D draw (with
/// `camera3d::entities`), placed at `transform3d::position` (`position` is ignored).
struct light3d_source {
  light3d_kind kind = light3d_point; ///< Kind of light.
  vec3 position{0.0f, 0.0f, 0.0f};   ///< Position.
  vec3 direction{0.0f, -1.0f, 0.0f}; ///< Direction it shines, spotlights only.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour.
  f32 intensity = 1.0f;              ///< Brightness, multiplied into the colour. Can be above 1.
  f32 radius = 10.0f;                ///< Reach, world units.
  f32 cone = 60.0f;                  ///< Cone opening angle, degrees (both sides), spotlights only.
  f32 softness = 0.25f;              ///< Share of the cone's edge that fades out, 0..1, spotlights only.
  /// Casts shadows, like the sun's `light3d::shadows`, within `radius`. Each draw has at
  /// most njin::light3d_shadow_max shadow-casting lights (later ones light without casting
  /// shadows). Costly: every shadow caster is drawn 6 more times for a point light and once
  /// for a spotlight, so turn it on only for a few important lights (a flashlight, a lamp
  /// hanging in the middle of a room).
  bool shadows = false;
};

/// Maximum lights in one 3D draw. Lights added past this are ignored.
inline constexpr i32 light3d_max = 16;

/// Maximum point and spot lights that cast shadows in one 3D draw.
inline constexpr i32 light3d_shadow_max = 4;

/// Adds a light for the currently open 3D draw. Call every frame, between
/// begin_3d() and end_3d() (order relative to draw calls does not matter:
/// every light lights every shape of that draw).
/// @param ctx Engine context.
/// @param light Light.
void light3d_add(context &ctx, const light3d_source &light);

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
  /// Opt-in hand-shaped clay normal/albedo variation for SDF draws only.
  /// 0 keeps the original smooth surface. Does not change hit depth or silhouette.
  f32 clay = 0.0f;
  f32 clay_detail = 9.0f; ///< Grain frequency relative to the closest SDF part radius.
  /// Normal map (tangent space, green up as in glTF, that is OpenGL +Y) for
  /// shapes and draw_instanced3d(). Invalid means none. For a model, set
  /// `model_material::normal`.
  texture_handle normal{};
  /// Above 0: the images (`texture`, `normal`, and a model material's images)
  /// are mapped by world position instead of the mesh's UVs, one image tile
  /// every `world_uv` 3D units. Upright faces: u runs across the face, v up
  /// (the image's top up), so water streaks run straight down however the
  /// shape is turned; flat faces: u along x, v along z. For walls and floors
  /// put together from many pieces: the image runs on across them, is not
  /// stretched by their size, and works on meshes without UVs. Default 0: the
  /// mesh's UVs. An image mapped this way is blended from two offsets picked
  /// by a slow noise, so its tiles do not show as a grid.
  f32 world_uv = 0.0f;
  /// The layer underneath, only with `world_uv` above 0: colour image and
  /// normal map of what lies under the main image (brick under render),
  /// showing in patches where the top layer has worn off, more of them at the
  /// foot of a wall, with a slightly dark rim at their edge. Not multiplied by
  /// the draw colour. The under layer only shifts in steps of 1/8 of a tile,
  /// so a grid pattern such as brick (repeating every 1/8 tile) keeps its
  /// joints. Invalid means no under layer.
  texture_handle under{};
  texture_handle under_normal{}; ///< Normal map of the under layer, as `normal`.
  /// How much of the under layer shows, 0 (none) to 1 (all of it, the top layer gone).
  f32 under_amount = 0.0f;
};

/// Sets the surface for 3D shapes drawn after this call, until the next call
/// or end_3d(). `material3d_set(ctx, {})` resets to the default. Only the
/// engine's built-in shader uses this value.
/// @param ctx Engine context.
/// @param material Surface.
void material3d_set(context &ctx, const material3d &material);

/// Draws a solid box, edges parallel to the axes.
///
/// Like every 3D draw function, only has an effect between begin_3d() and
/// end_3d(): the call is recorded and actually drawn at end_3d() (after
/// shadows are computed), in the order called. The shape is lit per
/// light3d_set(), light3d_add() and material3d_set(). If the game has bound
/// its own shader with shader_begin(), the shape draws with it; the engine
/// sets the `vec3` uniforms `lightDir`, `lightColor`, `ambient`, `viewPos`
/// (camera position), `fogColor`, the `float` uniform `fogDensity` (the fog
/// as light3d_set()) and the `int` uniform `instanceFloats` (0 when not drawn
/// instanced) when the shader declares them, and binds the extra images set
/// with shader_set_texture(). Attributes and the `mvp`, `matModel`,
/// `matNormal`, `colDiffuse` uniforms follow raylib's standard names. Because
/// the shape draws at end_3d(), a uniform the game sets with `shader_set_*`
/// keeps its last value before end_3d().
/// @param ctx Engine context.
/// @param center Box centre.
/// @param size Size along x, y, z.
/// @param color Colour.
void draw_cube3d(const context &ctx, vec3 center, vec3 size, rgba color);

/// Draws a solid sphere.
/// @param ctx Engine context.
/// @param center Centre.
/// @param radius Radius.
/// @param color Colour.
void draw_sphere3d(const context &ctx, vec3 center, f32 radius, rgba color);

/// Draws a horizontal plane (parallel to the xz plane), top face facing `+y`.
/// @param ctx Engine context.
/// @param center Centre.
/// @param size Size along x and z.
/// @param color Colour.
void draw_plane3d(const context &ctx, vec3 center, vec2 size, rgba color);

/// Draws a solid cylinder joining two points (triangle mesh). For tracers,
/// ropes, poles.
/// @param ctx Engine context.
/// @param from Centre of the first base.
/// @param to Centre of the second base.
/// @param radius Radius.
/// @param color Colour.
void draw_cylinder3d(const context &ctx, vec3 from, vec3 to, f32 radius, rgba color);

/// Draws a solid capsule with a triangle mesh: a cylinder joining two points,
/// with rounded ends. Joins any two points; when a smooth round edge is
/// needed up close, use draw_shape3d() with njin::shape3d_capsule.
/// @param ctx Engine context.
/// @param from Centre of the first hemisphere.
/// @param to Centre of the second hemisphere.
/// @param radius Radius.
/// @param color Colour.
void draw_capsule3d(const context &ctx, vec3 from, vec3 to, f32 radius, rgba color);

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
void draw_shape3d(const context &ctx, const shape3d &shape, rgba color);

/// One part of a blended SDF shape (draw_sdf_blend()): a rounded cone joining
/// `a` to `b`, radius `ra` at `a` and `rb` at `b` (equal radii make a capsule,
/// `a` on `b` a sphere).
struct sdf_part {
  vec3 a{0.0f, 0.0f, 0.0f}; ///< Centre of the first end.
  vec3 b{0.0f, 0.0f, 0.0f}; ///< Centre of the second end.
  f32 ra = 0.1f;            ///< Radius at `a`.
  f32 rb = 0.1f;            ///< Radius at `b`.
  /// How softly this part joins the parts before it in the list, world units.
  /// Negative (the default) uses draw_sdf_blend()'s `blend`. Lets a joint be
  /// filled in where wanted (a shoulder into the body) and stay slim where not
  /// (an elbow, a knee: keep it small, since smooth min adds about `blend / 4`
  /// at the joint).
  f32 blend = -1.0f;
};

/// Most parts in one blended SDF shape.
inline constexpr u32 sdf_blend_max = 32;

/// Draws several SDF parts melted into one solid (smooth min): where two parts
/// meet the joint is filled in smoothly instead of creased, like modelling
/// clay. A character built from a head, neck, body and limb segments, its
/// parts placed again each frame by its pose, stays seamless as it moves.
///
/// Like draw_shape3d(): worked out per pixel so its outline is smooth at any
/// size, lit, casting and receiving shadows, per material3d_set() and
/// fx3d_set() at the call. Each blended shape is one draw call; it costs by
/// the pixels it covers times its number of parts.
///
/// @code
/// // An arm: upper arm, forearm and hand joined.
/// const njin::sdf_part arm[] = {{.a = shoulder, .b = elbow, .ra = 0.05f, .rb = 0.04f},
///                               {.a = elbow, .b = wrist, .ra = 0.04f, .rb = 0.03f},
///                               {.a = wrist, .b = fingers, .ra = 0.035f, .rb = 0.02f}};
/// njin::draw_sdf_blend(ctx, arm, 3, 0.03f, clay);
/// @endcode
/// @param ctx Engine context.
/// @param parts The parts; copied, they need not live past the call.
/// @param count Number of parts, 1 to njin::sdf_blend_max (the rest are
/// dropped, with a warning).
/// @param blend How soft the joints are, world units, for the parts that set
/// no `sdf_part::blend` of their own: 0 is a hard join, more makes one part
/// flow further into the next.
/// @param color Colour.
void draw_sdf_blend(const context &ctx, const sdf_part *parts, u32 count, f32 blend, rgba color);

/// Loads a 3D model from a glTF (`.glb`, `.gltf`) or OBJ file.
///
/// The path resolves like texture_load(). The file's material colours and
/// textures are kept, and the model is lit per light3d_set().
/// @param ctx Engine context.
/// @param path File path.
/// @return Handle of the model, or an invalid handle if the file is missing
/// or broken.
model_handle model_load(context &ctx, const char *path);

/// How model_load() loads part of a glTF file: leaving out or keeping only
/// some nodes, and merging the meshes that share a material into one.
///
/// Node names match as substrings: `"substrate"` matches the node
/// `PBK_substrate.001`. Nodes without a mesh are not affected. The filter is
/// ignored for OBJ files.
struct model_load_desc {
  const char *path = nullptr;               ///< File path, as for model_load().
  const char *const *skip_nodes = nullptr;  ///< Leave out the meshes of nodes whose name contains one of these.
  u32 skip_count = 0;                       ///< Number of strings in `skip_nodes`.
  const char *const *only_nodes = nullptr;  ///< If set: keep only the meshes of nodes whose name contains one of these.
  u32 only_count = 0;                       ///< Number of strings in `only_nodes`.
  /// Merge the meshes that use the same material into one mesh (at most 65535
  /// vertices each), so draw_instanced3d() issues fewer draws. Merged meshes
  /// drop their bone data: the model is drawn in the file's rest pose, as a
  /// still object.
  bool merge = false;
};

/// Loads a model like model_load(), with the node filter and mesh merging of `desc`.
///
/// glTF materials with `KHR_materials_transmission` (clear glass) are drawn
/// see-through: opacity from `transmissionFactor`, no shadow, drawn after
/// every opaque part.
///
/// @code
/// // A window frame without its piece of wall, merged by material.
/// const char *skip[] = {"substrate", "floor_band"};
/// const njin::model_handle frame =
///     njin::model_load(ctx, {.path = "assets/window.glb", .skip_nodes = skip, .skip_count = 2, .merge = true});
/// @endcode
/// @param ctx Engine context.
/// @param desc The file, the node filter and whether to merge meshes.
/// @return Handle of the model, or an invalid handle if the file is missing,
/// broken, or the filter keeps no mesh.
model_handle model_load(context &ctx, const model_load_desc &desc);

/// Frees a model along with its textures. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Model to free.
void model_unload(context &ctx, model_handle handle);

/// A triangle mesh the game builds itself, for model_create(): terrain grown
/// from a seed, shapes put together at run time. The pointers need only live
/// until model_create() returns.
struct mesh3d_data {
  const vec3 *positions = nullptr; ///< Positions of the `vertex_count` vertices.
  /// Per-vertex normals (unit length). nullptr has the engine work them out:
  /// each vertex takes the average of the triangles sharing it, so curved
  /// surfaces look smooth.
  const vec3 *normals = nullptr;
  const rgba *colors = nullptr; ///< Per-vertex colours, multiplied by the material colour. nullptr is white.
  /// Per-vertex texture coordinates (UV), for the material's images
  /// (model_material_set()): a piece cut from a mesh that has UVs keeps its
  /// images. nullptr is (0, 0) on every vertex, colour only.
  const vec2 *texcoords = nullptr;
  u32 vertex_count = 0;         ///< Number of vertices.
  /// Three indices per triangle, counter-clockwise seen from the front.
  /// nullptr makes every three consecutive vertices a triangle. With indices,
  /// at most 65535 vertices (raylib's 16-bit indices): split a big mesh into
  /// several models.
  const u32 *indices = nullptr;
  u32 index_count = 0; ///< Number of indices, a multiple of 3.
};

/// Makes a model from a triangle mesh in memory, then used like a model loaded
/// from a file: draw_model(), draw_instanced3d(), model_material_set(),
/// ray3d_model(), model_unload(). Lit, casting and receiving shadows like any
/// other shape.
///
/// @code
/// // A red triangle lying on the ground.
/// const njin::vec3 p[] = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};
/// const njin::rgba c[] = {njin::colors::red, njin::colors::red, njin::colors::red};
/// const njin::model_handle tri = njin::model_create(ctx, {.positions = p, .colors = c, .vertex_count = 3});
/// @endcode
/// @param ctx Engine context.
/// @param mesh The mesh.
/// @return Handle of the model, or an invalid handle if the mesh is empty or
/// wrong (a warning says why).
model_handle model_create(context &ctx, const mesh3d_data &mesh);

/// A skinned mesh the game assembles at run time, for model_create_skinned():
/// a character put together from several parts (head, body, arms, hair...) by
/// a set of genes, without exporting a file for every combination. The
/// pointers only need to live until model_create_skinned() returns.
struct skinned_mesh3d_data {
  const vec3 *positions = nullptr; ///< Positions of the `vertex_count` vertices, in the rest pose of `skeleton`.
  /// Per-vertex normals (length 1). nullptr makes the engine compute them as model_create() does.
  const vec3 *normals = nullptr;
  /// Per-vertex texture coordinates (UV), for the material's images
  /// (model_material_set()): a character with its own texture still shares the
  /// clip library. nullptr is (0, 0) on every vertex, colour only.
  const vec2 *texcoords = nullptr;
  u32 vertex_count = 0; ///< Number of vertices.
  /// Four bones per vertex: bone indices of `skeleton` (model_bone_find()), so
  /// at most 255 bones.
  const u8 *joints = nullptr;
  const f32 *weights = nullptr; ///< Four weights per vertex, in the order of `joints`, summing to 1.
  /// Three indices per triangle, counter-clockwise seen from the front.
  const u32 *indices = nullptr;
  u32 index_count = 0; ///< Number of indices, a multiple of 3.
  /// Splits the triangles into parts, one material each (material `k` is part
  /// `k`, white): the end index of each part in `indices`, ascending, the last
  /// ending at `index_count`. nullptr is one part. At most 65535 vertices per
  /// part.
  const u32 *part_ends = nullptr;
  u32 part_count = 0; ///< Number of parts in `part_ends`.
  /// A skinned model loaded with model_load(): the new model copies its
  /// skeleton and rest pose and **shares** its animations (no copy), so
  /// model_anim_find(), draw_model_anim() and model_bone_pose() on the new
  /// model play this one's clips. Freeing this model first leaves the new one
  /// with its rest pose only.
  model_handle skeleton{};
};

/// Makes a skinned model from a mesh in memory, using another model's
/// skeleton and animations (a clip library loaded once for every character).
/// Drawn with draw_model_anim() like a model loaded from a file, each draw
/// with its own pose and (with njin::model_recolor) its own palette, so many
/// characters share one mesh with a motion and an outfit each.
///
/// @code
/// // The clip library once; one model per combination of parts.
/// const njin::model_handle clips = njin::model_load(ctx, "assets/shared_animations.glb");
/// const njin::model_handle body = njin::model_create_skinned(
///     ctx, {.positions = pos.data(), .vertex_count = n, .joints = joints.data(), .weights = weights.data(),
///           .indices = idx.data(), .index_count = (njin::u32)idx.size(), .skeleton = clips});
/// njin::draw_model_anim(ctx, body, at, {.anim = njin::model_anim_find(ctx, body, "Walk_Loop"), .time = t});
/// @endcode
/// @param ctx The engine context.
/// @param mesh The mesh, its parts and the model for the skeleton.
/// @return The model's handle, or an invalid handle if the mesh is wrong or
/// `skeleton` is not a skinned model (a warning says why). Free it with model_unload().
model_handle model_create_skinned(context &ctx, const skinned_mesh3d_data &mesh);

/// How model_lod_build() makes the levels of detail (LOD) of a model.
struct model_lod_desc {
  i32 levels = 3;        ///< Simplified levels, 1..4. Level k keeps about `ratio` to the power k of the triangles.
  f32 ratio = 0.5f;      ///< Share of the triangles each level keeps against the original, raised per level, 0.1..0.9.
  /// How far the shape may move, relative to the mesh's size (0.05 is 5%).
  /// Simplifying stops there even short of `ratio`, so a model with few
  /// triangles may get fewer levels.
  f32 max_error = 0.05f;
  /// Level 1 is drawn when the model (its bounding sphere) is less tall than
  /// this share of the screen's height; each next level at half the previous.
  f32 screen = 0.25f;
};

/// Makes levels of detail for a model: simplified copies with fewer
/// triangles, drawn in its place when it is small on screen. From then on
/// draw_model(), draw_model_anim() and njin::model3d pick the level by the
/// distance to the camera; a model with bones keeps its bones and animations
/// at every level. draw_instanced3d() and ray3d_model() always use the
/// original model.
///
/// Do it once after loading (a few milliseconds for a few tens of thousands
/// of triangles); calling it again replaces the old levels. model_unload()
/// frees the levels too.
///
/// @code
/// const njin::model_handle tree = njin::model_load(ctx, "tree.glb");
/// njin::model_lod_build(ctx, tree, {.levels = 2});
/// @endcode
/// @param ctx Engine context.
/// @param handle Model from model_load() or model_create().
/// @param desc How many levels and how simplified.
/// @return Levels made, 0 if the model is too simple to simplify or the handle is invalid.
i32 model_lod_build(context &ctx, model_handle handle, const model_lod_desc &desc = {});

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
  /// This part's own shader, like shader_begin() for a primitive, also when
  /// the model is drawn with draw_instanced3d() (the shader reads
  /// `instanceFloats` to know which way it is drawn). Invalid means the
  /// engine's built-in shader.
  shader_handle shader{};
  /// Shininess, emission, unlit, shadow casting, `world_uv`. `surface.texture`
  /// and `surface.normal` are unused here (`albedo`, `normal` are).
  material3d surface{};
  /// Draw both faces of every triangle (no back-face culling), for leaves,
  /// paper, single-layer cloth. Defaults to the glTF material's `doubleSided`.
  bool double_sided = false;
};

/// Number of materials of a model.
/// @param ctx Engine context.
/// @param handle Model.
/// @return Number of materials, 0 if the handle is invalid.
i32 model_material_count(const context &ctx, model_handle handle);

/// Finds a material by the name it has in the glTF file.
///
/// Matches by prefix: `"Human_Shirt"` matches the material `Human_Shirt_53_19_16`.
/// @param ctx The engine context.
/// @param handle The model.
/// @param name The name or its beginning.
/// @return The index of the first material that matches, or -1 if none does (or the file is not glTF).
i32 model_material_find(const context &ctx, model_handle handle, const char *name);

/// The `index`th material of a model.
/// @param ctx Engine context.
/// @param handle Model.
/// @param index 0..model_material_count() - 1.
/// @return The material, or the default if the handle or `index` is invalid.
model_material model_material_get(const context &ctx, model_handle handle, i32 index);

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
void model_material_set(context &ctx, model_handle handle, i32 index, const model_material &material);

/// Draws a model at `transform`, with its own material (model_material_set()).
/// material3d_set() does not apply to a model; fx3d_set() does. A game
/// shader bound with shader_begin() replaces the shader of every part that
/// has no shader of its own.
///
/// A model outside the camera's view (its bounding box outside the frustum)
/// is not drawn but still casts its shadow into the scene; render_info_get()
/// counts the models left out. A big mesh (terrain, roads) is best split into
/// several models by area, so the parts out of view are left out. With levels
/// of detail (model_lod_build()) a far model is drawn at a simpler level.
/// @param ctx Engine context.
/// @param handle Model from model_load(). An invalid handle is ignored.
/// @param transform Position, orientation and scale.
/// @param tint Colour multiplied into the model's colour. White keeps it as is.
void draw_model(const context &ctx, model_handle handle, const transform3d &transform,
                rgba tint = colors::white);

/// Number of skeletal animations in the model's file (glTF with a skin). model_load()
/// loads them together with the model.
/// @param ctx Engine context.
/// @param handle Model.
/// @return Animation count, 0 if the model has none or the handle is invalid.
i32 model_anim_count(const context &ctx, model_handle handle);

/// Finds an animation by the name given in Blender (the action name when exporting glTF).
/// @param ctx Engine context.
/// @param handle Model.
/// @param name Animation name.
/// @return Index 0..model_anim_count() - 1, or -1 if there is none.
i32 model_anim_find(const context &ctx, model_handle handle, const char *name);

/// Name of animation `index`.
/// @param ctx Engine context.
/// @param handle Model.
/// @param index 0..model_anim_count() - 1.
/// @return Name, or an empty string if the handle or `index` is invalid.
const char *model_anim_name(const context &ctx, model_handle handle, i32 index);

/// Length of animation `index`, seconds.
/// @param ctx Engine context.
/// @param handle Model.
/// @param index 0..model_anim_count() - 1.
/// @return Length, 0 if the handle or `index` is invalid.
f32 model_anim_duration(const context &ctx, model_handle handle, i32 index);

struct bone_pose3d;

/// Pose of a skinned model when drawn: which animation, at which second, and
/// (optionally) blended with a second animation to move smoothly between two motions.
/// Or the game places each bone itself with `bones` (ragdoll3d_bones()).
struct model_pose {
  i32 anim = -1;           ///< Animation (model_anim_find()). -1 is the file's rest pose.
  f32 time = 0.0f;         ///< Time in the animation, seconds.
  bool loop = true;        ///< Loop; otherwise it stops on the last frame.
  i32 blend_anim = -1;     ///< Second animation to blend in. -1 is no blending.
  f32 blend_time = 0.0f;   ///< Time in the second animation, seconds.
  bool blend_loop = true;  ///< Loop the second animation.
  f32 blend = 0.0f;        ///< Blend weight: 0 is only `anim`, 1 is only `blend_anim`.
  /// A pose set by the game: model_bone_count() bones, in model space as
  /// model_bone_pose() returns them (ragdoll3d_bones() fills this array). When not
  /// nullptr the animation fields above are ignored. The engine reads the array
  /// right when drawing (draw_model_anim(), or when an entity with njin::model3d
  /// is drawn) and does not keep it.
  const bone_pose3d *bones = nullptr;
};

/// Draws a skinned model in pose `pose`. Like draw_model(), and the shadow follows
/// the pose. Each draw has its own pose, so the same model drawn many times (a crowd
/// of monsters) can have a different motion for each one.
///
/// Bones are computed on the GPU, at most 128 bones, 4 bones per vertex (the glTF
/// limit). Parts drawn with a game shader (`model_material::shader` or
/// shader_begin()), draw_instanced3d() and ray3d_model() use the rest pose.
///
/// @code
/// // Run while moving, stand when stopped, blending over 0.2 seconds.
/// blend = move_toward(blend, moving ? 1.0f : 0.0f, dt / 0.2f);
/// njin::draw_model_anim(ctx, hero, {.position = pos, .rotation = {0, yaw, 0}},
///                       {.anim = idle, .time = t, .blend_anim = run, .blend_time = t, .blend = blend});
/// @endcode
/// @param ctx Engine context.
/// @param handle Model from model_load(). An invalid handle is ignored.
/// @param transform Position, orientation and scale.
/// @param pose Pose. A model without bones draws as with draw_model().
/// @param tint Colour multiplied into the model's colour.
void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint = colors::white);

/// A colour in place of one material's own, for one draw only.
struct model_recolor {
  i32 material = -1;                 ///< The material (model_material_find()). -1 changes nothing.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Replaces njin::model_material::color; the material keeps its opacity.
};

/// Like draw_model_anim() above, and material `recolor.material` has colour
/// `recolor.color` in this draw.
///
/// For many draws of one model, each in its own shirt colour (each side's
/// uniform), without loading the model several times or changing
/// model_material_set() between draws: the draws only run at end_3d(), so a
/// model_material_set() between them does not apply to each one on its own.
/// @code
/// const njin::i32 shirt = njin::model_material_find(ctx, man, "Shirt");
/// njin::draw_model_anim(ctx, man, at, pose, njin::colors::white, {.material = shirt, .color = team_red});
/// @endcode
/// @param ctx The engine context.
/// @param handle A model from model_load().
/// @param transform Position, orientation and scale.
/// @param pose The pose.
/// @param tint A colour multiplied into the whole model (after the material's recolour).
/// @param recolor The material recoloured and its new colour.
void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint, const model_recolor &recolor);

/// Like draw_model_anim() above, with several recoloured materials at once: a
/// character's own palette (skin, hair, shirt, trousers) on a shared model.
/// @code
/// const njin::model_recolor palette[] = {{.material = 0, .color = skin}, {.material = 1, .color = shirt}};
/// njin::draw_model_anim(ctx, body, at, pose, njin::colors::white, palette, 2);
/// @endcode
/// @param ctx The engine context.
/// @param handle The model.
/// @param transform Position, orientation and scale.
/// @param pose The pose.
/// @param tint Colour multiplied into the whole model (after the recolouring).
/// @param recolors The recoloured materials; a material listed twice takes the first colour.
/// @param count Number of elements in `recolors`.
void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint, const model_recolor *recolors, u32 count);

/// The model's number of bones (0 without bones or for an invalid handle).
/// @param ctx The engine context.
/// @param handle The model.
/// @return The number of bones.
i32 model_bone_count(const context &ctx, model_handle handle);

/// A bone's name, as in the glTF file.
/// @param ctx The engine context.
/// @param handle The model.
/// @param bone 0..model_bone_count() - 1.
/// @return The name, or an empty string if `bone` or the handle is invalid.
const char *model_bone_name(const context &ctx, model_handle handle, i32 bone);

/// Finds a bone by name (the whole string must match).
/// @param ctx The engine context.
/// @param handle The model.
/// @param name The bone's name.
/// @return The index 0..model_bone_count() - 1, or -1 if there is none.
i32 model_bone_find(const context &ctx, model_handle handle, const char *name);

/// The parent of `bone` in the skeleton, to walk up a chain of bones (IK: thigh,
/// calf, foot) or tell which bones hang below which.
/// @param ctx The engine context.
/// @param handle The model.
/// @param bone 0..model_bone_count() - 1.
/// @return The parent bone's index, or -1 for a root bone or an invalid handle or `bone`.
i32 model_bone_parent(const context &ctx, model_handle handle, i32 bone);

/// The box around the skin that bone `bone` pulls hardest (with every bone below
/// it if `children`), in the file's rest pose: along the model's axes, from the
/// bone's origin. Along the model's axes the box is tight (a sole lies flat on
/// the floor), not swollen like a box built along a bone that lies at a slant.
/// To fit a collider to the real shape (a foot, a hand, a head): when placing
/// it, turn the box by however much the bone has turned from the rest pose
/// (model_bone_pose() with the rest pose).
///
/// @code
/// // The foot (without the toes), around the origin of bone foot_l.
/// njin::vec3 lo, hi;
/// if (njin::model_bone_bounds(ctx, man, foot_l, false, &lo, &hi)) { ... }
/// @endcode
/// @param ctx The engine context.
/// @param handle A model with skin (a mesh with bones).
/// @param bone 0..model_bone_count() - 1.
/// @param children Counts the skin of every bone below `bone` too.
/// @param min Receives the box's smallest corner.
/// @param max Receives the largest corner.
/// @return `false` (min and max untouched) if no skin vertex qualifies, the model
/// has no skin, or the handle or `bone` is invalid.
bool model_bone_bounds(const context &ctx, model_handle handle, i32 bone, bool children, vec3 *min, vec3 *max);

/// The vertices of that skin (as model_bone_bounds()), in the same axes: along the
/// model's axes in the rest pose, from the bone's origin. To build a convex hull
/// that fits the real shape (physics3d_hull_create()).
/// @param ctx The engine context.
/// @param handle A model with skin.
/// @param bone 0..model_bone_count() - 1.
/// @param children Counts the skin of every bone below `bone` too.
/// @param out Array receiving the vertices, or nullptr to only count them.
/// @param count Number of elements in `out`.
/// @return How many vertices there are (may be more than `count`: only the first
/// `count` are written).
i32 model_bone_points(const context &ctx, model_handle handle, i32 bone, bool children, vec3 *out, i32 count);

/// A bone in a pose: its position and three axes in the model's space (before
/// the draw's transform), as draw_model_anim() places it.
struct bone_pose3d {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< The bone's origin.
  vec3 x_axis{1.0f, 0.0f, 0.0f};   ///< The bone's x axis, length 1.
  vec3 y_axis{0.0f, 1.0f, 0.0f};   ///< The y axis (along the bone with Blender's rigs), length 1.
  vec3 z_axis{0.0f, 0.0f, 1.0f};   ///< The z axis, length 1.
};

/// Bone `bone` in pose `pose` (worked out as draw_model_anim() does, blending
/// two animations too), to put an object in a hand, hit zones on bones, or
/// know where a foot is. Multiply by the draw's transform for the world.
///
/// @code
/// // A cigarette in the right hand, in the world.
/// const njin::bone_pose3d hand = njin::model_bone_pose(ctx, man, pose, hand_r);
/// const njin::vec3 at = pos + hand.position * scale; // the model is not turned
/// @endcode
/// @param ctx The engine context.
/// @param handle A skinned model.
/// @param pose The pose; `anim` -1 (and no blend) is the rest pose.
/// @param bone 0..model_bone_count() - 1.
/// @return The bone's pose; the default value (the origin, the standard axes)
/// if the handle or `bone` is invalid.
bone_pose3d model_bone_pose(const context &ctx, model_handle handle, const model_pose &pose, i32 bone);

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
ray3d camera3d_ray(const context &ctx, const camera3d &camera, vec2 screen);

/// Screen position of a 3D point, to place a label or a health bar over a
/// character's head.
/// @param ctx Engine context.
/// @param camera Camera of the draw.
/// @param point World point.
/// @param visible If not nullptr, set to `false` when the point is behind the
/// camera (the returned position is then meaningless).
/// @return Position, screen pixels (same system as mouse_pos()).
vec2 camera3d_to_screen(const context &ctx, const camera3d &camera, vec3 point, bool *visible = nullptr);

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
ray3d_hit ray3d_model(const context &ctx, const ray3d &ray, model_handle model, const transform3d &transform);

/// Built-in mesh to draw many copies of at once with draw_instanced3d().
enum mesh3d_kind {
  mesh3d_cube,     ///< 1 x 1 x 1 box, centred on the origin.
  mesh3d_sphere,   ///< Radius-1 sphere, centred on the origin.
  mesh3d_plane,    ///< 1 x 1 plane on the xz plane, facing `+y`.
  mesh3d_cylinder, ///< Radius-1 cylinder, base at the origin, height 1 along `+y`.
  /// Like `mesh3d_sphere` with few faces (about 100 triangles, against about
  /// 3000 for the smooth one): for thousands of small things on screen (the
  /// heads of a crowd of soldiers, smoke particles), where the smooth one only
  /// makes the GPU draw edges nobody sees.
  mesh3d_sphere_low,
  /// Like `mesh3d_cylinder` with 8 sides instead of 48, for the same reason.
  mesh3d_cylinder_low,
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
void draw_instanced3d(const context &ctx, mesh3d_kind mesh, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader = {});

/// Draws `count` copies of a model with one draw call per part of the model,
/// under the same instance data convention as the one above. Each part keeps
/// its own material's image and colour (model_material_set()). A normal map is
/// used only when the game set `model_material::normal` (a file's own normal
/// map is ignored here); emission images are not used. A part with a
/// `model_material::shader` draws with that shader instead of `shader`, as in
/// draw_model().
/// @param ctx Engine context.
/// @param model Model from model_load().
/// @param buffer Buffer written with instance_buffer_upload().
/// @param first First instance.
/// @param count Number of instances, cut down if it exceeds what was written.
/// @param shader The game's shader, or invalid to use the built-in one.
void draw_instanced3d(const context &ctx, model_handle model, instance_buffer_handle buffer, u32 first, u32 count,
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
  /// Cuts away what is in the way (sphere masking), for a third-person
  /// camera: the part of the shape between the camera and `mask_center`
  /// (3D world units), inside a cone widening to `mask_radius` at
  /// `mask_center`, is dropped in a dither pattern with a soft edge, so a
  /// character behind a wall stays visible. The ground underfoot (more than
  /// 0.6 `mask_radius` below `mask_center`) stays. Shadows do not change. Set it
  /// while drawing what can block the view (buildings, trees), turn it off
  /// before drawing the characters. `mask_radius` 0 is off.
  vec3 mask_center{0.0f, 0.0f, 0.0f};
  f32 mask_radius = 0.0f; ///< Radius of the cut at `mask_center`, 3D world units.
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
void fx3d_set(context &ctx, const fx3d &fx);

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
void particles3d_spawn(context &ctx, const particle_emitter &emitter, vec3 pos, i32 count,
                       const particles3d_desc &desc = {});

/// Clears every flying 3D particle, e.g. on a scene change.
/// @param ctx Engine context.
void particles3d_clear(context &ctx);

/// Number of live 3D particles.
/// @param ctx Engine context.
/// @return Particle count.
i32 particles3d_count(const context &ctx);

/// Component: a model drawn at the entity's njin::transform3d, in every 3D draw
/// with `camera3d::entities` (the engine adds it at end_3d(), along with the game's draw calls).
///
/// The engine advances `pose.time` and `pose.blend_time` every frame (by delta(), times
/// `speed`), so the game only has to pick the animation:
///
/// @code
/// const auto e = reg.create();
/// reg.emplace<njin::transform3d>(e, njin::transform3d{.position = {0, 0, -4}});
/// reg.emplace<njin::model3d>(e, njin::model3d{.model = robot, .pose = {.anim = walk}});
/// @endcode
struct model3d {
  model_handle model{};          ///< Model from model_load().
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour multiplied into the model's colour.
  model_pose pose{};             ///< Pose, as for draw_model_anim().
  f32 speed = 1.0f;              ///< Animation playback speed. 0 is stopped.
  fx3d fx{};                     ///< Flash and dissolve, as for fx3d_set().
  bool visible = true;           ///< Hide without removing the component.
};

/// Component: an SDF shape (as for draw_shape3d()) drawn at the entity's
/// njin::transform3d, in every 3D draw with `camera3d::entities`. Position and rotation
/// come from the transform (`shape.position` and `shape.rotation` are ignored), so an
/// entity that also has njin::body3d is drawn where physics puts it.
struct shape3d_render {
  shape3d shape{};                    ///< Shape kind and size.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour.
  material3d material{};              ///< Surface, as for material3d_set().
  fx3d fx{};                          ///< Flash and dissolve, as for fx3d_set().
  bool visible = true;                ///< Hide without removing the component.
};
/// @}
} // namespace njin
