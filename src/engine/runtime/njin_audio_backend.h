#pragma once
#include "njin_internal_only.h"
// Sound output on miniaudio, without raylib (docs/renderer-legacy-removal.md, step 3).
// It mixes as raylib's raudio did, so games sound the same: a float stereo device at the
// device's own rate; sounds decoded once into that format; every voice through its own
// miniaudio data converter whose output rate is the device rate over the pitch; volume
// and raudio's pan law (0.5 x (3 - x^2) on each side) when adding into the output, with no
// clipping in between. Music decodes from memory as it plays and loops without a gap; a
// stream (video sound) queues pushed frames, two buffers deep like raylib's AudioStream.
// Every call is from the game thread; the device callback takes the same lock.
namespace njin {
struct audio_voice_state;
// One playing thing: a sound (or an alias sharing its samples), a music stream or a
// pushed stream. Null when it could not be made.
struct audio_voice {
  audio_voice_state *v = nullptr;
  explicit operator bool() const { return v != nullptr; }
};

bool audio_device_open();
void audio_device_close();
bool audio_device_ready();
// Tests: a device that is not there, mixed on demand at `rate` by audio_render.
bool audio_device_open_offline(unsigned rate);
void audio_render(float *stereo, unsigned frames);

// wav, ogg, mp3, flac (by content). Null with a logged reason on failure.
audio_voice audio_sound_load(const char *path);
// Mono float samples at `rate`; the caller keeps the samples.
audio_voice audio_sound_samples(const float *samples, int count, int rate);
// Another voice over `source`'s samples (raylib's LoadSoundAlias).
audio_voice audio_sound_alias(audio_voice source);
// Looping by default, as raylib's LoadMusicStream.
audio_voice audio_music_load(const char *path);
// Pushed interleaved stereo float frames at `rate`, `buffer_frames` per buffer.
audio_voice audio_stream_open(int rate, int buffer_frames);
// Room for a buffer of frames (raylib's IsAudioStreamProcessed).
bool audio_stream_wants(audio_voice stream);
void audio_stream_push(audio_voice stream, const float *frames, int count);

// Stops it and frees it; an alias must go before the sound whose samples it shares.
void audio_voice_free(audio_voice &voice);
// From the start (sounds and music), or running (streams).
void audio_voice_play(audio_voice voice);
void audio_voice_stop(audio_voice voice); // and rewind
void audio_voice_pause(audio_voice voice);
void audio_voice_resume(audio_voice voice);
bool audio_voice_playing(audio_voice voice); // playing and not paused
void audio_voice_set_volume(audio_voice voice, float volume);
void audio_voice_set_pitch(audio_voice voice, float pitch); // > 0
void audio_voice_set_pan(audio_voice voice, float pan);     // -1 left .. 1 right
void audio_voice_set_looping(audio_voice voice, bool looping);
// For the inspector: the bytes it holds (a sound's samples, which its aliases share;
// the music file) and its length in device-rate frames (0 when unknown).
unsigned long long audio_voice_bytes(audio_voice voice);
unsigned long long audio_voice_frames(audio_voice voice);
} // namespace njin
