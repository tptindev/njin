#pragma once
#include "_types.h"

namespace njin {
// Opaque, see njin_ctx.h.
struct njin_ctx;

/// @addtogroup grp_shader
/// @{

/// Loads and compiles a shader.
///
/// Either path may be nullptr to keep raylib's default stage.
/// An invalid or unloaded handle is ignored by every shader function.
/// @param ctx Engine context.
/// @param vspath Vertex shader path, or nullptr.
/// @param fspath Fragment shader path, or nullptr.
/// @return Shader handle, or a handle with id 0 if a file is missing or compilation fails.
shader_handle shader_load(njin_ctx &ctx, const char *vspath,
                          const char *fspath);

/// Frees a shader. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Shader to free.
void shader_unload(njin_ctx &ctx, shader_handle handle);

/// Enables the shader for everything drawn afterwards, until shader_end().
///
/// Must be called between the start and end of the frame's drawing. Set
/// uniforms before calling this.
/// @param ctx Engine context.
/// @param handle Shader to enable.
void shader_begin(const njin_ctx &ctx, shader_handle handle);

/// Disables the shader enabled by shader_begin().
/// @param ctx Engine context.
void shader_end(const njin_ctx &ctx);

/// Sets an `int` uniform. A uniform that does not exist is logged as a warning
/// once and then ignored.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Uniform name in the shader.
/// @param value Value.
void shader_set_i32(njin_ctx &ctx, shader_handle handle, const char *name,
                    i32 value);

/// Sets a `float` uniform. See shader_set_i32() about missing uniforms.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Uniform name in the shader.
/// @param value Value.
void shader_set_f32(njin_ctx &ctx, shader_handle handle, const char *name,
                    f32 value);

/// Sets a `vec2` uniform. See shader_set_i32() about missing uniforms.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Uniform name in the shader.
/// @param value Value.
void shader_set_vec2(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec2 value);

/// Sets a `vec3` uniform. See shader_set_i32() about missing uniforms.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Uniform name in the shader.
/// @param value Value.
void shader_set_vec3(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec3 value);

/// Sets a `vec4` uniform. See shader_set_i32() about missing uniforms.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Uniform name in the shader.
/// @param value Value.
void shader_set_vec4(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec4 value);

/// Sets a `vec4` array uniform, for example `uniform vec4 lights[8];`.
///
/// `count` must not exceed the array size declared in the shader. To pass an
/// array of `vec2` or `vec3`, pack it into `vec4` (two `vec2` per element). See
/// shader_set_i32() about missing uniforms.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Array name in the shader, without `[0]`.
/// @param values The elements, contiguous in memory.
/// @param count Number of elements. 0 does nothing.
void shader_set_vec4_array(njin_ctx &ctx, shader_handle handle, const char *name,
                           const vec4 *values, u32 count);

/// Binds a texture to a `sampler2D` uniform of the shader, besides `texture0`.
///
/// Use it for extra images the shader reads: color palette (LUT), noise, mask,
/// normal map. Each shader takes at most 4 extra images; setting the same name
/// again replaces the image. The image is bound every time the shader is enabled
/// (shader_begin(), camera_set_post_shader(), draw_instanced()), so calling this
/// once is enough. When the image is reloaded by hot reload, the shader sees the new image.
///
/// - Use a standalone image, from texture_load(). An image packed in an atlas is rejected,
///   because the shader would see the whole atlas page rather than just that image.
/// - An extra image has no per-shader filter: it is sampled according to the
///   image's own texture_set_filter().
/// - The most reliable paths: the camera's post-processing shader from
///   camera_set_post_shader(), and draw_instanced(). Through shader_begin() the
///   extra image only lives until raylib flushes the next batch (8192 shapes
///   full, or 256 texture switches, or a draw_instanced() or a render texture
///   change in between); use it for a few draw calls, not for thousands of
///   sprites.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Name of the `uniform sampler2D` in the shader.
/// @param texture Image to bind.
void shader_set_texture(njin_ctx &ctx, shader_handle handle, const char *name, texture_handle texture);

/// Like the previous overload, with a render texture.
///
/// Do not bind a render texture that is currently being drawn into (between
/// render_texture_begin() and render_texture_end()). Render textures are stored
/// flipped on the vertical axis: the shader must flip `v` (`1.0 - v`) to read
/// them the right way up.
/// @param ctx Engine context.
/// @param handle Shader to set.
/// @param name Name of the `uniform sampler2D` in the shader.
/// @param texture Render texture to bind.
void shader_set_texture(njin_ctx &ctx, shader_handle handle, const char *name,
                        render_texture_handle texture);
/// @}

/// @addtogroup grp_instancing
/// @{

/// Whether this machine can draw instanced: needs OpenGL 3.3 or newer (or ES 3.0).
///
/// When `false`, every instance_* function returns an invalid handle or does
/// nothing; the game should have another way to draw ready (for example
/// texture_draw_ex() one by one).
/// @param ctx Engine context.
/// @return `true` if draw_instanced() can draw.
bool instancing_available(const njin_ctx &ctx);

/// Creates an instance buffer on the GPU: each instance is `floats_per_instance`
/// floats, whose meaning the game decides.
///
/// The shader reads them as per-instance `vec4` attributes named `instance0`,
/// `instance1`, `instance2`, `instance3` (4 floats per attribute, in order).
/// The buffer grows by itself when instance_buffer_upload() needs more room.
/// @param ctx Engine context.
/// @param floats_per_instance 4, 8, 12 or 16.
/// @return Handle, or a handle with id 0 if the number is invalid or the machine
/// does not support it (see instancing_available()).
instance_buffer_handle instance_buffer_create(njin_ctx &ctx, u32 floats_per_instance);

/// Destroys an instance buffer. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Buffer to destroy.
void instance_buffer_destroy(njin_ctx &ctx, instance_buffer_handle handle);

/// Writes `count` instances into the buffer, replacing all previous content.
///
/// `data` holds `count * floats_per_instance` floats, one instance after the
/// other. Calling it every frame with new data is the normal usage.
/// @param ctx Engine context.
/// @param handle Buffer to write.
/// @param data Instance data.
/// @param count Number of instances.
void instance_buffer_upload(njin_ctx &ctx, instance_buffer_handle handle, const f32 *data,
                            u32 count);

/// Draws `count` squares, starting at instance `first`, with **one** draw call.
///
/// Each square has 6 vertices (2 triangles). The game's vertex shader receives:
/// - `in vec3 vertexPosition`: a corner of the unit square, from `(0, 0)` to
///   `(1, 1)`, y pointing down. Only `xy` is meaningful.
/// - `in vec4 instance0` ... `instance3`: the data of the instance being drawn.
/// - `uniform mat4 mvp`: the current camera, as in normal drawing.
///
/// The shader places the square in the world (position, size, rotation) from the
/// instance data. The current blend mode (blend_begin()) is kept; anything drawn
/// earlier is flushed first, so the draw order is exactly the call order. An
/// instance drawn later covers one drawn earlier: sort the data (for example by
/// y) before writing it.
///
/// Must be called between the start and end of the frame's drawing, outside
/// shader_begin()/shader_end(). Other uniforms of the shader are set with
/// shader_set_*() as usual.
/// @param ctx Engine context.
/// @param handle Buffer written with instance_buffer_upload().
/// @param shader Shader whose vertex shader reads the attributes above.
/// @param first First instance to draw.
/// @param count Number of instances, clamped if it exceeds the number written.
void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count);

/// Like the previous overload, and binds `texture` to the shader's `sampler2D texture0` uniform.
///
/// Use it when each instance is a frame in a sprite sheet: the vertex shader picks
/// the frame from the instance data, the fragment shader reads it. For a texture
/// inside an atlas (atlas_load()), the coordinates are those of the whole atlas page.
/// @param ctx Engine context.
/// @param handle Buffer written with instance_buffer_upload().
/// @param shader Shader whose vertex shader reads the attributes above.
/// @param first First instance to draw.
/// @param count Number of instances, clamped if it exceeds the number written.
/// @param texture Texture bound to `texture0`. An invalid handle draws nothing.
void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count, texture_handle texture);

/// Like the previous overload, with a render texture, for example a sprite sheet just drawn (baked) once
/// with render_texture_begin().
///
/// Texture coordinates have their origin at the **bottom-left corner** of the drawn
/// image (`v = 1 - y / height`), because the OpenGL framebuffer is stored bottom-up.
/// A render texture is sampled with `filter_nearest` unless changed with
/// render_texture_set_filter().
/// @param ctx Engine context.
/// @param handle Buffer written with instance_buffer_upload().
/// @param shader Shader whose vertex shader reads the attributes above.
/// @param first First instance to draw.
/// @param count Number of instances, clamped if it exceeds the number written.
/// @param texture Render texture bound to `texture0`. An invalid handle draws nothing.
void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count, render_texture_handle texture);
/// @}

/// @addtogroup grp_texture
/// @{

/// Loads a texture from an image file.
///
/// An invalid or unloaded handle is ignored by every texture function.
/// @param ctx Engine context.
/// @param path Image file path.
/// @return Texture handle, or a handle with id 0 if the file is missing or cannot be decoded.
texture_handle texture_load(njin_ctx &ctx, const char *path);

/// Frees a texture. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Texture to free.
void texture_unload(njin_ctx &ctx, texture_handle handle);

/// Texture size (pixels).
/// @param ctx Engine context.
/// @param handle Texture to query.
/// @return The size, or `{0, 0}` if the handle is invalid.
vec2 texture_size(const njin_ctx &ctx, texture_handle handle);

/// Draws the texture with its top-left corner at `pos`.
///
/// `tint` is multiplied into every pixel; white `{1, 1, 1, 1}` leaves the image unchanged.
/// Must be called between the start and end of the frame's drawing.
/// @param ctx Engine context.
/// @param handle Texture to draw.
/// @param pos Top-left corner position.
/// @param tint Color multiplied into the image.
void texture_draw(const njin_ctx &ctx, texture_handle handle, vec2 pos,
                  rgba tint);
/// @}

/// @addtogroup grp_render_texture
/// @{

/// Creates a render texture: an off-screen image to draw into, then draw back as a
/// texture.
///
/// Use it for post-processing or to pre-draw a complex scene.
/// @param ctx Engine context.
/// @param width Width (pixels).
/// @param height Height (pixels).
/// @return Handle, or a handle with id 0 if the size is 0 or creation fails.
render_texture_handle render_texture_load(njin_ctx &ctx, u32 width,
                                          u32 height);

/// Frees a render texture. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Render texture to free.
void render_texture_unload(njin_ctx &ctx, render_texture_handle handle);

/// Render texture size (pixels).
/// @param ctx Engine context.
/// @param handle Render texture to query.
/// @return The size, or `{0, 0}` if the handle is invalid.
vec2 render_texture_size(const njin_ctx &ctx, render_texture_handle handle);

/// Starts drawing into a render texture, keeping the old content.
///
/// Every draw call between render_texture_begin() and render_texture_end() goes
/// into the render texture, in its own pixel space (not through the camera).
///
/// This call resets the camera transform, so do not use it in
/// `phase_pre_render` or `phase_render`. Draw into render textures in
/// `phase_post_update` or `phase_post_render`.
/// @param ctx Engine context.
/// @param handle Render texture to draw into. An invalid handle is ignored.
void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle);

/// Same as the previous overload but first clears the render texture with the `clear` color.
/// @param ctx Engine context.
/// @param handle Render texture to draw into. An invalid handle is ignored.
/// @param clear Color used to clear.
void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle,
                          rgba clear);

/// Ends drawing into the render texture.
/// @param ctx Engine context.
void render_texture_end(const njin_ctx &ctx);

/// Saves the render texture content to an image file, immediately.
///
/// The image has the same orientation as drawn (the top-left corner of the drawing is the
/// top-left corner of the file) and keeps **every byte as is**, alpha channel
/// included: alpha is neither multiplied nor divided, so a render texture used to
/// hold data (a baked sprite sheet, see draw_instanced()) is saved exactly as it
/// holds it.
///
/// The format follows the file extension: `.png` (recommended), `.bmp`, `.tga`, `.qoi`. The parent
/// folder is created if it does not exist. Do not call between render_texture_begin() and
/// render_texture_end(). It reads back from the GPU, so it is much slower than a draw
/// call: use it when exporting a file, not every frame.
/// @param ctx Engine context.
/// @param handle Render texture to save. An invalid handle returns `false`.
/// @param path File path. To save into the game's save folder, use save_path().
/// @return `true` if saved. The reason for a failure (unknown extension, cannot write) is written to the log.
bool render_texture_save(njin_ctx &ctx, render_texture_handle handle, const char *path);

/// Draws the render texture content the right way up, top-left corner at `pos`.
/// @param ctx Engine context.
/// @param handle Render texture to draw.
/// @param pos Top-left corner position.
/// @param tint Color multiplied into the image. White `{1, 1, 1, 1}` leaves the image unchanged.
void render_texture_draw(const njin_ctx &ctx, render_texture_handle handle,
                         vec2 pos, rgba tint);
/// @}
} // namespace njin
