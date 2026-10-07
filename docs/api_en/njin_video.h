#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_audio.h"

namespace njin {
struct context;

/// @addtogroup grp_video
/// @{

/// How video_open() opens a video.
///
/// **MPEG-1 only** (a `.mpg` file: MPEG-1 picture, MP2 sound), decoded by pl_mpeg.
/// Convert other videos to it with ffmpeg:
/// @code{.sh}
/// ffmpeg -i in.mp4 -c:v mpeg1video -q:v 4 -c:a mp2 -b:a 192k -f mpeg out.mpg
/// @endcode
/// (`-q:v` from 2, the best, to 31; 4 balances quality against file size).
struct video_desc {
  const char *path = nullptr; ///< A `.mpg` file, found like any asset.
  bool loop = false;          ///< Plays again from the start when it ends.
  bool play = true;           ///< Plays at once when opened. `false` opens it and holds the first frame.
  bool audio = true;          ///< Plays the sound (if the file has sound and there is an audio device).
  audio_bus bus = bus_music;  ///< Volume bus of the sound (audio_set_bus_volume()).
  f32 volume = 1.0f;          ///< Its own volume, 0..1, times the bus and `bus_master`.
};

/// Opens a video. The engine decodes it in `phase_post_update` in real time (not
/// by time_set_scale(); it keeps playing while the game is paused), picture and
/// sound in step. Each new frame is copied into the video's texture
/// (video_texture()).
/// @param ctx The engine context.
/// @param desc How to open it.
/// @return Handle, or an invalid handle if the file is missing or not MPEG-1
/// (a warning says why).
video_handle video_open(context &ctx, const video_desc &desc);

/// Closes the video, freeing its texture and sound. An invalid handle is ignored.
/// @param ctx The engine context.
/// @param handle Video.
void video_close(context &ctx, video_handle handle);

/// Plays on (or again from the start if it has ended).
/// @param ctx The engine context.
/// @param handle Video.
void video_play(context &ctx, video_handle handle);

/// Pauses; the picture holds the current frame.
/// @param ctx The engine context.
/// @param handle Video.
void video_pause(context &ctx, video_handle handle);

/// Whether the video is playing.
/// @param ctx The engine context.
/// @param handle Video.
/// @return `true` if it is playing.
bool video_playing(const context &ctx, video_handle handle);

/// Whether the video has played to the end (not looping and at the end): when to
/// move on after a cutscene.
/// @param ctx The engine context.
/// @param handle Video.
/// @return `true` if it has ended.
bool video_finished(const context &ctx, video_handle handle);

/// Jumps to second `seconds`. The picture shown at once is the nearest frame
/// before that point.
/// @param ctx The engine context.
/// @param handle Video.
/// @param seconds The time, seconds, clamped to 0..video_duration().
void video_seek(context &ctx, video_handle handle, f32 seconds);

/// Which second it is at.
/// @param ctx The engine context.
/// @param handle Video.
/// @return The time, seconds.
f32 video_time(const context &ctx, video_handle handle);

/// How many seconds the video lasts.
/// @param ctx The engine context.
/// @param handle Video.
/// @return The duration, seconds.
f32 video_duration(const context &ctx, video_handle handle);

/// Turns looping on or off.
/// @param ctx The engine context.
/// @param handle Video.
/// @param loop Loop.
void video_set_loop(context &ctx, video_handle handle, bool loop);

/// Sets the video's own volume, 0..1.
/// @param ctx The engine context.
/// @param handle Video.
/// @param volume Volume.
void video_set_volume(context &ctx, video_handle handle, f32 volume);

/// Picture size, pixels.
/// @param ctx The engine context.
/// @param handle Video.
/// @return Width and height.
vec2 video_size(const context &ctx, video_handle handle);

/// Picture frames per second of the file.
/// @param ctx The engine context.
/// @param handle Video.
/// @return Frames per second.
f32 video_framerate(const context &ctx, video_handle handle);

/// Whether the video plays sound: the file has sound, `video_desc::audio` is on
/// and there is an audio device.
/// @param ctx The engine context.
/// @param handle Video.
/// @return `true` if it has sound.
bool video_has_audio(const context &ctx, video_handle handle);

/// Number of picture frames shown since it was opened (counting seeks too): to
/// know whether the picture changed.
/// @param ctx The engine context.
/// @param handle Video.
/// @return Number of frames.
i32 video_frame_count(const context &ctx, video_handle handle);

/// The texture holding the current frame. Draw it like any texture
/// (texture_draw_ex()), or put it on a 3D shape (`material3d::texture`,
/// `model_material`): a TV screen, a billboard. The texture belongs to the video:
/// do not texture_unload() it; video_close() frees it.
/// @param ctx The engine context.
/// @param handle Video.
/// @return Texture, or an invalid handle if the video is invalid.
texture_handle video_texture(const context &ctx, video_handle handle);

/// Draws the current frame stretched into `dest` (2D, as texture_draw_ex()).
/// @param ctx The engine context.
/// @param handle Video.
/// @param dest Area to draw in.
/// @param tint Colour multiplied into the picture.
void video_draw(const context &ctx, video_handle handle, rect dest, rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Draws the current frame fitted inside `area`, keeping its aspect ratio: the
/// spare strips on the sides or above and below are filled with `bars` (the
/// black bars of a full-screen cutscene).
/// @param ctx The engine context.
/// @param handle Video.
/// @param area Area, usually the whole screen.
/// @param bars Colour of the spare strips; transparent leaves them empty.
void video_draw_fit(const context &ctx, video_handle handle, rect area, rgba bars = {0.0f, 0.0f, 0.0f, 1.0f});
/// @}
} // namespace njin
