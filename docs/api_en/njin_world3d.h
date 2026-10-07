#pragma once
#include "_math.h"
#include "njin_3d.h"
#include "njin_procgen.h"

namespace njin {
struct context;

/// @addtogroup grp_world3d
/// @{

// ---------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------

/// The most surface layers a terrain can have (grass, earth, rock, snow...).
inline constexpr i32 terrain3d_layer_max = 4;

/// One surface layer of a terrain: an image repeated over the ground, and the rule
/// for where it covers when njin::terrain3d_desc has `auto_splat` on.
///
/// The image is placed by world position (x, z), one tile every `tile` metres, and
/// blended with itself at another scale so the tiling does not show. On steep
/// ground (cliffs) the image is projected from three sides, so it does not stretch.
struct terrain3d_layer {
  texture_handle albedo{}; ///< Colour image. Invalid means `color` alone.
  texture_handle normal{}; ///< Normal map (green up, as in glTF). Invalid means none.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour multiplied into the image (or the plain colour with no image).
  f32 tile = 4.0f; ///< Side of one image tile, metres.
  /// @name Automatic cover rule (njin::terrain3d_desc::auto_splat)
  /// The layer covers where the height is in `[min_height, max_height]` and the
  /// slope in `[min_slope, max_slope]`, fading out over `blend` at the edges. A later
  /// layer covers an earlier one, so put the widest cover (grass) first.
  /// @{
  f32 min_height = -1.0e9f; ///< Lowest height, metres (as terrain3d_height() gives it).
  f32 max_height = 1.0e9f;  ///< Highest height, metres.
  f32 min_slope = 0.0f;     ///< Smallest slope, degrees (0 is flat).
  f32 max_slope = 90.0f;    ///< Largest slope, degrees.
  f32 blend = 3.0f;         ///< Width of the fade: metres for height, degrees for slope.
  /// @}
};

/// How terrain3d_create() builds a terrain.
///
/// A terrain is a square grid of `resolution` x `resolution` height samples, covering
/// a square of side `size` metres whose corner (smallest x, smallest z) is at
/// `origin`. Heights come from `heights`, or failing that from `heightmap`, or
/// failing that from `noise`.
///
/// The engine splits the grid into chunks of `chunk_quads` x `chunk_quads` squares,
/// skips chunks out of view, and draws far chunks with less detail (each time the
/// distance doubles past `lod_distance`, half as many squares per side). Each chunk's
/// edge has a "skirt" hanging down, so two chunks at different levels of detail never
/// show a gap.
struct terrain3d_desc {
  vec3 origin{0.0f, 0.0f, 0.0f}; ///< Corner of smallest x and smallest z; `y` is added to every height.
  f32 size = 256.0f;             ///< Side of the square, metres.
  /// Height samples per side (at least 3). Two samples are `size / (resolution - 1)` apart.
  i32 resolution = 257;
  /// Height of each sample, metres (plus `origin.y`), `resolution * resolution` values by
  /// row (row `z`, then column `x`). Copied. nullptr means the next source.
  const f32 *heights = nullptr;
  /// Height map: a grey image (red channel, 0 to 1) or a `.r16`/`.raw` file (16-bit,
  /// square, little-endian). Resampled to `resolution`. An 8-bit image has only 256
  /// steps: set `smooth` to smooth them out. nullptr means `noise`.
  const char *heightmap = nullptr;
  /// Noise that makes the heights when neither source above is given, in world metres
  /// (`frequency` is bumps per metre). The result is stretched so the lowest is 0 and
  /// the highest 1 before it is multiplied by `height_scale`.
  noise_desc noise{.seed = 1, .frequency = 0.004f, .fractal = fractal_fbm, .octaves = 5};
  f32 height_scale = 40.0f; ///< How many metres a value of 1 in the image or noise becomes.
  i32 smooth = 0;           ///< Smoothing passes over the heights (3 x 3 average) after reading.
  terrain3d_layer layers[terrain3d_layer_max]{}; ///< The surface layers.
  i32 layer_count = 1;      ///< How many layers are used, 1 to njin::terrain3d_layer_max.
  /// Work out which layer covers where from each layer's rule. Off, `splatmap` is used
  /// (or layer 0 alone), and more can be painted with terrain3d_paint().
  bool auto_splat = true;
  /// Layer weight image when `auto_splat` is off: red, green, blue and alpha are layers
  /// 0 to 3. Resampled to `resolution`. nullptr means layer 0 alone.
  const char *splatmap = nullptr;
  i32 chunk_quads = 32;      ///< Squares per side of a chunk: 8, 16, 32, 64 or 128.
  f32 lod_distance = 60.0f;  ///< Chunks nearer than this (metres) are drawn in full detail.
  i32 lod_levels = 4;        ///< Number of levels of detail, 1 to 6.
  f32 specular = 0.05f;      ///< Shininess of the ground (more when wet, see njin::weather3d).
  f32 shininess = 16.0f;     ///< Sharpness of the highlight.
  bool cast_shadows = true;  ///< Casts shadows (hills shade the sun). Always receives them.
  bool collision = true;     ///< Makes a collision body (a Jolt height field) for characters, vehicles and bodies.
  f32 friction = 0.8f;       ///< Friction of the collision body.
  u64 user = 0;              ///< body3d_user() of the collision body.
};

/// Creates a terrain. Can be called in any phase (no begin_3d() needed).
///
/// The layers' images get mipmaps (texture_set_filter() with njin::filter_mipmap) so
/// distant ground does not shimmer.
/// @param ctx The engine context.
/// @param desc How to build it.
/// @return Handle, or an invalid handle if `desc` is wrong (a warning says why).
terrain3d_handle terrain3d_create(context &ctx, const terrain3d_desc &desc);

/// Destroys a terrain and its collision body. Grass and objects scattered on it are
/// no longer drawn.
/// @param ctx The engine context.
/// @param handle Terrain. An invalid handle is ignored.
void terrain3d_destroy(context &ctx, terrain3d_handle handle);

/// Draws the terrain in the open 3D pass (between begin_3d() and end_3d()). It takes
/// light, shadows, lamps and fog like every 3D shape.
/// @param ctx The engine context.
/// @param handle Terrain.
void draw_terrain3d(const context &ctx, terrain3d_handle handle);

/// Ground height at `(x, z)`, exactly as the grid is drawn at full detail and as the
/// collision body has it (the same split of each square into triangles). Outside the
/// terrain the nearest edge is used.
/// @param ctx The engine context.
/// @param handle Terrain.
/// @param x x coordinate, metres.
/// @param z z coordinate, metres.
/// @return Height, metres; 0 if the handle is invalid.
f32 terrain3d_height(const context &ctx, terrain3d_handle handle, f32 x, f32 z);

/// Ground normal at `(x, z)` (length 1, pointing up).
/// @param ctx The engine context.
/// @param handle Terrain.
/// @param x x coordinate, metres.
/// @param z z coordinate, metres.
/// @return Normal; `{0, 1, 0}` if the handle is invalid.
vec3 terrain3d_normal(const context &ctx, terrain3d_handle handle, f32 x, f32 z);

/// Whether `(x, z)` lies on the terrain.
/// @param ctx The engine context.
/// @param handle Terrain.
/// @param x x coordinate, metres.
/// @param z z coordinate, metres.
/// @return `true` if inside its square.
bool terrain3d_contains(const context &ctx, terrain3d_handle handle, f32 x, f32 z);

/// Weight of layer `layer` at `(x, z)`, 0 to 1 (the layers add up to 1). To know what
/// a character stands on (grass or rock footsteps).
/// @param ctx The engine context.
/// @param handle Terrain.
/// @param x x coordinate, metres.
/// @param z z coordinate, metres.
/// @param layer 0..njin::terrain3d_layer_max - 1.
/// @return Weight; 0 if the handle or `layer` is invalid.
f32 terrain3d_layer_weight(const context &ctx, terrain3d_handle handle, f32 x, f32 z, i32 layer);

/// The terrain's (static) collision body, to recognise it in raycasts and contacts.
/// @param ctx The engine context.
/// @param handle Terrain.
/// @return Body, or an invalid handle if `collision` is off.
body3d_handle terrain3d_body(const context &ctx, terrain3d_handle handle);

/// Kind of edit of njin::terrain3d_brush.
enum terrain3d_brush_kind {
  terrain3d_raise,   ///< Raises by `strength` metres at the centre.
  terrain3d_lower,   ///< Lowers by `strength` metres at the centre.
  terrain3d_flatten, ///< Pulls towards height `height`, a share `strength` (0..1) each time.
  terrain3d_smooth,  ///< Smooths, a share `strength` (0..1) each time.
};

/// One round stroke of terrain editing, strongest at the centre and fading out to the edge.
struct terrain3d_brush {
  terrain3d_brush_kind kind = terrain3d_raise; ///< Kind of edit.
  vec3 center{0.0f, 0.0f, 0.0f}; ///< Centre (`x` and `z` are used).
  f32 radius = 4.0f;             ///< Radius, metres.
  f32 strength = 0.5f;           ///< Metres (raise, lower) or a share 0..1 (flatten, smooth).
  f32 height = 0.0f;             ///< Target height of `terrain3d_flatten`, metres.
  /// The outer part of the radius that fades out, 0 (hard) to 1 (fading from the centre).
  f32 falloff = 0.6f;
};

/// Edits the terrain's height with one stroke: the drawn grid, the collision body, the
/// automatic layers, and the grass and scattered objects there all follow at once
/// (scattered objects keep their place, only their height changes).
/// @param ctx The engine context.
/// @param handle Terrain.
/// @param brush The stroke.
void terrain3d_edit(context &ctx, terrain3d_handle handle, const terrain3d_brush &brush);

/// Paints layer `layer` onto the terrain round `center` (the other layers fade). The
/// paint is kept when the height is edited, even with `auto_splat` on.
/// @param ctx The engine context.
/// @param handle Terrain.
/// @param center Centre (`x` and `z` are used).
/// @param radius Radius, metres.
/// @param layer Layer.
/// @param strength How much is added at the centre each time, 0..1.
void terrain3d_paint(context &ctx, terrain3d_handle handle, vec3 center, f32 radius, i32 layer, f32 strength);

// ---------------------------------------------------------------------------
// Grass and scattered objects
// ---------------------------------------------------------------------------

/// How a field of grass grows on a terrain (grass3d_create()).
///
/// Each blade is a few triangles; tens of thousands are drawn with instancing. Grass
/// is only made for the area round the camera (within `draw_distance`), thinner and
/// lower further out until it vanishes, so however wide the terrain, only the near
/// part costs anything. Grass sways in the wind (wind3d_set(), or the wind of
/// njin::weather3d when the sky is drawn with draw_sky3d()).
struct grass3d_desc {
  terrain3d_handle terrain{}; ///< Terrain the grass grows on.
  f32 density = 6.0f;         ///< Blades per square metre where it grows thickest.
  f32 height = 0.45f;         ///< Blade height, metres.
  f32 height_jitter = 0.4f;   ///< Blades differ in height by up to this share of `height`.
  f32 width = 0.05f;          ///< Width of a blade at its root, metres.
  rgba base_color{0.12f, 0.30f, 0.07f, 1.0f}; ///< Colour at the root.
  rgba tip_color{0.52f, 0.70f, 0.28f, 1.0f};  ///< Colour at the tip.
  f32 color_jitter = 0.15f;   ///< Blades differ in brightness by up to this share.
  /// Terrain layer the grass grows on: the number of blades is multiplied by that
  /// layer's weight (terrain3d_layer_weight()). -1 grows everywhere.
  i32 layer = 0;
  f32 max_slope = 35.0f;      ///< Does not grow on steeper ground, degrees.
  /// Noise splitting the grass into patches (in metres), used when `patchiness` > 0.
  noise_desc patches{.seed = 3, .frequency = 0.06f, .octaves = 2};
  f32 patchiness = 0.4f;      ///< 0 grows evenly; 1 in clear patches with bare ground between.
  f32 draw_distance = 60.0f;  ///< No grass further than this, metres.
  f32 fade = 20.0f;           ///< Grass shrinks over this distance before `draw_distance`, metres.
  f32 sway = 1.0f;            ///< How much it sways in the wind. 0 stands still.
  u32 seed = 1;               ///< Seed: the same seed grows the same grass.
  bool cast_shadows = false;  ///< Blades cast shadows (costly: every blade is drawn into the shadow map too). Always receives them.
};

/// Creates a field of grass. It grows again where the terrain is edited (terrain3d_edit()).
/// @param ctx The engine context.
/// @param desc How it grows.
/// @return Handle, or an invalid handle if the terrain is invalid.
grass3d_handle grass3d_create(context &ctx, const grass3d_desc &desc);

/// Destroys a field of grass.
/// @param ctx The engine context.
/// @param handle Field of grass. An invalid handle is ignored.
void grass3d_destroy(context &ctx, grass3d_handle handle);

/// Draws the grass round the camera of the open 3D pass.
/// @param ctx The engine context.
/// @param handle Field of grass.
void draw_grass3d(const context &ctx, grass3d_handle handle);

/// Blades drawn in the last 3D pass, for measuring.
/// @param ctx The engine context.
/// @param handle Field of grass.
/// @return Number of blades.
i32 grass3d_drawn(const context &ctx, grass3d_handle handle);

/// Sets the wind for grass, clouds and rain: a velocity on the xz plane, metres per
/// second. draw_sky3d() sets it again from `weather3d::wind` every frame.
/// @param ctx The engine context.
/// @param wind Wind velocity `(x, z)`.
void wind3d_set(context &ctx, vec2 wind);

/// The wind in use.
/// @param ctx The engine context.
/// @return Wind velocity `(x, z)`, metres per second.
vec2 wind3d_get(const context &ctx);

/// How to scatter one kind of object (rocks, trees, bushes) over a terrain
/// (scatter3d_create()).
///
/// The places are picked once at creation, from the seed: far enough apart
/// (`spacing`), within the allowed height, slope and layer. The objects are drawn with
/// instancing area by area, skipping areas out of view, using `far_model` for areas
/// further than `lod_distance` and drawing nothing beyond `draw_distance`.
struct scatter3d_desc {
  terrain3d_handle terrain{}; ///< Terrain.
  model_handle model{};       ///< Model drawn near the camera.
  model_handle far_model{};   ///< Simpler model for far away. Invalid means `model`.
  f32 density = 0.02f;        ///< Objects per square metre where thickest.
  f32 spacing = 2.0f;         ///< Two objects are at least this many metres apart.
  f32 min_height = -1.0e9f;   ///< Only placed from this height up, metres.
  f32 max_height = 1.0e9f;    ///< Only placed below this height, metres.
  f32 min_slope = 0.0f;       ///< Smallest slope, degrees.
  f32 max_slope = 30.0f;      ///< Largest slope, degrees.
  i32 layer = -1;             ///< Only placed on this layer (weight from `layer_min`); -1 for any layer.
  f32 layer_min = 0.5f;       ///< Minimum weight of `layer`.
  noise_desc patches{.seed = 5, .frequency = 0.02f, .octaves = 2}; ///< Noise grouping them into clusters, when `patchiness` > 0.
  f32 patchiness = 0.0f;      ///< 0 scatters evenly; 1 in clusters (woods, rock fields).
  f32 scale_min = 0.8f;       ///< Smallest scale.
  f32 scale_max = 1.2f;       ///< Largest scale.
  f32 align = 0.0f;           ///< 0 stands upright (trees); 1 leans with the ground (rocks).
  f32 sink = 0.0f;            ///< Sinks this many metres into the ground (a rock's foot does not float on a slope).
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour multiplied into the model.
  f32 color_jitter = 0.1f;    ///< Objects differ in brightness by up to this share.
  f32 lod_distance = 80.0f;   ///< Further away `far_model` is used, metres.
  f32 draw_distance = 300.0f; ///< Further away nothing is drawn, metres.
  u32 seed = 1;               ///< Seed.
};

/// Scatters objects over a terrain.
/// @param ctx The engine context.
/// @param desc How to scatter them.
/// @return Handle, or an invalid handle if the terrain or model is invalid.
scatter3d_handle scatter3d_create(context &ctx, const scatter3d_desc &desc);

/// Destroys a layer of scattered objects.
/// @param ctx The engine context.
/// @param handle Layer of scattered objects. An invalid handle is ignored.
void scatter3d_destroy(context &ctx, scatter3d_handle handle);

/// Draws the scattered objects in view of the open 3D pass (with draw_instanced3d()).
/// @param ctx The engine context.
/// @param handle Layer of scattered objects.
void draw_scatter3d(const context &ctx, scatter3d_handle handle);

/// Number of objects scattered.
/// @param ctx The engine context.
/// @param handle Layer of scattered objects.
/// @return Number of objects.
i32 scatter3d_count(const context &ctx, scatter3d_handle handle);

/// Position, rotation and scale of the scattered objects, so the game can add
/// collision (body3d_create()) for trees or large rocks.
/// @param ctx The engine context.
/// @param handle Layer of scattered objects.
/// @param out Array receiving them, or nullptr to only count.
/// @param count Number of elements of `out`.
/// @return Number of objects (may be more than `count`: only the first `count` are written).
i32 scatter3d_transforms(const context &ctx, scatter3d_handle handle, transform3d *out, i32 count);

// ---------------------------------------------------------------------------
// Water
// ---------------------------------------------------------------------------

/// The most waves a water surface can have.
inline constexpr i32 water3d_wave_max = 8;

/// One Gerstner wave: sharp crests, flat troughs, the water moving in circles as in
/// real waves. Its speed follows its wavelength as in deep water (long waves are faster).
struct water3d_wave {
  vec2 direction{1.0f, 0.0f}; ///< Direction the wave travels on the xz plane (any length).
  f32 wavelength = 12.0f;     ///< Distance between two crests, metres.
  /// Steepness, 0..1: the amplitude is `steepness * wavelength / (2 pi)`. The waves'
  /// steepnesses should add up to less than 1, or the crests fold over.
  f32 steepness = 0.2f;
  f32 speed = 1.0f;           ///< Multiplies the wave's natural speed.
};

/// How to build a water surface (water3d_create()).
///
/// A nonzero `size` is a rectangular lake centred on `center`. A zero `size` is a sea
/// reaching to the horizon: the grid follows the camera, dense near it and sparse far away.
///
/// The colour follows the depth (from `shallow_color` to `deep_color`), clear where it is
/// shallow, with foam along the shore and on wave crests. The surface reflects the sky
/// (that of draw_sky3d() if drawn in the pass, otherwise from the fog colour and ambient
/// light), glitters in the sun and receives shadows. Depth and shore come from
/// `terrain`: with no terrain the water counts as evenly deep with no shore.
struct water3d_desc {
  f32 level = 0.0f;                ///< Height of the still water, metres.
  vec2 center{0.0f, 0.0f};         ///< Centre of the lake on the xz plane.
  vec2 size{0.0f, 0.0f};           ///< Size of the lake along x and z, metres; `{0, 0}` for a sea.
  /// The waves. By default three gentle ones for a lake in the wind.
  water3d_wave waves[water3d_wave_max]{{{1.0f, 0.3f}, 9.0f, 0.18f, 1.0f},
                                       {{0.6f, -0.8f}, 5.5f, 0.14f, 1.0f},
                                       {{-0.4f, 1.0f}, 3.1f, 0.10f, 1.0f}};
  i32 wave_count = 3;              ///< Number of waves used, 0..njin::water3d_wave_max.
  rgba shallow_color{0.10f, 0.42f, 0.45f, 1.0f}; ///< Colour of shallow water.
  rgba deep_color{0.02f, 0.09f, 0.16f, 1.0f};    ///< Colour of deep water.
  f32 depth_fade = 6.0f;           ///< At this depth, metres, the water is nearly `deep_color`.
  f32 clarity = 1.5f;              ///< Shallower than this, metres, you see through to the bottom.
  rgba foam_color{0.95f, 0.97f, 1.0f, 1.0f};     ///< Foam colour.
  f32 foam_width = 0.5f;           ///< Foam along the shore within this depth, metres. 0 is none.
  f32 crest_foam = 0.35f;          ///< Foam on high wave crests, 0..1.
  f32 ripples = 0.35f;             ///< Small ripples on the waves, 0..1.
  f32 specular = 1.0f;             ///< Strength of the sun's glitter.
  f32 shininess = 300.0f;          ///< Sharpness of the sun's glitter.
  terrain3d_handle terrain{};      ///< Terrain making the bottom and the shore.
  /// Grid density, times the default. Higher gives smoother waves far away, at a cost.
  f32 detail = 1.0f;
};

/// Creates a water surface.
/// @param ctx The engine context.
/// @param desc How to build it.
/// @return Handle.
water3d_handle water3d_create(context &ctx, const water3d_desc &desc);

/// Destroys a water surface. Bodies floating on it stop floating.
/// @param ctx The engine context.
/// @param handle Water surface. An invalid handle is ignored.
void water3d_destroy(context &ctx, water3d_handle handle);

/// Changes how a water surface is built (bigger waves in a storm, a rising level).
/// @param ctx The engine context.
/// @param handle Water surface.
/// @param desc The new description.
void water3d_set(context &ctx, water3d_handle handle, const water3d_desc &desc);

/// The current description.
/// @param ctx The engine context.
/// @param handle Water surface.
/// @return Description, or the default if the handle is invalid.
water3d_desc water3d_get(const context &ctx, water3d_handle handle);

/// Draws the water surface in the open 3D pass. Water is drawn after every opaque
/// shape, like glass.
/// @param ctx The engine context.
/// @param handle Water surface.
void draw_water3d(const context &ctx, water3d_handle handle);

/// Height of the water at `(x, z)` right now, from exactly the shader's wave formula.
/// Outside the lake it is `level`.
/// @param ctx The engine context.
/// @param handle Water surface.
/// @param x x coordinate, metres.
/// @param z z coordinate, metres.
/// @return Height, metres; 0 if the handle is invalid.
f32 water3d_height(const context &ctx, water3d_handle handle, f32 x, f32 z);

/// Normal of the water at `(x, z)` right now.
/// @param ctx The engine context.
/// @param handle Water surface.
/// @param x x coordinate, metres.
/// @param z z coordinate, metres.
/// @return Normal (length 1).
vec3 water3d_normal(const context &ctx, water3d_handle handle, f32 x, f32 z);

/// The waves' clock, seconds: it runs on delta(), so it stops while the game is paused.
/// @param ctx The engine context.
/// @return Time of the waves.
f32 water3d_time(const context &ctx);

/// How a body floats on water (water3d_float()).
struct buoyancy3d {
  /// Lift: 1 hovers, more than 1 floats (wood, boats), less than 1 slowly sinks.
  f32 buoyancy = 1.4f;
  f32 linear_drag = 0.6f;  ///< The water drags against movement.
  f32 angular_drag = 0.15f; ///< The water drags against turning.
  vec3 flow{0.0f, 0.0f, 0.0f}; ///< A current carrying the body along, metres per second.
};

/// Makes a dynamic body float on the water: each physics step the part of its volume
/// below the wave surface (at the body's centre) pushes it up, and the water holds it
/// back, so a wooden crate bobs on the waves. Call again with the same body to change
/// `buoyancy`.
/// @param ctx The engine context.
/// @param water Water surface.
/// @param body Dynamic body.
/// @param buoyancy Lift and drag.
void water3d_float(context &ctx, water3d_handle water, body3d_handle body, const buoyancy3d &buoyancy = {});

/// Stops a body floating on the water.
/// @param ctx The engine context.
/// @param water Water surface.
/// @param body Body.
void water3d_unfloat(context &ctx, water3d_handle water, body3d_handle body);

// ---------------------------------------------------------------------------
// Sky and weather
// ---------------------------------------------------------------------------

/// Weather: clouds, fog, rain, snow, wind, wet ground. Goes in njin::sky3d.
struct weather3d {
  f32 clouds = 0.3f;         ///< Share of the sky with clouds, 0..1. 1 is overcast, hiding the sun.
  f32 cloud_darkness = 0.0f; ///< Dark clouds (rain clouds), 0..1.
  f32 fog = 0.0f;            ///< Fog density, as `light3d::fog_density`. 0 is no fog.
  f32 rain = 0.0f;           ///< Rain, 0..1.
  f32 snow = 0.0f;           ///< Falling snow, 0..1.
  vec2 wind{2.0f, 0.6f};     ///< Wind on the xz plane, metres per second: clouds drift, grass leans, rain slants.
  f32 wetness = 0.0f;        ///< Wet ground (darker and shinier), 0..1.
};

/// Ready-made kinds of weather for weather3d_preset().
enum weather3d_kind {
  weather3d_clear,    ///< Clear sky, few clouds.
  weather3d_overcast, ///< Overcast, no harsh sun.
  weather3d_rain,     ///< Rain, wet ground.
  weather3d_snow,     ///< Falling snow.
  weather3d_fog,      ///< Thick fog.
};

/// A ready-made kind of weather. A plain value, change it freely before use.
/// @param kind Kind.
/// @return Weather.
weather3d weather3d_preset(weather3d_kind kind);

/// Interpolates between two weathers, so the sky turns to rain gradually.
/// @param a Weather at `t = 0`.
/// @param b Weather at `t = 1`.
/// @param t Progress, 0..1.
/// @return The weather in between.
weather3d weather3d_lerp(const weather3d &a, const weather3d &b, f32 t);

/// A sky by time of day, for draw_sky3d().
///
/// The sun rises in the east (`+x`), is highest at 12 o'clock in the south (`+z`) and
/// sets in the west, by latitude and season. At night there is the moon (a pale blue
/// light opposite the sun) and stars.
struct sky3d {
  f32 hour = 10.0f;         ///< Hour of the day, 0..24 (fractions allowed: 6.5 is half past six).
  f32 latitude = 35.0f;     ///< Latitude, degrees: further from the equator the sun is lower.
  /// Season, -1 (midwinter) to 1 (midsummer): long or short days, high or low sun.
  f32 season = 0.0f;
  f32 north = 0.0f;         ///< Turns the whole sky about the y axis, degrees (the map's north).
  weather3d weather{};      ///< Weather.
  f32 cloud_height = 900.0f; ///< Height of the cloud layer, metres.
  f32 cloud_scale = 1.0f;   ///< Size of the clouds; larger makes bigger clouds.
  bool stars = true;        ///< Stars at night.
  f32 sun_size = 1.0f;      ///< Size of the sun in the sky, times its real size.
  /// draw_sky3d() sets the pass's light (light3d_set()) from the sky: the direction of
  /// the sun (or moon), the sunlight's colour, the ambient light, the fog colour and
  /// density; the current light's shadow settings are kept. Off, the game calls
  /// sky3d_light() itself.
  bool drive_light = true;
};

/// Direction from the scene to the sun (length 1). A negative `y` means the sun has set.
/// @param sky Sky.
/// @return Direction to the sun.
vec3 sky3d_sun_direction(const sky3d &sky);

/// Light from the sky: `base` with the direction and colour of the sunlight (the moon at
/// night), ambient light from the sky's colour, the fog colour from the horizon, and the
/// weather's fog (the thicker of `base` and the weather). Shadows are kept as in `base`.
/// @param sky Sky.
/// @param base The base light (shadow settings).
/// @return Light.
light3d sky3d_light(const sky3d &sky, const light3d &base = {});

/// Colour of the sky looking along `direction` (no clouds or stars), as draw_sky3d()
/// draws it. For a 2D background, the clear colour, or knowing whether it is light or dark.
/// @param sky Sky.
/// @param direction Direction of view.
/// @return Colour (alpha 1).
rgba sky3d_color(const sky3d &sky, vec3 direction);

/// Draws the sky (background, sun, clouds, stars) behind everything in the open 3D pass,
/// and rain or snow round the camera by the weather. Sets the wind (wind3d_set()) and how
/// wet the ground is; with `sky3d::drive_light` also the pass's light.
///
/// Call it anywhere between begin_3d() and end_3d(): the sky only covers what nothing
/// was drawn over.
/// @param ctx The engine context.
/// @param sky Sky.
void draw_sky3d(context &ctx, const sky3d &sky);
/// @}
} // namespace njin
