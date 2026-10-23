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

/// Draws a filled rectangle with all four corners rounded.
/// @param ctx Engine context.
/// @param r Rectangle.
/// @param radius Corner radius, in pixels; larger than half the short side is
///        clamped to half the short side (making a pill shape). 0 is square corners like draw_rect().
/// @param color Color.
void draw_rect_rounded(const context &ctx, rect r, f32 radius, rgba color);

/// Draws the outline of a rectangle with all four corners rounded. The outline lies inside
/// `r`, like draw_rect_lines(), so drawing with the same `r` and `radius` as draw_rect_rounded()
/// makes the outline hug the fill exactly.
/// @param ctx Engine context.
/// @param r Rectangle.
/// @param radius Outer corner radius of the outline, in pixels.
/// @param thickness Outline thickness.
/// @param color Color.
void draw_rect_rounded_lines(const context &ctx, rect r, f32 radius, f32 thickness, rgba color);

/// Blurs whatever was already drawn under `r` (a frosted-glass look): the background of a
/// popup, to set it apart from what is behind it while still showing it faintly. The part already
/// drawn inside `r` is replaced by itself blurred, clipped to the corner radius `radius`;
/// the popup is then drawn on top, with a slightly transparent background so the blurred layer
/// shows through.
///
/// `r` is measured in screen space, like the UI (including a canvas from ui_canvas_set()). Does
/// nothing when: `blur` <= 0, inside a world camera, drawing into a render
/// texture, or on a machine with a software renderer. Do not call it between draw_offset_push()
/// and draw_offset_pop(). Text already queued to draw sharp at the window resolution
/// (config::crisp_text) that lies under `r` is hidden, since it is not yet in the image to blur.
/// Each call costs one copy of that region plus a few draws at half size.
/// ui_skin::blur does this for the background of widgets and panels.
/// @param ctx Engine context.
/// @param r Region to blur.
/// @param blur Blur amount: radius, in pixels.
/// @param radius Corner radius of the region, in pixels. 0 is square corners.
/// @param tint Color multiplied into the blurred layer; alpha less than 1 blends the blurred layer with the sharp image beneath it.
void draw_backdrop_blur(const context &ctx, rect r, f32 blur, f32 radius = 0.0f,
                        rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Turns drawing rounded shapes with an SDF (signed distance field) on or off: each shape is
/// one quad, and each pixel's coverage is computed in the shader, so edges and rounded corners
/// are anti-aliased smoothly at any scale (virtual resolution, smooth UI). Applies to
/// draw_rect_rounded(), draw_rect_rounded_lines() and the UI's flat color faces (a ui_skin without an
/// image). Off by default: shapes are drawn with raylib's triangles as before. Each shape changes a
/// uniform, so it costs one separate draw call; worth using for the UI, from medium graphics quality up.
/// When a different shader is already active (shader_begin(), draw_dissolve_begin()) the shape
/// still draws the old way under that shader.
/// @param ctx Engine context.
/// @param on Turn on or off.
void draw_set_smooth_shapes(context &ctx, bool on);

/// Whether rounded shapes are currently drawn with SDF (draw_set_smooth_shapes()).
/// @param ctx Engine context.
/// @return `true` when on.
bool draw_smooth_shapes(const context &ctx);

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

/// Draws a triangle covered by part of a texture: each vertex samples the image at its
/// `uv` coordinate (0..1 over the image, `{0, 0}` is the top-left corner, `{1, 1}` the bottom-right),
/// the part in between is interpolated, then multiplied by `tint`. The order of the three vertices
/// does not matter. Use it when an image must fit a shape that is not a rectangle: a pie slice, a
/// curved strip, a polygon cut into triangles. A texture inside an atlas also works, `uv` is still
/// measured over that image alone.
/// @code
/// // A patterned pie slice, with the slice's center at the image's bottom-right corner.
/// const auto uv_of = [&](njin::vec2 p) { return njin::vec2{1, 1} + (p - corner) / radius; };
/// njin::draw_triangle_textured(ctx, g.pattern, a, b, c, uv_of(a), uv_of(b), uv_of(c));
/// @endcode
/// @param ctx Engine context.
/// @param texture Image to cover the triangle with.
/// @param a First vertex.
/// @param b Second vertex.
/// @param c Third vertex.
/// @param uv_a Image coordinate of vertex `a`.
/// @param uv_b Image coordinate of vertex `b`.
/// @param uv_c Image coordinate of vertex `c`.
/// @param tint Color multiplied into the image; white keeps it unchanged.
void draw_triangle_textured(const context &ctx, texture_handle texture, vec2 a, vec2 b, vec2 c, vec2 uv_a,
                            vec2 uv_b, vec2 uv_c, rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Like the version above, covered by part of a render texture (`uv` measured like a regular
/// image: `{0, 0}` is the top-left corner of whatever was drawn into it). Use it to redraw
/// something already drawn into a render texture while distorting it: bending, squeezing, pulling
/// into a point.
/// @param ctx Engine context.
/// @param texture Render texture to cover the triangle with.
/// @param a First vertex.
/// @param b Second vertex.
/// @param c Third vertex.
/// @param uv_a Image coordinate of vertex `a`.
/// @param uv_b Image coordinate of vertex `b`.
/// @param uv_c Image coordinate of vertex `c`.
/// @param tint Color multiplied into the image; white keeps it unchanged.
void draw_triangle_textured(const context &ctx, render_texture_handle texture, vec2 a, vec2 b, vec2 c,
                            vec2 uv_a, vec2 uv_b, vec2 uv_c, rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Fades everything drawn afterward (shapes, text, images, UI): their opacity multiplied by
/// `alpha`, until draw_alpha_pop(). Nests, with the layers multiplying together; each push needs
/// one pop within the same frame. Use it to fade a group of drawn things in or out together
/// (a HUD panel closing, a loading screen).
/// @code
/// njin::draw_alpha_push(ctx, 1.0f - leaving);
/// draw_card(ctx);   // its background, text and icon all fade along with it
/// njin::draw_alpha_pop(ctx);
/// @endcode
/// @param ctx Engine context.
/// @param alpha Opacity, 0 (transparent) to 1 (unchanged).
void draw_alpha_push(const context &ctx, f32 alpha);

/// Removes the nearest draw_alpha_push() fade. @param ctx Engine context.
void draw_alpha_pop(const context &ctx);

/// Scales everything drawn afterwards by `scale`, then moves it by `offset`
/// (world units), until draw_offset_pop(): a point `p` is drawn at
/// `p * scale + offset`. Draws a part of the world that has its own
/// coordinates (a map area generated on its own) in its place, or draws it
/// shrunk into a render texture and that texture scaled back up. Nests; every
/// push needs one pop in the same phase. Pixel text (draw_text with a pixel
/// font) is not moved.
/// @code
/// njin::draw_offset_push(ctx, ward_origin);
/// draw_ward(ctx, ward); // the area's own coordinates
/// njin::draw_offset_pop(ctx);
/// @endcode
/// @param ctx Engine context.
/// @param offset The offset.
/// @param scale The scale factor, 1 keeps the size.
void draw_offset_push(const context &ctx, vec2 offset, f32 scale = 1.0f);

/// Removes the offset of the latest draw_offset_push().
/// @param ctx Engine context.
void draw_offset_pop(const context &ctx);
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
/// @param path Font file path (ttf, otf, or ttc: the first font in the collection).
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

/// Chooses the font drawn when no font is passed (an empty handle `{}`), for text across the
/// whole game, both the UI and draw_text(), changed once instead of passing a font at every call.
/// An invalid handle, `{}`, or a freed font falls back to the engine's font
/// (JetBrains Mono). Not calling this function leaves text drawn exactly as before.
///
/// njin::font_set_style() with an empty handle still changes the style of the engine's font,
/// not the font chosen here: to change this font's style, pass its own handle.
/// @param ctx Engine context.
/// @param font Font to use as the default, `{}` to fall back to the engine's font.
void font_set_default(context &ctx, font_handle font);

/// Makes a font also build the characters found in `text`, beyond the built-in Latin and
/// Vietnamese character set: Russian, Thai, Chinese, Japanese, Korean... Each text size is
/// rebuilt on the next draw. A character the font does not have still cannot be drawn: Chinese
/// text needs a font that has Chinese glyphs. Usually called once with an entire language file.
/// @code
/// const njin::font_handle cjk = njin::font_load(ctx, "C:/Windows/Fonts/msyh.ttc");
/// std::string zh;
/// if (njin::file_read("assets/lang/zh.json", zh))
///   njin::font_add_chars(ctx, cjk, zh.c_str());
/// njin::font_set_default(ctx, cjk);
/// @endcode
/// @param ctx Engine context.
/// @param font Font to add to, handle id 0 is the engine's font.
/// @param text Text (UTF-8) with the characters to build.
void font_add_chars(context &ctx, font_handle font, const char *text);

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
  /// Smooth at any distance: mipmaps made and trilinear filtering. For images
  /// repeated over large 3D surfaces seen from afar (walls, floors), where
  /// `filter_linear` shimmers. Images in an atlas and render textures get
  /// `filter_linear`.
  filter_mipmap,
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
