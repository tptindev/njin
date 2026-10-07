#pragma once
#include "_types.h"
#include <entt/entity/fwd.hpp>

namespace njin {
// Opaque, see njin_ctx.h.
struct context;

/// @addtogroup grp_sound
/// @{

/// Audio mixing bus, like a slider in a game's settings menu.
///
/// The real volume of a sound is its own volume times its bus volume times
/// `bus_master`. Music is always on `bus_music`; a sound defaults to `bus_sfx`,
/// changed with sound_set_bus().
enum audio_bus {
  bus_master, ///< Master: multiplies into everything.
  bus_music,  ///< Background music (all music).
  bus_sfx,    ///< In-game effects. The default for sounds.
  bus_ui,     ///< Interface sounds.
  bus_voice,  ///< Voice-over, speech.
  audio_bus_count ///< Number of buses. Not a real bus.
};

/// Sets the volume of a bus, from 0 upward (1 is unchanged). Applies immediately,
/// including to sounds already playing. Saved with the settings by settings_save().
/// @param ctx Engine context.
/// @param bus Bus.
/// @param volume Volume. A negative value is treated as 0.
void audio_set_bus_volume(context &ctx, audio_bus bus, f32 volume);

/// Volume of a bus. @param ctx Engine context. @param bus Bus.
/// @return Volume, default 1.
f32 audio_bus_volume(const context &ctx, audio_bus bus);

/// Mutes or unmutes a whole bus, without touching the volume that was set.
/// @param ctx Engine context.
/// @param bus Bus.
/// @param muted `true` to mute.
void audio_set_bus_muted(context &ctx, audio_bus bus, bool muted);

/// Whether the bus is muted. @param ctx Engine context. @param bus Bus.
/// @return `true` if muted.
bool audio_bus_muted(const context &ctx, audio_bus bus);

/// Moves a sound to another bus, for example `bus_ui` for menu click sounds.
/// @param ctx Engine context.
/// @param handle Sound.
/// @param bus Bus.
void sound_set_bus(context &ctx, sound_handle handle, audio_bus bus);

/// Loads a short sound into memory.
///
/// Use it for effects such as gunshots and clicks. For long background music, use
/// music_load(). An invalid or unloaded handle is ignored by every sound function. If
/// the machine has no audio device, loading fails and is logged, while the game keeps
/// running.
/// @param ctx Engine context.
/// @param path Path of the audio file (wav, ogg, mp3, flac...).
/// @return Handle of the sound, or a handle with id 0 if there is no device, the file
/// is missing or cannot be decoded.
sound_handle sound_load(context &ctx, const char *path);

/// Creates a sound from samples already in memory.
///
/// The data is mono, 32-bit floats in the range -1..1. It is copied into the sound's
/// own buffer and not kept, so your array can be destroyed right after the function
/// returns. Use it for sounds the game generates itself at startup.
/// @param ctx Engine context.
/// @param samples Array of samples.
/// @param count Number of samples.
/// @param sample_rate Sample rate, for example 44100.
/// @return Handle of the sound, or a handle with id 0 if a parameter is wrong or creation fails.
sound_handle sound_load_samples(context &ctx, const f32 *samples, i32 count,
                                i32 sample_rate);

/// Frees the sound. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Sound to free.
void sound_unload(context &ctx, sound_handle handle);

/// Sets the sound's volume, from 0 upward (1 is the original volume).
///
/// Applies immediately, including to instances already playing. Default is 1. A
/// negative value is treated as 0.
/// @param ctx Engine context.
/// @param handle Sound to set.
/// @param volume Volume.
void sound_set_volume(context &ctx, sound_handle handle, f32 volume);

/// Mutes or unmutes the sound.
///
/// Muting brings the playing volume to 0 without touching the stored volume, so
/// unmuting restores it exactly as it was.
/// @param ctx Engine context.
/// @param handle Sound to set.
/// @param muted `true` to mute.
void sound_set_muted(context &ctx, sound_handle handle, bool muted);

/// Plays the sound without cutting off instances that are already playing.
///
/// Called repeatedly, instances overlap, up to 8 at once. Beyond that, the
/// longest-running instance is cut to make room. Suits sounds that many things trigger
/// together, so a crowd is heard as a crowd.
/// @param ctx Engine context.
/// @param handle Sound to play.
void sound_play_once(context &ctx, sound_handle handle);

/// Like sound_play_once() but adjusts pitch and loudness for this instance only.
///
/// `pitch` 1 is as recorded, 2 is twice as high. `gain` multiplies the sound's volume
/// for this instance. Neither is stored: a later plain play returns to 1 and 1. Suits
/// one sound used for many objects of different sizes: one recording for all, and the
/// bigger object sounds louder and deeper.
/// @param ctx Engine context.
/// @param handle Sound to play.
/// @param pitch Pitch. A very small value is raised to a minimum.
/// @param gain Volume factor for this instance. A negative value is treated as 0.
void sound_play_once_at(context &ctx, sound_handle handle, f32 pitch, f32 gain);

/// Cuts every instance that is playing and plays again from the start, so only one
/// instance is ever heard.
///
/// Suits interface clicks or warning sounds: when pressed in quick succession, each
/// press is heard clearly instead of piling into a heap.
/// @param ctx Engine context.
/// @param handle Sound to play.
void sound_play_restart(context &ctx, sound_handle handle);

/// Marks the sound as looping and plays it if it is not already playing.
///
/// The engine's audio module replays the sound whenever it sees it just finished, so
/// this is the only call needed to keep it running. Because it is replayed after it
/// ends, the seam has a gap of silence about one frame long. For long background
/// music, use music_load() to avoid the gap. Call sound_stop() to stop.
/// @param ctx Engine context.
/// @param handle Sound to loop.
void sound_play_loop(context &ctx, sound_handle handle);

/// Plays the sound like sound_play_once(), with a position in the world.
///
/// Volume falls off with distance from the point the camera is looking at, and the
/// sound pans to the left or right speaker according to its position on the screen.
/// See audio_set_range().
/// @param ctx Engine context.
/// @param handle Sound to play.
/// @param world_pos Where the sound comes from, in the world.
void sound_play_at(context &ctx, sound_handle handle, vec2 world_pos);

/// Hearing distance of sound_play_at().
///
/// Closer than `full_until` it is heard at full volume, farther than `silent_from` it
/// is silent, and in between it fades evenly. Defaults are 200 and 1200 world units.
/// @param ctx Engine context.
/// @param full_until Distance where fading starts.
/// @param silent_from Distance where it becomes fully silent. If smaller than
/// `full_until`, it is raised to `full_until`.
void audio_set_range(context &ctx, f32 full_until, f32 silent_from);

/// Stops every playing instance of the sound and clears looping.
/// @param ctx Engine context.
/// @param handle Sound to stop.
void sound_stop(context &ctx, sound_handle handle);
/// @}

/// @addtogroup grp_music
/// @{

/// Loads a piece of music to stream from disk.
///
/// The music is not held entirely in memory but decoded in chunks, which suits
/// background music or long ambient sound. It loops inside the decoder, so there is no
/// gap of silence at the seam. The engine's audio module feeds the stream every frame.
/// @param ctx Engine context.
/// @param path Path of the music file (ogg, mp3, wav, flac...).
/// @return Handle of the music, or a handle with id 0 if there is no device, the file
/// is missing or cannot be decoded.
music_handle music_load(context &ctx, const char *path);

/// Frees the music. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Music to free.
void music_unload(context &ctx, music_handle handle);

/// Sets the music's volume, from 0 upward (1 is the original volume). Default is 1.
/// @param ctx Engine context.
/// @param handle Music to set.
/// @param volume Volume. A negative value is treated as 0.
void music_set_volume(context &ctx, music_handle handle, f32 volume);

/// Mutes or unmutes the music, without touching the stored volume.
/// @param ctx Engine context.
/// @param handle Music to set.
/// @param muted `true` to mute.
void music_set_muted(context &ctx, music_handle handle, bool muted);

/// Turns looping on or off. Default is on. Takes effect even mid-track.
/// @param ctx Engine context.
/// @param handle Music to set.
/// @param looping `true` to loop.
void music_set_looping(context &ctx, music_handle handle, bool looping);

/// Plays from the start, even if paused or already playing.
/// @param ctx Engine context.
/// @param handle Music to play.
void music_play(context &ctx, music_handle handle);

/// Stops and returns the position to the start of the track.
/// @param ctx Engine context.
/// @param handle Music to stop.
void music_stop(context &ctx, music_handle handle);

/// Pauses but keeps the position. Use music_resume() to continue.
/// @param ctx Engine context.
/// @param handle Music to pause.
void music_pause(context &ctx, music_handle handle);

/// Continues from where music_pause() stopped.
/// @param ctx Engine context.
/// @param handle Music to resume.
void music_resume(context &ctx, music_handle handle);

/// Whether the music is playing (not counting while paused).
/// @param ctx Engine context.
/// @param handle Music.
/// @return `true` if playing.
bool music_playing(context &ctx, music_handle handle);

/// Plays from the start, fading in from silence over `seconds` seconds. If already
/// playing, it only fades up to full level from the current level.
/// @param ctx Engine context.
/// @param handle Music.
/// @param seconds Fade-in time, in seconds (real time).
void music_fade_in(context &ctx, music_handle handle, f32 seconds);

/// Fades out, then stops.
/// @param ctx Engine context.
/// @param handle Music.
/// @param seconds Fade-out time, in seconds (real time).
void music_fade_out(context &ctx, music_handle handle, f32 seconds);

/// Switches music: every other music that is playing fades out and stops, while
/// `handle` fades in. Call it when entering a new level, or when meeting a boss. If
/// `handle` is already playing it continues, and is not replayed from the start.
/// @code
/// njin::music_crossfade(ctx, g.boss_theme, 1.5f);
/// @endcode
/// @param ctx Engine context.
/// @param handle Music to switch to. With a handle of id 0, all music just fades out.
/// @param seconds Switch time, in seconds (real time).
void music_crossfade(context &ctx, music_handle handle, f32 seconds);
/// @}

/// @addtogroup grp_sound3d
/// @{

/// The ears of the 3D world: every 3D sound is heard from here.
///
/// By default it follows the camera of the last begin_3d() drawn to the screen
/// (position, facing, up), and its velocity is measured from how far the camera
/// moves each frame. Set it by hand with audio_set_listener3d() when the ears are
/// not at the camera (a third-person view that should hear from the character).
struct audio_listener3d {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Position of the ears.
  vec3 forward{0.0f, 0.0f, -1.0f}; ///< Facing. Need not be of length 1.
  vec3 up{0.0f, 1.0f, 0.0f};       ///< Up direction. Need not be of length 1.
  vec3 velocity{0.0f, 0.0f, 0.0f}; ///< Velocity, units per second, for the Doppler effect.
};

/// Sets the listener by hand. From now on it stops following the camera, until
/// audio_listener3d_follow_camera() is called. Non-finite values (NaN, infinity) are ignored.
/// @param ctx Engine context.
/// @param listener The listener. A velocity of 0 gives no Doppler from the listener moving.
void audio_set_listener3d(context &ctx, const audio_listener3d &listener);

/// Makes the listener follow the begin_3d() camera (the default), or stop
/// following it and stay where it is.
/// @param ctx Engine context.
/// @param follow `true` to follow the camera.
void audio_listener3d_follow_camera(context &ctx, bool follow = true);

/// The listener in use, also while it follows the camera.
/// @param ctx Engine context.
/// @return The listener.
audio_listener3d audio_listener3d_get(const context &ctx);

/// The speed of sound, world units per second, for the Doppler effect. Default 343
/// (metres per second, when one unit is one metre). Lower makes Doppler stronger.
/// @param ctx Engine context.
/// @param units_per_second Speed. Ignored when not positive.
void audio_set_speed_of_sound(context &ctx, f32 units_per_second);

/// How volume falls with distance, from `min_distance` (full) to
/// `max_distance` (silent).
enum audio_rolloff {
  /// Falls as 1/distance, as in real life: fast up close, slow far away. Over the
  /// last 10% before `max_distance` it fades to 0 so it does not cut off. The default.
  rolloff_inverse,
  /// Falls evenly from 1 at `min_distance` to 0 at `max_distance`. Predictable, for
  /// games that need to know exactly how far a sound is heard.
  rolloff_linear,
  /// Falls as a power of distance, `(d / min_distance)^-rolloff_factor`: dies off
  /// faster than inverse. Fades out over the last 10% like inverse.
  rolloff_exponential,
};

/// How a 3D sound is heard. Every field can be changed while the sound plays,
/// with voice3d_set_desc().
struct sound3d_desc {
  f32 volume = 1.0f; ///< Multiplies the sound's volume (and its bus). Negative is 0.
  f32 pitch = 1.0f;  ///< Pitch before Doppler. 1 is as recorded.
  f32 min_distance = 1.0f;  ///< Closer than this is full volume.
  f32 max_distance = 40.0f; ///< Farther than this is silent (and costs nothing to mix).
  audio_rolloff rolloff = rolloff_inverse; ///< How it falls off with distance.
  /// Steepness of `rolloff_inverse` and `rolloff_exponential`. 1 is real life,
  /// 2 dies off faster, 0.5 carries farther.
  f32 rolloff_factor = 1.0f;
  /// How far left and right go, 0..1. 1 puts a sound on the right fully in the
  /// right speaker; 0 keeps it centred (only volume changes). Closer than
  /// `min_distance` it drifts back to the centre, so a sound right overhead does
  /// not jump from one speaker to the other.
  f32 spread = 1.0f;
  /// Strength of the Doppler effect: higher as the source and listener close in,
  /// lower as they move apart. 0 is off, 1 is real life.
  f32 doppler = 1.0f;
  /// Direction a directional speaker plays toward (a car horn, a megaphone), in
  /// the world. `{0, 0, 0}` (the default) plays equally in every direction and the
  /// `cone_*` fields are ignored.
  vec3 cone_direction{0.0f, 0.0f, 0.0f};
  f32 cone_inner = 360.0f; ///< Full angle (degrees) of the full-volume zone around `cone_direction`.
  f32 cone_outer = 360.0f; ///< Full angle (degrees) outside of which only `cone_outer_volume` remains.
  f32 cone_outer_volume = 0.0f; ///< Volume factor outside `cone_outer`, 0..1.
  /// Occlusion: every frame a physics ray (physics3d_raycast()) is cast from the
  /// listener to the source; if it hits a body before the source the sound drops
  /// to `occlusion_volume`, easing over about 0.15 seconds. Only the volume
  /// changes, the sound is not muffled. With no physics world nothing is ever
  /// occluded.
  bool occlusion = false;
  f32 occlusion_volume = 0.35f; ///< Volume factor when occluded, 0..1.
  /// A hit within this distance of the source does not count as occlusion: the
  /// body of the thing making the sound (a running car's chassis) does not block
  /// its own sound. Raise it for large things.
  f32 occlusion_margin = 0.5f;
};

/// Plays `handle` once at `position`, heard from the 3D listener as `desc` says.
///
/// Each 3D sound has its own voice, so its volume, pan and pitch are recomputed
/// every frame from its position and the listener's. The final volume also
/// multiplies the sound's volume and its bus (sound_set_bus(),
/// audio_set_bus_volume()). At most 64 3D sounds at once; past that the one-shot
/// that has played longest is cut. A non-finite position (NaN, infinity) is not played.
/// @param ctx Engine context.
/// @param handle A loaded sound.
/// @param position Where it plays, in the world.
/// @param desc How it is heard.
/// @return Handle of the sound, or a handle of id 0 if it could not be played.
voice3d_handle sound_play3d(context &ctx, sound_handle handle, vec3 position, const sound3d_desc &desc = {});

/// Like the one above, but the sound follows `entity` (the position of its
/// njin::transform3d) until it finishes. If the entity is destroyed the sound
/// plays out where it last was.
/// @param ctx Engine context.
/// @param handle A loaded sound.
/// @param entity An entity with njin::transform3d.
/// @param desc How it is heard.
/// @return Handle of the sound, or a handle of id 0 if it could not be played.
voice3d_handle sound_play3d(context &ctx, sound_handle handle, entt::entity entity, const sound3d_desc &desc = {});

/// Plays `handle` in a loop at `position` until voice3d_stop() (or sound_stop()
/// on that sound): an engine, a crackling fire, a waterfall. As with
/// sound_play_loop(), the join has a gap of about one frame.
/// @param ctx Engine context.
/// @param handle A loaded sound.
/// @param position Where it plays, in the world.
/// @param desc How it is heard.
/// @return Handle of the sound, or a handle of id 0 if it could not be played.
voice3d_handle sound_loop3d(context &ctx, sound_handle handle, vec3 position, const sound3d_desc &desc = {});

/// Like the one above, but the sound follows `entity`: an engine on a car. If the
/// entity is destroyed the loop stops.
/// @param ctx Engine context.
/// @param handle A loaded sound.
/// @param entity An entity with njin::transform3d.
/// @param desc How it is heard.
/// @return Handle of the sound, or a handle of id 0 if it could not be played.
voice3d_handle sound_loop3d(context &ctx, sound_handle handle, entt::entity entity, const sound3d_desc &desc = {});

/// Moves the sound to `position`, and stops it following an entity if it was.
/// Ignored when not finite.
/// @param ctx Engine context.
/// @param voice The sound.
/// @param position New position, in the world.
void voice3d_set_position(context &ctx, voice3d_handle voice, vec3 position);

/// Makes the sound follow `entity`, `offset` away from its position (in the
/// world, not turned with the entity).
/// @param ctx Engine context.
/// @param voice The sound.
/// @param entity An entity with njin::transform3d.
/// @param offset Offset.
void voice3d_attach(context &ctx, voice3d_handle voice, entt::entity entity, vec3 offset = {});

/// Sets the source's velocity for Doppler. By default the engine measures it from
/// how far the source moves each frame; after this call the engine stops measuring
/// and uses this value (taken from body3d_velocity(), say) until the sound stops.
/// @param ctx Engine context.
/// @param voice The sound.
/// @param velocity Velocity, units per second.
void voice3d_set_velocity(context &ctx, voice3d_handle voice, vec3 velocity);

/// Changes how a playing sound is heard: volume from a car's throttle, pitch from
/// its engine speed (vehicle3d_rpm()).
/// @param ctx Engine context.
/// @param voice The sound.
/// @param desc New way of hearing it.
void voice3d_set_desc(context &ctx, voice3d_handle voice, const sound3d_desc &desc);

/// How the sound is heard now. Edit a copy then voice3d_set_desc().
/// @param ctx Engine context.
/// @param voice The sound.
/// @return The desc, or the default one if the handle is invalid.
sound3d_desc voice3d_desc(const context &ctx, voice3d_handle voice);

/// Stops the sound. The handle is no longer valid.
/// @param ctx Engine context.
/// @param voice The sound.
void voice3d_stop(context &ctx, voice3d_handle voice);

/// Whether the sound is still playing (a loop: until it is stopped).
/// @param ctx Engine context.
/// @param voice The sound.
/// @return `true` if it is playing.
bool voice3d_playing(const context &ctx, voice3d_handle voice);

/// What the engine computed for a 3D sound at its last update, for debugging or
/// to draw a sound indicator on screen.
struct voice3d_mix {
  f32 volume = 0.0f;   ///< Volume playing now: sound, bus, distance, cone and occlusion multiplied.
  f32 pan = 0.0f;      ///< Left and right, -1 (left) .. 1 (right).
  f32 pitch = 1.0f;    ///< Pitch playing now, Doppler included.
  f32 distance = 0.0f; ///< Distance to the listener.
  bool occluded = false; ///< Whether the ray from the listener to the source is blocked.
};

/// Volume, pan and pitch of the sound at its last update.
/// @param ctx Engine context.
/// @param voice The sound.
/// @return The result, or the default one if the handle is invalid.
voice3d_mix voice3d_state(const context &ctx, voice3d_handle voice);
/// @}
} // namespace njin
