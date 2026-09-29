#pragma once
#include "_math.h"
#include <string>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_draw
/// @{

/// Draws a filled rectangle.
///
/// Every draw function must be called between the start and end of the frame's drawing, that is in
/// `phase_pre_render`, `phase_render` or `phase_post_render`. The space (world
/// or screen) depends on the phase: see the Game loop page.
/// @param ctx Engine context.
/// @param r Rectangle.
/// @param color Color.
void draw_rect(const context &ctx, rect r, rgba color);

/// Draws a rectangle outline. The outline lies inside `r`.
/// @param ctx Engine context.
/// @param r Rectangle.
/// @param thickness Outline thickness.
/// @param color Color.
void draw_rect_lines(const context &ctx, rect r, f32 thickness, rgba color);

/// Draws a filled rectangle rotated around its center.
/// @param ctx Engine context.
/// @param center Center.
/// @param size Size.
/// @param rotation Rotation angle in degrees, clockwise.
/// @param color Color.
void draw_rect_rotated(const context &ctx, vec2 center, vec2 size,
                       f32 rotation, rgba color);

/// Draws a filled circle.
/// @param ctx Engine context.
/// @param center Center.
/// @param radius Radius.
/// @param color Color.
void draw_circle(const context &ctx, vec2 center, f32 radius, rgba color);

/// Draws a circle outline. The outline lies inside the radius.
/// @param ctx Engine context.
/// @param center Center.
/// @param radius Radius.
/// @param thickness Outline thickness.
/// @param color Color.
void draw_circle_lines(const context &ctx, vec2 center, f32 radius,
                       f32 thickness, rgba color);

/// Draws a line segment.
/// @param ctx Engine context.
/// @param a Start point.
/// @param b End point.
/// @param thickness Thickness.
/// @param color Color.
void draw_line(const context &ctx, vec2 a, vec2 b, f32 thickness, rgba color);

/// Draws a filled triangle. The order of the three vertices does not matter.
/// @param ctx Engine context.
/// @param a First vertex.
/// @param b Second vertex.
/// @param c Third vertex.
/// @param color Color.
void draw_triangle(const context &ctx, vec2 a, vec2 b, vec2 c, rgba color);
/// @}

/// @addtogroup grp_text
/// @{

/// Text rendering style, see font_set_style().
enum font_style : u8 {
  font_smooth, ///< Anti-aliased, smooth filtering. Suits vector fonts and text read at length.
  font_pixel,  ///< No anti-aliasing, nearest filtering: each glyph texel is either on
               ///< or off. Suits pixel art games.
};

/// Loads a TrueType/OpenType font, with Vietnamese characters included.
///
/// Each drawn text size has its own glyph image, built at exactly that size on the first
/// draw (the size is rounded to an integer number of pixels, from 6 to 256), so text is crisp at every
/// size and one font works for both small and large text.
/// @param ctx Engine context.
/// @param path Font file path (ttf, otf).
/// @param size Size prebuilt at load time, in pixels, so the first draw does not
/// have to wait. Omitted or 0 makes a trial build at size 16 to check that the file is readable.
/// @param style Text rendering style, font_smooth by default.
/// @return Font handle, or a handle with id 0 (the default font) if loading fails.
font_handle font_load(context &ctx, const char *path, i32 size = 0,
                      font_style style = font_smooth);

/// Changes the text rendering style of a font, including the default font (handle id 0). Glyph
/// images already built are discarded and rebuilt on the next draw.
///
/// The font_pixel style turns off anti-aliasing and uses nearest filtering, so it is **crisp only when drawn
/// at the size the font was designed for**: a pixel font like Press Start 2P designed at 8
/// pixels is drawn at 8, 16, 24. A regular vector font (like JetBrains Mono) built at a small
/// size this way will look jagged. Text of this style is always drawn in the virtual image, scaled with
/// the same nearest filter as sprites, and does not go through the crisp text layer of
/// config::crisp_text. Enable `integer_scale` for the virtual resolution so every text
/// pixel has the same size.
/// @param ctx Engine context.
/// @param font Font to change, handle id 0 is the default font.
/// @param style New style.
void font_set_style(context &ctx, font_handle font, font_style style);

/// Frees a font. An invalid handle is ignored. Drawing with a freed handle uses
/// the default font.
/// @param ctx Engine context.
/// @param font Font to free.
void font_unload(context &ctx, font_handle font);

/// Draws text with its top-left corner at `pos`. Supports line breaks with `\n`.
///
/// The string is UTF-8. The default font (handle id 0) is JetBrains Mono, which already has
/// Vietnamese text. The position is rounded to a pixel so text does not blur.
///
/// With a virtual resolution and a real GPU, on-screen text (outside the world) is drawn at the
/// window resolution, see config::crisp_text.
/// @param ctx Engine context.
/// @param text UTF-8 string.
/// @param pos Top-left corner position.
/// @param size Text size, in pixels.
/// @param color Color.
/// @param font Font, defaults to the engine's font.
void draw_text(const context &ctx, const char *text, vec2 pos, f32 size,
               rgba color, font_handle font = {});

/// Size of a string when drawn with draw_text() with the same parameters.
///
/// Use it to center or right-align: `pos.x = center.x - text_measure(...).x / 2`.
/// @param ctx Engine context.
/// @param text UTF-8 string.
/// @param size Text size, in pixels.
/// @param font Font, defaults to the engine's font.
/// @return Width and height, in pixels.
vec2 text_measure(const context &ctx, const char *text, f32 size,
                  font_handle font = {});

/// Splits a piece of text into lines no wider than `max_width` when drawn with
/// draw_text() at the same size and font.
///
/// Only breaks lines at spaces and at `\n`, so it never cuts a word in two
/// (or a UTF-8 character); a word longer than the whole line stands alone on its own line.
/// @param ctx Engine context.
/// @param text UTF-8 string.
/// @param size Text size, pixels.
/// @param max_width Maximum width of a line, pixels.
/// @param font Font.
/// @return The lines, in order.
std::vector<std::string> text_wrap(const context &ctx, const char *text, f32 size,
                                   f32 max_width, font_handle font = {});

/// Draws a piece of text that wraps automatically within the width `max_width`, top-left corner at
/// `pos`.
/// @param ctx Engine context.
/// @param text UTF-8 string.
/// @param pos Top-left corner.
/// @param size Text size, pixels.
/// @param max_width Maximum width, pixels.
/// @param color Color.
/// @param font Font.
/// @param line_spacing Line spacing, multiplied by the line height. 1 is tight.
/// @return Size of the block of text drawn.
vec2 draw_text_wrapped(const context &ctx, const char *text, vec2 pos, f32 size,
                       f32 max_width, rgba color, font_handle font = {},
                       f32 line_spacing = 1.1f);
/// @}

/// @addtogroup grp_texture
/// @{

/// Sampling method when a texture is scaled.
enum texture_filter {
  filter_nearest, ///< Keeps every pixel, sharp edges. Use for pixel art.
  filter_linear,  ///< Smoothing. Default.
};

/// Changes the sampling method of a texture.
/// @param ctx Engine context.
/// @param handle Texture to change.
/// @param filter Sampling method.
void texture_set_filter(context &ctx, texture_handle handle,
                        texture_filter filter);

/// Changes the sampling method of a render texture.
///
/// A newly created render texture is sampled with `filter_nearest`. Set `filter_linear` when
/// the game's shader (see draw_instanced()) reads it at a size different from the size
/// it was drawn at.
/// @param ctx Engine context.
/// @param handle Render texture to change.
/// @param filter Sampling method.
void render_texture_set_filter(context &ctx, render_texture_handle handle,
                               texture_filter filter);

/// Full parameters for texture_draw_ex().
struct texture_draw_desc {
  vec2 pos{};     ///< Position of the `origin` anchor point.
  /// Region of the image, in pixels. Size 0 means the whole image. Used for sprite
  /// sheets.
  rect source{};
  vec2 scale{1.0f, 1.0f}; ///< Scale per axis. A negative value flips.
  /// Anchor point, as a fraction of the size: `{0, 0}` is the top-left corner, `{0.5,
  /// 0.5}` is the center. `pos` lands exactly on this point, and the image rotates around it.
  vec2 origin{};
  f32 rotation = 0.0f; ///< Rotation angle in degrees, clockwise.
  bool flip_x = false; ///< Flip horizontally.
  bool flip_y = false; ///< Flip vertically.
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Color multiplied into the image.
};

/// Draws a texture with a source region, scale, anchor point, rotation and flips.
/// @param ctx Engine context.
/// @param handle Texture to draw.
/// @param desc Draw parameters.
void texture_draw_ex(const context &ctx, texture_handle handle,
                     const texture_draw_desc &desc);

/// Attaches a fixed "material" shader to a texture: no need to wrap every draw
/// in shader_begin()/shader_end() anymore.
///
/// From the next draw on, every texture_draw() and texture_draw_ex() that draws
/// `handle` auto-binds this shader (including any auxiliary textures attached
/// with shader_set_texture()), then unbinds it right after drawing. The old way
/// still works exactly as before and always wins: if a different shader_begin()
/// is already active (even one from another texture's material),
/// texture_draw()/texture_draw_ex() leaves it alone instead of swapping in its
/// own material. Set uniforms with shader_set_*() before drawing, same as
/// shader_begin().
///
/// Does not apply to sprites/tilemaps/particles drawn through the ECS: those
/// systems manage their own shaders (e.g. the flash/dissolve effects of
/// njin_fx.h).
/// @param ctx Engine context.
/// @param handle Texture to attach to.
/// @param shader Shader to attach, or a handle with id 0 to remove the material.
void texture_set_shader(context &ctx, texture_handle handle,
                        shader_handle shader);
/// @}

/// @addtogroup grp_draw
/// @{

/// Blend mode of what is drawn after blend_begin().
enum blend_mode {
  blend_alpha,    ///< Blends by transparency. Default.
  blend_additive, ///< Adds colors: brightens. Use for fire, light, sparks.
  blend_multiply, ///< Multiplies colors: darkens. Use for drop shadows, color overlays.
};

/// Changes the blend mode for everything drawn afterwards, until blend_end().
/// @param ctx Engine context.
/// @param mode Blend mode.
void blend_begin(const context &ctx, blend_mode mode);

/// Enables Y sorting for a draw layer: within that layer, sprites and particles with a
/// larger `y` (lower on the screen) are drawn later, so they cover what stands
/// behind them. This is how top-down games let a character walk around behind a tree or a house.
///
/// `y` is `transform.pos.y + sprite::sort_offset`. Set `sprite::origin` at the
/// feet (`{0.5, 1}`) and no `sort_offset` is needed. A tilemap in that layer is still drawn
/// before every sprite (as the background). Two things with the same `y` keep a stable order between
/// frames.
/// @param ctx Engine context.
/// @param layer Draw layer (njin::sprite::layer).
/// @param on `true` to enable.
void draw_set_y_sort(context &ctx, i32 layer, bool on);

/// Returns to the default blend mode.
/// @param ctx Engine context.
void blend_end(const context &ctx);

/// Only draw inside a region of the screen, until clip_end().
///
/// `area` is in **screen** pixels, not through the camera. Use it for scrolling panels
/// in the UI.
/// @param ctx Engine context.
/// @param area Region allowed to be drawn, screen pixels.
void clip_begin(const context &ctx, rect area);

/// Removes the draw region limit.
/// @param ctx Engine context.
void clip_end(const context &ctx);
/// @}
} // namespace njin
