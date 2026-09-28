#pragma once
#include "_types.h"

namespace njin {
// Opaque, see njin_ctx.h.
struct njin_ctx;

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
void audio_set_bus_volume(njin_ctx &ctx, audio_bus bus, f32 volume);

/// Volume of a bus. @param ctx Engine context. @param bus Bus.
/// @return Volume, default 1.
f32 audio_bus_volume(const njin_ctx &ctx, audio_bus bus);

/// Mutes or unmutes a whole bus, without touching the volume that was set.
/// @param ctx Engine context.
/// @param bus Bus.
/// @param muted `true` to mute.
void audio_set_bus_muted(njin_ctx &ctx, audio_bus bus, bool muted);

/// Whether the bus is muted. @param ctx Engine context. @param bus Bus.
/// @return `true` if muted.
bool audio_bus_muted(const njin_ctx &ctx, audio_bus bus);

/// Moves a sound to another bus, for example `bus_ui` for menu click sounds.
/// @param ctx Engine context.
/// @param handle Sound.
/// @param bus Bus.
void sound_set_bus(njin_ctx &ctx, sound_handle handle, audio_bus bus);

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
sound_handle sound_load(njin_ctx &ctx, const char *path);

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
sound_handle sound_load_samples(njin_ctx &ctx, const f32 *samples, i32 count,
                                i32 sample_rate);

/// Frees the sound. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Sound to free.
void sound_unload(njin_ctx &ctx, sound_handle handle);

/// Sets the sound's volume, from 0 upward (1 is the original volume).
///
/// Applies immediately, including to instances already playing. Default is 1. A
/// negative value is treated as 0.
/// @param ctx Engine context.
/// @param handle Sound to set.
/// @param volume Volume.
void sound_set_volume(njin_ctx &ctx, sound_handle handle, f32 volume);

/// Mutes or unmutes the sound.
///
/// Muting brings the playing volume to 0 without touching the stored volume, so
/// unmuting restores it exactly as it was.
/// @param ctx Engine context.
/// @param handle Sound to set.
/// @param muted `true` to mute.
void sound_set_muted(njin_ctx &ctx, sound_handle handle, bool muted);

/// Plays the sound without cutting off instances that are already playing.
///
/// Called repeatedly, instances overlap, up to 8 at once. Beyond that, the
/// longest-running instance is cut to make room. Suits sounds that many things trigger
/// together, so a crowd is heard as a crowd.
/// @param ctx Engine context.
/// @param handle Sound to play.
void sound_play_once(njin_ctx &ctx, sound_handle handle);

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
void sound_play_once_at(njin_ctx &ctx, sound_handle handle, f32 pitch, f32 gain);

/// Cuts every instance that is playing and plays again from the start, so only one
/// instance is ever heard.
///
/// Suits interface clicks or warning sounds: when pressed in quick succession, each
/// press is heard clearly instead of piling into a heap.
/// @param ctx Engine context.
/// @param handle Sound to play.
void sound_play_restart(njin_ctx &ctx, sound_handle handle);

/// Marks the sound as looping and plays it if it is not already playing.
///
/// The engine's audio module replays the sound whenever it sees it just finished, so
/// this is the only call needed to keep it running. Because it is replayed after it
/// ends, the seam has a gap of silence about one frame long. For long background
/// music, use music_load() to avoid the gap. Call sound_stop() to stop.
/// @param ctx Engine context.
/// @param handle Sound to loop.
void sound_play_loop(njin_ctx &ctx, sound_handle handle);

/// Plays the sound like sound_play_once(), with a position in the world.
///
/// Volume falls off with distance from the point the camera is looking at, and the
/// sound pans to the left or right speaker according to its position on the screen.
/// See audio_set_range().
/// @param ctx Engine context.
/// @param handle Sound to play.
/// @param world_pos Where the sound comes from, in the world.
void sound_play_at(njin_ctx &ctx, sound_handle handle, vec2 world_pos);

/// Hearing distance of sound_play_at().
///
/// Closer than `full_until` it is heard at full volume, farther than `silent_from` it
/// is silent, and in between it fades evenly. Defaults are 200 and 1200 world units.
/// @param ctx Engine context.
/// @param full_until Distance where fading starts.
/// @param silent_from Distance where it becomes fully silent. If smaller than
/// `full_until`, it is raised to `full_until`.
void audio_set_range(njin_ctx &ctx, f32 full_until, f32 silent_from);

/// Stops every playing instance of the sound and clears looping.
/// @param ctx Engine context.
/// @param handle Sound to stop.
void sound_stop(njin_ctx &ctx, sound_handle handle);
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
music_handle music_load(njin_ctx &ctx, const char *path);

/// Frees the music. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Music to free.
void music_unload(njin_ctx &ctx, music_handle handle);

/// Sets the music's volume, from 0 upward (1 is the original volume). Default is 1.
/// @param ctx Engine context.
/// @param handle Music to set.
/// @param volume Volume. A negative value is treated as 0.
void music_set_volume(njin_ctx &ctx, music_handle handle, f32 volume);

/// Mutes or unmutes the music, without touching the stored volume.
/// @param ctx Engine context.
/// @param handle Music to set.
/// @param muted `true` to mute.
void music_set_muted(njin_ctx &ctx, music_handle handle, bool muted);

/// Turns looping on or off. Default is on. Takes effect even mid-track.
/// @param ctx Engine context.
/// @param handle Music to set.
/// @param looping `true` to loop.
void music_set_looping(njin_ctx &ctx, music_handle handle, bool looping);

/// Plays from the start, even if paused or already playing.
/// @param ctx Engine context.
/// @param handle Music to play.
void music_play(njin_ctx &ctx, music_handle handle);

/// Stops and returns the position to the start of the track.
/// @param ctx Engine context.
/// @param handle Music to stop.
void music_stop(njin_ctx &ctx, music_handle handle);

/// Pauses but keeps the position. Use music_resume() to continue.
/// @param ctx Engine context.
/// @param handle Music to pause.
void music_pause(njin_ctx &ctx, music_handle handle);

/// Continues from where music_pause() stopped.
/// @param ctx Engine context.
/// @param handle Music to resume.
void music_resume(njin_ctx &ctx, music_handle handle);

/// Whether the music is playing (not counting while paused).
/// @param ctx Engine context.
/// @param handle Music.
/// @return `true` if playing.
bool music_playing(njin_ctx &ctx, music_handle handle);

/// Plays from the start, fading in from silence over `seconds` seconds. If already
/// playing, it only fades up to full level from the current level.
/// @param ctx Engine context.
/// @param handle Music.
/// @param seconds Fade-in time, in seconds (real time).
void music_fade_in(njin_ctx &ctx, music_handle handle, f32 seconds);

/// Fades out, then stops.
/// @param ctx Engine context.
/// @param handle Music.
/// @param seconds Fade-out time, in seconds (real time).
void music_fade_out(njin_ctx &ctx, music_handle handle, f32 seconds);

/// Switches music: every other music that is playing fades out and stops, while
/// `handle` fades in. Call it when entering a new level, or when meeting a boss. If
/// `handle` is already playing it continues, and is not replayed from the start.
/// @code
/// njin::music_crossfade(ctx, g.boss_theme, 1.5f);
/// @endcode
/// @param ctx Engine context.
/// @param handle Music to switch to. With a handle of id 0, all music just fades out.
/// @param seconds Switch time, in seconds (real time).
void music_crossfade(njin_ctx &ctx, music_handle handle, f32 seconds);
/// @}
} // namespace njin
