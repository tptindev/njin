#pragma once
#include "_types.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_core
/// @{

/// Window and loop configuration, passed to njin_create().
struct njin_cfg {
  const char *title;  ///< Window title.
  f32 width;          ///< Window width (pixels).
  f32 height;         ///< Window height (pixels).
  f32 target_fps;     ///< Target FPS of the loop.
  /// Background color cleared every frame. White by default.
  rgba clear_bg_color = { .r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0 };
  /// Number of times `phase_fixed_update` runs per second. Default 60.
  f32 fixed_hz = 60.0f;
  /// Key that closes the game immediately. Esc by default. Set `key_none` to disable it, when
  /// the game needs Esc for something else (for example opening a pause menu).
  key_code exit_key = key_escape;
  /// Lets the user drag to resize the window.
  bool resizable = false;
  /// Name of the game's save folder, see save_path(). If empty, `title` is used.
  const char *app_name = nullptr;
  /// Virtual resolution, for example `{320, 180}` for pixel art. `{0, 0}` disables it. See
  /// window_set_virtual_size().
  vec2 virtual_size{};
  /// With a virtual resolution: scale only by integer multiples (every virtual pixel is the
  /// same size), and the leftover window area becomes a border.
  bool integer_scale = true;
  /// Vertical sync: each frame waits for the screen to finish refreshing before presenting, so there is no tearing
  /// and the GPU and CPU rest between frames (saves battery). Off by default. When on,
  /// `target_fps` still applies as an extra cap. See window_set_vsync().
  bool vsync = false;
  /// Draws text at the window's real resolution when the machine has a real GPU, so text stays sharp at every
  /// window size (on by default). With a virtual resolution (`virtual_size`), the scene is
  /// drawn small and then scaled up, and text drawn in it becomes blurry; when this is on, on-screen text
  /// (UI, HUD) is drawn after scaling, from a font built at the exact on-screen size. See
  /// the Drawing and text page. Machines with only a software renderer always draw text into the virtual image.
  bool crisp_text = true;
  /// Draws the interface at the window's real resolution, for smooth UI. With a virtual resolution
  /// (`virtual_size`), by default the whole frame is drawn into a small image and then scaled up with a
  /// nearest filter, so rounded panels, buttons and sliders look pixelated according to the scale. When this
  /// is on, the world is still drawn into the virtual image (pixel art), while `phase_post_render` (UI, HUD),
  /// dialogs, toasts, flash and fade are drawn after the image has been scaled, straight into the window:
  /// shapes and text are smooth at every window size, and coordinates are still in virtual pixels.
  /// Off by default: the UI keeps the pixel style. When on, `crisp_text` is no longer needed.
  /// See the Drawing and text page.
  bool smooth_ui = false;
  /// Anti-aliasing by supersampling: the world and interface are drawn at
  /// `render_scale` times the resolution (per axis) and then scaled back down with a smooth
  /// filter when going to the screen. `1` is off (default). `2`, `4` or `8` give smoother edges,
  /// costing that many times more pixels for the GPU to draw (4 times at level 2, 16 times
  /// at level 4...).
  ///
  /// This is not the window's MSAA (raylib has only a single 4x level through GLFW,
  /// and 2x/8x cannot be chosen); this approach runs the same on every GPU and gives
  /// exactly the level that was set. It cannot be changed while running: `njin_create()` creates
  /// the window and the render textures with exactly this value once, so changing the level
  /// needs a game restart (read the value the player chose from your own settings file,
  /// before calling njin_create() on the next run). See render_scale().
  ///
  /// It changes no coordinates: screen_size(), mouse and camera are still computed as with
  /// `virtual_size` (if set) or the window size (if not), and know nothing
  /// about `render_scale`. Pixel art uses `virtual_size` with `filter_nearest`, so it
  /// usually does not need this; it suits games with drawn shapes, rotated sprites or vector text better.
  i32 render_scale = 1;
};

/// Returns the configured target FPS (`target_fps`).
///
/// This is a configuration value, not the measured FPS. For frame time
/// use delta().
/// @param ctx Engine context.
/// @return Target FPS.
f32 fps(const njin_ctx &ctx);

/// Returns the size of the screen the game draws to, in pixels.
///
/// Without a virtual resolution this is the window: initially equal to the configured `width`, `height`,
/// and it changes when the user drags the window (if `resizable`) or
/// when fullscreen is turned on. With a virtual resolution (window_set_virtual_size()) it is
/// the virtual size, which does not change with the window; the window's real size is
/// window_size().
/// @param ctx Engine context.
/// @return Window size: `x` is width, `y` is height.
vec2 screen_size(const njin_ctx &ctx);
/// @}
}
