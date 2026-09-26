#include "njin_audio_impl.h"
#include "njin_log.h"
#include "njin_path.h"
#include <algorithm>
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
  if (slot != nullptr)
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
