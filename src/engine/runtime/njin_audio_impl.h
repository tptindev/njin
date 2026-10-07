#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_audio.h"
#include <entt/entity/fwd.hpp>
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

// At most this many 3D voices at once; past it the oldest one-shot is cut.
inline constexpr usize voice3d_max = 64;

// A sound playing in 3D. It has its own alias of the sound's samples, so its
// volume, pan and pitch are its own and are recomputed every frame. The slot
// is reused after the voice ends; `gen` tells an old handle from the new voice.
struct voice3d_slot {
  bool alive = false;
  u32 gen = 0;
  sound_handle sound{};
  Sound alias{}; // shares the sound's sample data; unloaded before the sound
  bool looping = false;
  u64 serial = 0; // start order, to cut the oldest one-shot when full
  sound3d_desc desc{};
  vec3 position{};
  vec3 velocity{};
  vec3 last_position{};
  bool has_last = false;
  bool velocity_set = false; // voice3d_set_velocity: stop measuring it
  bool attached = false;
  entt::entity entity{};
  vec3 offset{};
  f32 occlusion = 1.0f; // eased toward desc.occlusion_volume or 1
  voice3d_mix mix{};
};

// Owns every sound and music stream. The destructor frees them, so it must run
// while the audio device is still open (before CloseAudioDevice).
struct audio_store {
  std::vector<sound_slot> sounds;
  std::vector<music_slot> musics;
  std::vector<voice3d_slot> voices3d;
  u64 voice3d_serial = 0;
  // 3D listener. While `listener_follow`, it is the last on-screen begin_3d
  // camera (audio3d_note_camera), its velocity measured from frame to frame.
  audio_listener3d listener{};
  bool listener_follow = true;
  bool camera_seen = false;
  audio_listener3d camera{}; // velocity unused
  vec3 listener_last{};
  bool listener_has_last = false;
  f32 speed_of_sound = 343.0f;
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

// The volume a voice of this sound plays at before any 3D factor: the sound's
// own volume, its bus and master, 0 when muted. 0 for an invalid handle.
f32 sound_store_effective_volume(const audio_store &store, sound_handle handle);

// 3D voices. start returns an invalid handle when the sound is invalid, the
// alias cannot be made or every voice is a loop. The mix is computed at once
// from the current listener, so the first buffer is already at the right level.
voice3d_handle voice3d_store_start(audio_store &store, sound_handle sound, vec3 position, const sound3d_desc &desc,
                                   bool looping);
voice3d_slot *voice3d_slot_of(audio_store &store, voice3d_handle handle);
const voice3d_slot *voice3d_slot_of(const audio_store &store, voice3d_handle handle);
void voice3d_store_release(voice3d_slot &voice);
// Stops and frees every 3D voice of `sound` (sound_stop, sound_unload).
void voice3d_store_release_sound(audio_store &store, sound_handle sound);
// What the voice should play at, from the listener and its position and
// velocity; `occlusion` is the eased factor. Pure, for the update and tests.
voice3d_mix voice3d_compute(const audio_listener3d &listener, f32 speed_of_sound, const sound3d_desc &desc,
                            vec3 position, vec3 velocity, f32 base_volume, f32 occlusion);
// Recomputes the voice's mix and writes it to its alias.
void voice3d_store_apply(audio_store &store, voice3d_slot &voice);
// begin_3d onto the screen: the camera the listener follows.
void audio3d_note_camera(audio_store &store, vec3 position, vec3 target, vec3 up);

struct context;
// Once per frame, before audio_store_update: moves the listener with the
// camera, follows attached entities, measures velocities, casts occlusion rays,
// writes every 3D voice's volume, pan and pitch, restarts 3D loops and frees
// one-shots that have ended (njin_audio3d.cpp).
void audio3d_update(context &ctx);

// Once per frame: restarts looping sounds that have ended, moves music fades
// on by `dt_real`, and feeds every music stream. UpdateMusicStream returns at
// once for a stream that is not playing, so stopped and paused music costs
// nothing here.
void audio_store_update(audio_store &store, f32 dt_real);
} // namespace njin
