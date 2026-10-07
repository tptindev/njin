#include "njin_audio_impl.h"
#include "_math.h"
#include "njin_log.h"
#include "njin_path.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace njin {
namespace {
// What a bus multiplies its sounds by, master included.
f32 bus_gain(const audio_store &store, audio_bus bus) {
  const auto one = [&](audio_bus b) { return store.bus_muted[b] ? 0.0f : store.bus_volume[b]; };
  return bus == bus_master ? one(bus_master) : one(bus) * one(bus_master);
}

// The volume every voice of this slot should be playing at.
f32 effective_volume(const audio_store &store, const sound_slot &slot) {
  return slot.muted ? 0.0f : slot.volume * bus_gain(store, slot.bus);
}

f32 effective_volume(const audio_store &store, const music_slot &slot) {
  return slot.muted ? 0.0f : slot.volume * slot.fade * bus_gain(store, bus_music);
}

// Releases every voice in the right order: the aliases point into `sound`'s
// sample data, so unloading `sound` first would leave them reading freed
// memory.
void release(sound_slot &slot) {
  for (Sound &voice : slot.voices)
    UnloadSoundAlias(voice);
  if (IsSoundValid(slot.sound))
    UnloadSound(slot.sound);
  slot = sound_slot{};
}

void release(music_slot &slot) {
  if (IsMusicValid(slot.music))
    UnloadMusicStream(slot.music); // stops the stream itself when playing
  slot = music_slot{};
}

// Each alias carries its own AudioBuffer with its own volume, so a live change
// has to reach all of them or already-playing voices keep the old level.
void apply_volume(const audio_store &store, sound_slot &slot) {
  const f32 volume = effective_volume(store, slot);
  SetSoundVolume(slot.sound, volume);
  for (Sound &voice : slot.voices)
    SetSoundVolume(voice, volume);
}

sound_handle add_sound(audio_store &store, const Sound &sound) {
  store.sounds.push_back(sound_slot{.alive = true, .sound = sound, .voices = {}});
  return sound_handle{.id = (u32)store.sounds.size()};
}

// A voice free to start on: the sound itself when idle, else an idle alias,
// else a fresh alias while the pool has room. Once every voice is busy and the
// pool is capped it steals round-robin, which lands on the one that started
// longest ago. Never null: slot.sound is valid, so there is always a fallback.
Sound *pick_voice(sound_slot &slot) {
  if (!IsSoundPlaying(slot.sound))
    return &slot.sound;
  for (Sound &voice : slot.voices) {
    if (!IsSoundPlaying(voice))
      return &voice;
  }
  // slot.sound is the first voice, so the aliases stop one short of the cap.
  if (slot.voices.size() + 1 < sound_max_voices) {
    // Safe only because slot.sound passed IsSoundValid: LoadSoundAlias reads
    // the source buffer without checking it for null.
    const Sound alias = LoadSoundAlias(slot.sound);
    if (IsSoundValid(alias)) {
      slot.voices.push_back(alias);
      return &slot.voices.back();
    }
  }
  if (slot.voices.empty())
    return &slot.sound;
  Sound *stolen = &slot.voices[slot.next_voice];
  slot.next_voice = (slot.next_voice + 1) % slot.voices.size();
  return stolen;
}

// raylib picks the decoder from the file extension, and a path without one
// reaches a string comparison as null. Reject it here with a readable reason.
bool has_extension(const char *path) {
  if (GetFileExtension(path) != nullptr)
    return true;
  NJIN_WARN("audio: file has no extension, cannot tell its format: %s", path);
  return false;
}

bool device_ready(const char *what) {
  if (IsAudioDeviceReady())
    return true;
  NJIN_WARN("audio: no audio device, cannot load %s", what);
  return false;
}
} // namespace

audio_store::~audio_store() {
  for (voice3d_slot &voice : voices3d) // aliases first: they read the sounds' data
    voice3d_store_release(voice);
  for (sound_slot &slot : sounds)
    release(slot);
  for (music_slot &slot : musics)
    release(slot);
}

// Sounds

sound_handle sound_store_load(audio_store &store, const char *path) {
  if (path == nullptr) {
    NJIN_WARN("sound: path is null");
    return sound_handle{};
  }
  if (!device_ready(path))
    return sound_handle{};
  // raylib only logs a missing file; fail loudly with the path instead.
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("sound: file not found: %s", path);
    return sound_handle{};
  }
  if (!has_extension(resolved.c_str()))
    return sound_handle{};
  const Sound sound = LoadSound(resolved.c_str());
  if (!IsSoundValid(sound)) {
    UnloadSound(sound);
    NJIN_WARN("sound: failed to load: %s", path);
    return sound_handle{};
  }
  return add_sound(store, sound);
}

sound_handle sound_store_load_samples(audio_store &store, const f32 *samples,
                                      i32 count, i32 sample_rate) {
  if (samples == nullptr || count <= 0 || sample_rate <= 0) {
    NJIN_WARN("sound: invalid samples (count=%d, rate=%d)", count, sample_rate);
    return sound_handle{};
  }
  if (!device_ready("samples"))
    return sound_handle{};
  // sampleSize 32 is read as float by LoadSoundFromWave, which converts the
  // frames into a buffer of its own and never touches wave.data again, so the
  // caller keeps ownership and the const is only cast away for the API.
  const Wave wave{.frameCount = (unsigned int)count,
                  .sampleRate = (unsigned int)sample_rate,
                  .sampleSize = 32,
                  .channels = 1,
                  .data = const_cast<void *>(static_cast<const void *>(samples))};
  const Sound sound = LoadSoundFromWave(wave);
  if (!IsSoundValid(sound)) {
    UnloadSound(sound);
    NJIN_WARN("sound: failed to build from %d samples", count);
    return sound_handle{};
  }
  return add_sound(store, sound);
}

void sound_store_unload(audio_store &store, sound_handle handle) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr)
    return;
  voice3d_store_release_sound(store, handle);
  release(*slot);
}

void sound_store_set_volume(audio_store &store, sound_handle handle,
                            f32 volume) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->volume = volume < 0.0f ? 0.0f : volume;
  if (!slot->muted)
    apply_volume(store, *slot);
}

void sound_store_set_muted(audio_store &store, sound_handle handle,
                           bool muted) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->muted = muted;
  apply_volume(store, *slot);
}

// Every one-shot goes through here so pitch is written on every start: a voice
// last played at 0.6 would otherwise carry that into the next plain play,
// since the alias keeps whatever it was set to.
void sound_store_play_once(audio_store &store, sound_handle handle, f32 pitch,
                           f32 gain, f32 pan) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->looping = false;
  Sound *voice = pick_voice(*slot);
  SetSoundVolume(*voice, effective_volume(store, *slot) * (gain < 0.0f ? 0.0f : gain));
  SetSoundPitch(*voice, pitch > 1e-3f ? pitch : 1e-3f);
  // Written on every start for the same reason as pitch.
  SetSoundPan(*voice, pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan));
  PlaySound(*voice);
}

void sound_store_play_restart(audio_store &store, sound_handle handle) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->looping = false;
  // Cut the copies play_once may have spread across the pool, so this call
  // always leaves exactly one instance running.
  for (Sound &voice : slot->voices)
    StopSound(voice);
  SetSoundVolume(slot->sound, effective_volume(store, *slot));
  SetSoundPitch(slot->sound, 1.0f); // a pitched one-shot may have left it off 1
  SetSoundPan(slot->sound, 0.0f);
  // PlaySound rewinds the cursor whether or not the voice was playing, so no
  // StopSound is needed on the base voice first.
  PlaySound(slot->sound);
}

void sound_store_play_loop(audio_store &store, sound_handle handle) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->looping = true;
  if (!IsSoundPlaying(slot->sound)) {
    SetSoundVolume(slot->sound, effective_volume(store, *slot));
    SetSoundPitch(slot->sound, 1.0f);
    SetSoundPan(slot->sound, 0.0f);
    PlaySound(slot->sound);
  }
}

void sound_store_stop(audio_store &store, sound_handle handle) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->looping = false;
  StopSound(slot->sound);
  for (Sound &voice : slot->voices)
    StopSound(voice);
  voice3d_store_release_sound(store, handle);
}

f32 sound_store_effective_volume(const audio_store &store, sound_handle handle) {
  if (handle.id == 0 || handle.id > store.sounds.size())
    return 0.0f;
  const sound_slot &slot = store.sounds[handle.id - 1];
  return slot.alive ? effective_volume(store, slot) : 0.0f;
}

// 3D voices

voice3d_slot *voice3d_slot_of(audio_store &store, voice3d_handle handle) {
  if (handle.id == 0 || handle.id > store.voices3d.size())
    return nullptr;
  voice3d_slot &v = store.voices3d[handle.id - 1];
  return v.alive && v.gen == handle.gen ? &v : nullptr;
}

const voice3d_slot *voice3d_slot_of(const audio_store &store, voice3d_handle handle) {
  if (handle.id == 0 || handle.id > store.voices3d.size())
    return nullptr;
  const voice3d_slot &v = store.voices3d[handle.id - 1];
  return v.alive && v.gen == handle.gen ? &v : nullptr;
}

void voice3d_store_release(voice3d_slot &voice) {
  if (!voice.alive)
    return;
  if (IsSoundValid(voice.alias)) {
    StopSound(voice.alias);
    UnloadSoundAlias(voice.alias);
  }
  const u32 gen = voice.gen;
  voice = voice3d_slot{};
  voice.gen = gen; // the next use bumps it
}

void voice3d_store_release_sound(audio_store &store, sound_handle sound) {
  for (voice3d_slot &voice : store.voices3d)
    if (voice.alive && voice.sound.id == sound.id)
      voice3d_store_release(voice);
}

voice3d_mix voice3d_compute(const audio_listener3d &listener, f32 speed_of_sound, const sound3d_desc &desc,
                            vec3 position, vec3 velocity, f32 base_volume, f32 occlusion) {
  voice3d_mix mix{};
  const vec3 to_source = position - listener.position;
  const f32 dist = length(to_source);
  mix.distance = dist;
  const vec3 dir = dist > 1e-5f ? to_source / dist : vec3{0.0f, 0.0f, 0.0f};

  const f32 lo = std::max(desc.min_distance, 1e-3f);
  const f32 hi = std::max(desc.max_distance, lo + 1e-3f);
  const f32 rf = std::max(desc.rolloff_factor, 0.0f);
  f32 att = 0.0f;
  if (dist < hi) {
    const f32 d = std::max(dist, lo);
    switch (desc.rolloff) {
    case rolloff_linear:
      att = clamp(1.0f - rf * (d - lo) / (hi - lo), 0.0f, 1.0f);
      break;
    case rolloff_exponential:
      att = std::pow(d / lo, -rf);
      break;
    case rolloff_inverse:
    default:
      att = lo / (lo + rf * (d - lo));
      break;
    }
    // inverse and exponential never reach 0 on their own: fade the last tenth
    // of the range so a sound walking out of range does not cut off.
    const f32 edge = hi - 0.1f * (hi - lo);
    if (desc.rolloff != rolloff_linear && d > edge)
      att *= (hi - d) / (hi - edge);
  }

  f32 cone = 1.0f;
  if (length_sq(desc.cone_direction) > 0.0f && dist > 1e-5f) {
    const f32 c = clamp(dot(normalize(desc.cone_direction), dir * -1.0f), -1.0f, 1.0f);
    const f32 angle = std::acos(c) * (180.0f / 3.14159265f);
    const f32 inner = clamp(desc.cone_inner, 0.0f, 360.0f) * 0.5f;
    const f32 outer = std::max(clamp(desc.cone_outer, 0.0f, 360.0f) * 0.5f, inner);
    const f32 outside = clamp(desc.cone_outer_volume, 0.0f, 1.0f);
    if (angle >= outer)
      cone = outside;
    else if (angle > inner)
      cone = 1.0f + (outside - 1.0f) * (angle - inner) / (outer - inner);
  }

  const vec3 right = normalize(cross(listener.forward, listener.up));
  f32 pan = dot(dir, right) * clamp(desc.spread, 0.0f, 1.0f);
  if (dist < lo)
    pan *= dist / lo; // right over the listener's head: centre, do not flip sides
  mix.pan = clamp(pan, -1.0f, 1.0f);

  f32 doppler = 1.0f;
  if (desc.doppler > 0.0f && dist > 1e-5f && speed_of_sound > 0.0f) {
    // Along the line from the listener to the source: the listener closing in
    // is positive, the source moving away is positive.
    const f32 c = speed_of_sound;
    const f32 vl = clamp(dot(listener.velocity, dir) * desc.doppler, -0.9f * c, 0.9f * c);
    const f32 vs = clamp(dot(velocity, dir) * desc.doppler, -0.9f * c, 0.9f * c);
    doppler = clamp((c + vl) / (c + vs), 0.25f, 4.0f);
  }
  mix.pitch = std::max(desc.pitch, 1e-3f) * doppler;
  mix.volume = base_volume * std::max(desc.volume, 0.0f) * att * cone * clamp(occlusion, 0.0f, 1.0f);
  return mix;
}

void voice3d_store_apply(audio_store &store, voice3d_slot &voice) {
  const bool occluded = voice.mix.occluded;
  voice.mix = voice3d_compute(store.listener, store.speed_of_sound, voice.desc, voice.position, voice.velocity,
                              sound_store_effective_volume(store, voice.sound), voice.occlusion);
  voice.mix.occluded = occluded;
  SetSoundVolume(voice.alias, voice.mix.volume);
  SetSoundPan(voice.alias, voice.mix.pan);
  SetSoundPitch(voice.alias, voice.mix.pitch);
}

voice3d_handle voice3d_store_start(audio_store &store, sound_handle sound, vec3 position, const sound3d_desc &desc,
                                   bool looping) {
  sound_slot *slot = sound_slot_of(store, sound);
  if (slot == nullptr)
    return voice3d_handle{};
  usize alive = 0;
  voice3d_slot *free_slot = nullptr;
  voice3d_slot *oldest = nullptr;
  for (voice3d_slot &v : store.voices3d) {
    if (!v.alive) {
      if (free_slot == nullptr)
        free_slot = &v;
      continue;
    }
    alive++;
    if (!v.looping && (oldest == nullptr || v.serial < oldest->serial))
      oldest = &v;
  }
  if (alive >= voice3d_max) {
    if (oldest == nullptr) {
      static bool told = false;
      if (!told) {
        told = true;
        NJIN_WARN("audio: %zu 3D loops already playing, a new 3D sound is not played", voice3d_max);
      }
      return voice3d_handle{};
    }
    voice3d_store_release(*oldest);
    free_slot = oldest;
  }
  // Safe only because the slot's sound passed IsSoundValid on load: see pick_voice.
  const Sound alias = LoadSoundAlias(slot->sound);
  if (!IsSoundValid(alias))
    return voice3d_handle{};
  if (free_slot == nullptr) {
    store.voices3d.emplace_back();
    free_slot = &store.voices3d.back();
  }
  voice3d_slot &v = *free_slot;
  const u32 gen = v.gen + 1;
  v = voice3d_slot{};
  v.alive = true;
  v.gen = gen;
  v.sound = sound;
  v.alias = alias;
  v.looping = looping;
  v.serial = ++store.voice3d_serial;
  v.desc = desc;
  v.position = position;
  v.last_position = position;
  v.has_last = true;
  voice3d_store_apply(store, v);
  PlaySound(v.alias);
  return voice3d_handle{.id = (u32)(&v - store.voices3d.data()) + 1, .gen = gen};
}

void audio3d_note_camera(audio_store &store, vec3 position, vec3 target, vec3 up) {
  store.camera.position = position;
  store.camera.forward = target - position;
  store.camera.up = up;
  store.camera_seen = true;
}

// Music

music_handle music_store_load(audio_store &store, const char *path) {
  if (path == nullptr) {
    NJIN_WARN("music: path is null");
    return music_handle{};
  }
  if (!device_ready(path))
    return music_handle{};
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("music: file not found: %s", path);
    return music_handle{};
  }
  if (!has_extension(resolved.c_str()))
    return music_handle{};
  const Music music = LoadMusicStream(resolved.c_str());
  if (!IsMusicValid(music)) {
    UnloadMusicStream(music);
    NJIN_WARN("music: failed to load: %s", path);
    return music_handle{};
  }
  // raylib enables looping on load for every format it decodes, which is the
  // right default for a soundtrack; music_set_looping turns it off.
  store.musics.push_back(music_slot{.alive = true, .music = music});
  return music_handle{.id = (u32)store.musics.size()};
}

void music_store_unload(audio_store &store, music_handle handle) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot != nullptr)
    release(*slot);
}

void music_store_set_volume(audio_store &store, music_handle handle,
                            f32 volume) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->volume = volume < 0.0f ? 0.0f : volume;
  SetMusicVolume(slot->music, effective_volume(store, *slot));
}

void music_store_set_muted(audio_store &store, music_handle handle,
                           bool muted) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->muted = muted;
  SetMusicVolume(slot->music, effective_volume(store, *slot));
}

void music_store_set_looping(audio_store &store, music_handle handle,
                             bool looping) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot == nullptr)
    return;
  // UpdateMusicStream reads this every refill, so it takes effect mid-track.
  slot->music.looping = looping;
}

void music_store_play(audio_store &store, music_handle handle) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot == nullptr)
    return;
  // PlayMusicStream only rewinds the audio buffer. The decode position lives
  // in the decoder and only StopMusicStream seeks that back, so without this a
  // play after a pause would restart the buffer while the decoder carried on
  // from where it stopped.
  StopMusicStream(slot->music);
  slot->fade = slot->fade_target = 1.0f; // a plain play cancels any fade
  slot->fade_speed = 0.0f;
  slot->stop_at_end = false;
  SetMusicVolume(slot->music, effective_volume(store, *slot));
  PlayMusicStream(slot->music);
}

void music_store_stop(audio_store &store, music_handle handle) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot != nullptr)
    StopMusicStream(slot->music);
}

void music_store_pause(audio_store &store, music_handle handle) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot != nullptr)
    PauseMusicStream(slot->music);
}

void music_store_resume(audio_store &store, music_handle handle) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot != nullptr)
    ResumeMusicStream(slot->music);
}

void sound_store_set_bus(audio_store &store, sound_handle handle, audio_bus bus) {
  sound_slot *slot = sound_slot_of(store, handle);
  if (slot == nullptr || bus < bus_master || bus >= audio_bus_count)
    return;
  slot->bus = bus;
  apply_volume(store, *slot);
}

namespace {
// A bus changed: every live voice and stream takes the new level now.
void apply_all(audio_store &store) {
  for (sound_slot &slot : store.sounds)
    if (slot.alive)
      apply_volume(store, slot);
  for (music_slot &slot : store.musics)
    if (slot.alive)
      SetMusicVolume(slot.music, effective_volume(store, slot));
}
} // namespace

void audio_store_set_bus_volume(audio_store &store, audio_bus bus, f32 volume) {
  if (bus < bus_master || bus >= audio_bus_count)
    return;
  store.bus_volume[bus] = volume < 0.0f ? 0.0f : volume;
  apply_all(store);
}

void audio_store_set_bus_muted(audio_store &store, audio_bus bus, bool muted) {
  if (bus < bus_master || bus >= audio_bus_count)
    return;
  store.bus_muted[bus] = muted;
  apply_all(store);
}

void music_store_fade(audio_store &store, music_handle handle, f32 target, f32 seconds, bool play,
                      bool stop) {
  music_slot *slot = music_slot_of(store, handle);
  if (slot == nullptr)
    return;
  target = target < 0.0f ? 0.0f : (target > 1.0f ? 1.0f : target);
  if (play && !IsMusicStreamPlaying(slot->music)) {
    StopMusicStream(slot->music);
    slot->fade = 0.0f;
    SetMusicVolume(slot->music, effective_volume(store, *slot));
    PlayMusicStream(slot->music);
  }
  slot->fade_target = target;
  slot->stop_at_end = stop;
  if (seconds <= 0.0f) {
    slot->fade = target;
    slot->fade_speed = 0.0f;
  } else {
    slot->fade_speed = 1.0f / seconds;
  }
  SetMusicVolume(slot->music, effective_volume(store, *slot));
  if (slot->stop_at_end && slot->fade <= 0.0f) {
    StopMusicStream(slot->music);
    slot->stop_at_end = false;
  }
}

bool music_store_playing(audio_store &store, music_handle handle) {
  music_slot *slot = music_slot_of(store, handle);
  return slot != nullptr && IsMusicStreamPlaying(slot->music);
}

void audio_store_update(audio_store &store, f32 dt_real) {
  for (sound_slot &slot : store.sounds) {
    if (slot.alive && slot.looping && !IsSoundPlaying(slot.sound))
      PlaySound(slot.sound);
  }
  for (music_slot &slot : store.musics) {
    if (!slot.alive)
      continue;
    if (slot.fade != slot.fade_target && slot.fade_speed > 0.0f) {
      const f32 step = slot.fade_speed * dt_real;
      slot.fade = slot.fade < slot.fade_target ? std::min(slot.fade + step, slot.fade_target)
                                               : std::max(slot.fade - step, slot.fade_target);
      SetMusicVolume(slot.music, effective_volume(store, slot));
      if (slot.fade <= 0.0f && slot.stop_at_end) {
        StopMusicStream(slot.music);
        slot.stop_at_end = false;
      }
    }
    UpdateMusicStream(slot.music);
  }
}
} // namespace njin
