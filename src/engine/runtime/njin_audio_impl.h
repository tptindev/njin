#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_audio.h"
#include <raylib.h>
#include <vector>

namespace njin {
// Same rules as the other stores (njin_texture.h): handle id 0 is "invalid",
// id N maps to slots[N - 1], and slots are never reused, so a stale handle can
// never alias a newer resource.

// Ceiling on how many copies of one sound may overlap, counting the slot's own
// Sound as the first voice. Each extra voice is an alias costing an
// AudioBuffer and a converter, so this bounds what one noisy sound can spend.
inline constexpr usize sound_max_voices = 8;

struct sound_slot {
  bool alive = false;
  bool muted = false;
  bool looping = false;
  f32 volume = 1.0f;
  audio_bus bus = bus_sfx;
  usize next_voice = 0; // round-robin cursor, only read once the pool is full
  Sound sound{};        // owns the sample data; the voice a loop plays on
  // Aliases of `sound`, grown on demand by play_once. They share its sample
  // data and do not own it, so they must be unloaded before it is.
  std::vector<Sound> voices;
};

// Streamed rather than held in memory: it decodes a buffer at a time and needs
// audio_store_update every frame. In exchange raylib loops it inside the
// decoder, with none of the frame-sized gap a looping Sound leaves.
struct music_slot {
  bool alive = false;
  bool muted = false;
  f32 volume = 1.0f;
  Music music{};
  // Fade: `fade` moves toward `fade_target` at `fade_speed` per second, and
  // the stream stops when it reaches 0 with `stop_at_end`.
  f32 fade = 1.0f;
  f32 fade_target = 1.0f;
  f32 fade_speed = 0.0f;
  bool stop_at_end = false;
};

// Owns every sound and music stream. The destructor frees them, so it must run
// while the audio device is still open (before CloseAudioDevice).
struct audio_store {
  std::vector<sound_slot> sounds;
  std::vector<music_slot> musics;
  // sound_play_at: full volume within range_near, silent past range_far.
  f32 range_near = 200.0f;
  f32 range_far = 1200.0f;
  // Mixer: every sound's volume is multiplied by its bus and by master.
  f32 bus_volume[audio_bus_count] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  bool bus_muted[audio_bus_count] = {};

  audio_store() = default;
  ~audio_store();
  // Copying would free the same audio buffers twice.
  audio_store(const audio_store &) = delete;
  audio_store &operator=(const audio_store &) = delete;
};

inline sound_slot *sound_slot_of(audio_store &store, sound_handle handle) {
  if (handle.id == 0 || handle.id > store.sounds.size())
    return nullptr;
  sound_slot &slot = store.sounds[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

inline music_slot *music_slot_of(audio_store &store, music_handle handle) {
  if (handle.id == 0 || handle.id > store.musics.size())
    return nullptr;
  music_slot &slot = store.musics[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

// Returns an invalid handle if there is no audio device, the file is missing
// or it cannot be decoded.
sound_handle sound_store_load(audio_store &store, const char *path);
// Mono, 32-bit float samples at sample_rate. They are converted into the
// sound's own buffer and not kept, so the caller's array may go on return.
sound_handle sound_store_load_samples(audio_store &store, const f32 *samples,
                                      i32 count, i32 sample_rate);
void sound_store_unload(audio_store &store, sound_handle handle);
void sound_store_set_volume(audio_store &store, sound_handle handle, f32 volume);
void sound_store_set_muted(audio_store &store, sound_handle handle, bool muted);
// pan is -1 (left) .. 1 (right), 0 centred.
void sound_store_play_once(audio_store &store, sound_handle handle, f32 pitch,
                           f32 gain, f32 pan = 0.0f);
void sound_store_play_restart(audio_store &store, sound_handle handle);
void sound_store_play_loop(audio_store &store, sound_handle handle);
void sound_store_stop(audio_store &store, sound_handle handle);

// Returns an invalid handle if there is no audio device, the file is missing
// or it cannot be decoded.
music_handle music_store_load(audio_store &store, const char *path);
void music_store_unload(audio_store &store, music_handle handle);
void music_store_set_volume(audio_store &store, music_handle handle, f32 volume);
void music_store_set_muted(audio_store &store, music_handle handle, bool muted);
void music_store_set_looping(audio_store &store, music_handle handle,
                             bool looping);
void music_store_play(audio_store &store, music_handle handle);
void music_store_stop(audio_store &store, music_handle handle);
void music_store_pause(audio_store &store, music_handle handle);
void music_store_resume(audio_store &store, music_handle handle);

void sound_store_set_bus(audio_store &store, sound_handle handle, audio_bus bus);
void audio_store_set_bus_volume(audio_store &store, audio_bus bus, f32 volume);
void audio_store_set_bus_muted(audio_store &store, audio_bus bus, bool muted);
// Starts a fade of `handle` toward `target` (0..1) over `seconds`. With `play`
// a stopped stream starts from silence; with `stop` it stops at 0.
void music_store_fade(audio_store &store, music_handle handle, f32 target, f32 seconds, bool play,
                      bool stop);
bool music_store_playing(audio_store &store, music_handle handle);

// Once per frame: restarts looping sounds that have ended, moves music fades
// on by `dt_real`, and feeds every music stream. UpdateMusicStream returns at
// once for a stream that is not playing, so stopped and paused music costs
// nothing here.
void audio_store_update(audio_store &store, f32 dt_real);
} // namespace njin
