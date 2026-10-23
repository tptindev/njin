#pragma once
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_quality
/// @{

/// Ready-made quality levels, see graphics_quality_preset().
enum quality_level {
  quality_low,    ///< For weak hardware: old integrated GPUs, machines with no real GPU.
  quality_medium, ///< For newer integrated GPUs, office laptops.
  quality_high,   ///< No limits at all: the game draws exactly what it set.
};

/// Graphics quality ceiling, applied over everything the game sets.
///
/// The game still sets its effects as usual (light3d_set(), post3d_set(),
/// post_fx_set(), lighting_set(), particle emitters, model LOD...). This
/// struct only limits what actually gets drawn: turning an effect off,
/// lowering a resolution, cutting particles. The `_get` functions still
/// return exactly what the game set, so the game's settings menu does not
/// need to know anything about it, and going back to `quality_high` returns
/// everything to how it was.
///
/// The default value of every field is "no limit": a game that never calls
/// graphics_quality_set() draws exactly as before.
struct graphics_quality {
  /// The world (`phase_render`) is drawn at this fraction of the screen's
  /// resolution on each axis, then scaled up smoothly; the UI
  /// (`phase_post_render`) and text still draw at the real resolution.
  /// 0.25..1, 1 is off. 0.5 leaves only a quarter of the pixels for the GPU
  /// to shade. Does not apply when there is a virtual resolution
  /// (window_set_virtual_size()): pixel art is already drawn into a small
  /// image.
  f32 world_scale = 1.0f;
  /// Shadows of the 3D sun (`light3d::shadows`). `false` turns them off.
  bool shadows = true;
  /// Largest edge of the sun's shadow map, in pixels (`light3d::shadow_size`).
  /// 0 is no limit.
  i32 shadow_size_max = 0;
  /// Shadows of 3D point and spot lights (`light3d_source::shadows`). A
  /// point light redraws the scene six times for its shadow, so this is the
  /// most expensive thing on weak hardware.
  bool lamp_shadows = true;
  /// Largest edge of each lamp shadow cell, in pixels
  /// (`light3d::source_shadow_size`). 0 is no limit.
  i32 lamp_shadow_size_max = 0;
  bool ssao = true;          ///< post3d's SSAO. `false` turns it off.
  i32 ssao_samples_max = 0;  ///< Maximum SSAO samples per pixel. 0 is no limit.
  bool ssr = true;           ///< post3d's screen-space reflections.
  i32 ssr_steps_max = 0;     ///< Maximum SSR trace steps. 0 is no limit.
  bool motion_blur = true;   ///< post3d's motion blur.
  bool taa = true;           ///< post3d's TAA.
  bool sun_shafts = true;    ///< post3d's light shafts and lens flare.
  bool bloom = true;         ///< post_fx's bloom.
  bool dof = true;           ///< post_fx's depth of field.
  /// Largest resolution of the 2D light map (`lighting_desc::scale`),
  /// 0.25..1. 1 is no limit.
  f32 lighting_scale_max = 1.0f;
  /// Multiplier on particle counts: spawn rate, particles per burst and the
  /// maximum particles of each emitter, 2D and 3D. 0..1, 1 keeps it unchanged.
  f32 particles = 1.0f;
  /// Multiplier on a model's LOD switch distance (`model_lod_desc::screen`):
  /// 2 switches a model to a lower-triangle version at half the distance. 1
  /// keeps it unchanged. Only has an effect on a model with LOD built
  /// (model_lod_build()).
  f32 lod_scale = 1.0f;
  /// Multiplier on grass draw distance (`grass3d_desc::draw_distance`),
  /// 0.1..1. 1 keeps it unchanged.
  f32 view_distance = 1.0f;
};

/// Returns the ready-made limit set for a level.
///
/// `quality_high` is the default struct (no limits at all). `quality_medium`
/// turns off SSR, TAA, motion blur and lamp shadows, and caps sun shadows at
/// 1024. `quality_low` also turns off sun shadows, SSAO, light shafts, bloom
/// and depth of field, draws the world at 0.7 resolution, and halves the
/// particle count. Take it and tweak individual fields if the game wants
/// something different.
/// @param level Quality level.
/// @return The limit set for that level.
graphics_quality graphics_quality_preset(quality_level level);

/// Sets the graphics quality ceiling. Applies from the next frame, and can
/// be changed at any time (a settings menu). After the first call,
/// settings_save() stores it alongside the player's settings and
/// settings_load() loads it back.
/// @param ctx Engine context.
/// @param quality Limit set, usually taken from graphics_quality_preset().
void graphics_quality_set(context &ctx, const graphics_quality &quality);

/// Returns the limit set currently in use. The default struct if it was
/// never set.
/// @param ctx Engine context.
/// @return The current limit set.
graphics_quality graphics_quality_get(const context &ctx);

/// Guesses the level that fits the running machine, from the GPU name the
/// OpenGL driver reports.
///
/// `quality_low` for software renderers (machines with no real GPU) and old
/// integrated or mobile GPUs (Intel HD/GMA, Mali, Adreno, VideoCore,
/// PowerVR). `quality_medium` for newer integrated GPUs (Intel integrated
/// Iris/UHD/Arc, an APU's Radeon Vega/Graphics). Everything else is
/// `quality_high`. Only a suggestion for the first run: let the player
/// change it in the menu.
/// @param ctx Engine context.
/// @return Suggested level.
quality_level graphics_quality_suggest(const context &ctx);
/// @}
} // namespace njin
