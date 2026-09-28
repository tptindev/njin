#pragma once
#include "_math.h"
#include "_types.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_window
/// @{

/// Request to quit the game. The loop stops at the end of the current frame;
/// `phase_shutdown` still runs as usual.
/// @param ctx Engine context.
void njin_quit(njin_ctx &ctx);

/// Change the window size. Has no effect while in fullscreen.
/// @param ctx Engine context.
/// @param size New size, in pixels.
void window_set_size(njin_ctx &ctx, vec2 size);

/// Change the window title.
/// @param ctx Engine context.
/// @param title New title.
void window_set_title(njin_ctx &ctx, const char *title);

/// Turn fullscreen on or off.
///
/// Uses a borderless window covering the screen, so switching back and forth is
/// fast and does not change the screen resolution.
/// @param ctx Engine context.
/// @param fullscreen `true` to turn on.
void window_set_fullscreen(njin_ctx &ctx, bool fullscreen);

/// Whether the window is in fullscreen.
/// @param ctx Engine context.
/// @return `true` if in fullscreen.
bool window_fullscreen(const njin_ctx &ctx);

/// Turn vertical sync on or off at runtime, see njin_cfg::vsync.
/// @param ctx Engine context.
/// @param vsync `true` to turn on.
void window_set_vsync(njin_ctx &ctx, bool vsync);

/// Whether vertical sync is on.
/// @param ctx Engine context.
/// @return `true` if on.
bool window_vsync(const njin_ctx &ctx);

/// Whether the window was just resized this frame (the user dragged the window,
/// or toggled fullscreen).
/// @param ctx Engine context.
/// @return `true` if the size just changed.
bool window_resized(const njin_ctx &ctx);

/// Show or hide the mouse cursor while it is inside the window.
/// @param ctx Engine context.
/// @param visible `true` to show.
void cursor_set_visible(njin_ctx &ctx, bool visible);

/// Capture the screen and save it to an image file.
///
/// The image is captured at the **end of the current frame**, after `phase_post_render`,
/// so it contains everything in the frame including the UI, whichever phase this
/// function is called in. Calling it several times in one frame saves several
/// files with the same image.
///
/// The format follows the file extension: `.png` (recommended), `.bmp`, `.tga`, `.qoi`.
/// The parent folder is created if it does not exist. The result is written to the log.
/// @param ctx Engine context.
/// @param path File path. If nullptr, saves into the `screenshots` folder
/// in the game save folder (see save_path()), named by date and time.
void screenshot(njin_ctx &ctx, const char *path = nullptr);

/// Turn on virtual resolution: the game draws onto a fixed screen of `size` pixels (for example
/// 320 x 180), then the engine scales it up to the window, keeping the aspect ratio, with the
/// leftover space as bars.
///
/// Everything goes through the virtual screen: world, UI, toast, scene transitions. screen_size()
/// returns `size`, and mouse_pos() and mouse_delta() are in virtual pixels, so the game code
/// does not need to know how big the real window is. The image is scaled without smoothing, so
/// pixel art stays sharp at every window size.
///
/// With `integer_scale`, it only scales by 1, 2, 3... times: every virtual pixel is the same
/// size, and the bars may be thicker. Otherwise it scales to fit the window exactly.
/// @param ctx Engine context.
/// @param size Virtual size, pixels. `{0, 0}` to turn off and draw directly onto the window.
/// @param integer_scale Only scale by integer multiples.
void window_set_virtual_size(njin_ctx &ctx, vec2 size, bool integer_scale = true);

/// Color of the bars around the virtual screen. Black by default.
/// @param ctx Engine context.
/// @param color Color.
void window_set_bar_color(njin_ctx &ctx, rgba color);

/// Real size of the window, pixels, even when virtual resolution is on.
/// @param ctx Engine context.
/// @return Window size.
vec2 window_size(const njin_ctx &ctx);

/// The area of the window that the virtual screen occupies, in window pixels. Without
/// virtual resolution it is the whole window.
/// @param ctx Engine context.
/// @return Image area within the window.
rect window_viewport(const njin_ctx &ctx);

/// Lock the mouse cursor inside the window and hide it, like a first-person shooter.
/// While locked, use mouse_delta() instead of mouse_pos().
/// @param ctx Engine context.
/// @param locked `true` to lock.
void cursor_set_locked(njin_ctx &ctx, bool locked);
/// @}
} // namespace njin
