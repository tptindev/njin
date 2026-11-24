#include "njin_audio_backend.h"
#include "njin_image_io.h"
#include "njin_log.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
// Ogg Vorbis through stb_vorbis (miniaudio's extras), as miniaudio documents it.
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace njin {
namespace {
constexpr ma_uint32 channels = 2;
} // namespace

struct audio_voice_state {
  enum kind_t { sound, music, stream } kind = sound;
  // sound: the samples, float stereo at the device rate, shared with its aliases
  std::shared_ptr<const std::vector<float>> pcm;
  size_t cursor = 0; // frames
  // music: the file in memory and its decoder (float stereo at the device rate)
  std::vector<std::uint8_t> file;
  ma_decoder decoder{};
  bool decoder_ready = false;
  // stream: queued frames, two buffers deep
  std::vector<float> ring;
  size_t ring_read = 0, ring_count = 0; // frames
  size_t buffer_frames = 0;
  // every voice: its converter (input rate -> device rate / pitch)
  ma_data_converter converter{};
  ma_uint32 rate_in = 0;
  float volume = 1.0f, pitch = 1.0f, pan = 0.0f;
  bool playing = false, paused = false, looping = false;
};

namespace {
struct device_state {
  ma_context context{};
  ma_device device{};
  bool open = false;
  bool offline = false; // audio_device_open_offline: mixed by audio_render, no device
  ma_uint32 offline_rate = 0;
  std::mutex lock;
  std::vector<audio_voice_state *> voices;
} audio;

ma_uint32 device_rate() { return audio.offline ? audio.offline_rate : audio.device.sampleRate; }

bool init_converter(audio_voice_state &v, ma_uint32 rate_in) {
  ma_data_converter_config config = ma_data_converter_config_init(ma_format_f32, ma_format_f32, channels, channels, rate_in, device_rate());
  config.allowDynamicSampleRate = true; // pitch moves the output rate, as raudio does
  v.rate_in = rate_in;
  return ma_data_converter_init(&config, nullptr, &v.converter) == MA_SUCCESS;
}

// Up to `want` input frames of `v` into `out` (float stereo); fewer at its end.
ma_uint64 read_source(audio_voice_state &v, float *out, ma_uint64 want) {
  switch (v.kind) {
  case audio_voice_state::sound: {
    const size_t total = v.pcm->size() / channels;
    const ma_uint64 take = std::min<ma_uint64>(want, total > v.cursor ? total - v.cursor : 0);
    std::memcpy(out, v.pcm->data() + v.cursor * channels, size_t(take) * channels * sizeof(float));
    v.cursor += size_t(take);
    return take;
  }
  case audio_voice_state::music: {
    ma_uint64 read = 0;
    if (v.decoder_ready) ma_decoder_read_pcm_frames(&v.decoder, out, want, &read);
    return read;
  }
  case audio_voice_state::stream: {
    // An empty queue plays silence and keeps going (a stream never ends by itself).
    const size_t cap = v.ring.size() / channels;
    const ma_uint64 take = std::min<ma_uint64>(want, v.ring_count);
    for (ma_uint64 i = 0; i < take; ++i) {
      const size_t at = (v.ring_read + size_t(i)) % cap;
      out[i * 2] = v.ring[at * 2];
      out[i * 2 + 1] = v.ring[at * 2 + 1];
    }
    v.ring_read = (v.ring_read + size_t(take)) % cap;
    v.ring_count -= size_t(take);
    std::fill(out + take * 2, out + want * 2, 0.0f);
    return want;
  }
  }
  return 0;
}

void rewind(audio_voice_state &v) {
  v.cursor = 0;
  if (v.kind == audio_voice_state::music && v.decoder_ready) ma_decoder_seek_to_pcm_frame(&v.decoder, 0);
  ma_data_converter_reset(&v.converter);
}

// raudio's MixAudioFrames for a stereo device: its pan law, no clipping.
void mix(float *out, const float *in, ma_uint64 frames, const audio_voice_state &v) {
  const float right = (v.pan + 1.0f) / 2.0f, left = 1.0f - right;
  const float levels[2]{v.volume * 0.5f * left * (3.0f - left * left), v.volume * 0.5f * right * (3.0f - right * right)};
  for (ma_uint64 f = 0; f < frames; ++f) {
    out[f * 2] += in[f * 2] * levels[0];
    out[f * 2 + 1] += in[f * 2 + 1] * levels[1];
  }
}

void mix_all(float *out, ma_uint32 frame_count) {
  std::memset(out, 0, size_t(frame_count) * channels * sizeof(float));
  std::lock_guard<std::mutex> guard(audio.lock);
  float converted[512 * channels], source[2048 * channels];
  for (audio_voice_state *v : audio.voices) {
    if (!v->playing || v->paused) continue;
    ma_uint32 done = 0;
    while (done < frame_count && v->playing) {
      ma_uint64 out_frames = std::min<ma_uint32>(frame_count - done, 512);
      ma_uint64 in_frames = 0;
      ma_data_converter_get_required_input_frame_count(&v->converter, out_frames, &in_frames);
      in_frames = std::min<ma_uint64>(in_frames, 2048);
      const ma_uint64 got = read_source(*v, source, in_frames);
      ma_uint64 in_used = got, produced = out_frames;
      ma_data_converter_process_pcm_frames(&v->converter, source, &in_used, converted, &produced);
      mix(out + size_t(done) * channels, converted, produced, *v);
      done += ma_uint32(produced);
      if (got < in_frames) { // the end of a sound or of the music
        if (v->looping) { rewind(*v); continue; }
        v->playing = false;
        rewind(*v);
      } else if (produced == 0 && got == 0) {
        break;
      }
    }
  }
}

void on_device(ma_device *, void *output, const void *, ma_uint32 frame_count) {
  mix_all(static_cast<float *>(output), frame_count);
}

audio_voice_state *track(std::unique_ptr<audio_voice_state> owned) {
  std::lock_guard<std::mutex> guard(audio.lock);
  audio.voices.push_back(owned.get());
  return owned.release();
}
} // namespace

bool audio_device_open() {
  if (audio.open) return true;
  if (ma_context_init(nullptr, 0, nullptr, &audio.context) != MA_SUCCESS) {
    NJIN_WARN("audio: no audio backend could be initialised");
    return false;
  }
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = channels;
  config.sampleRate = 0; // the device's own
  config.dataCallback = on_device;
  config.noPreSilencedOutputBuffer = true; // on_device silences it
  config.noFixedSizedCallback = true;
  if (ma_device_init(&audio.context, &config, &audio.device) != MA_SUCCESS) {
    NJIN_WARN("audio: no playback device");
    ma_context_uninit(&audio.context);
    return false;
  }
  if (ma_device_start(&audio.device) != MA_SUCCESS) {
    NJIN_WARN("audio: the playback device did not start");
    ma_device_uninit(&audio.device);
    ma_context_uninit(&audio.context);
    return false;
  }
  audio.open = true;
  NJIN_INFO("audio: %s, %u Hz", audio.device.playback.name, audio.device.sampleRate);
  return true;
}

void audio_device_close() {
  if (!audio.open) return;
  // The callback stops before anything it reads goes away.
  if (!audio.offline) {
    ma_device_uninit(&audio.device);
    ma_context_uninit(&audio.context);
  }
  audio.open = audio.offline = false;
}

bool audio_device_open_offline(unsigned rate) {
  if (audio.open || rate == 0) return false;
  audio.offline = true;
  audio.offline_rate = rate;
  audio.open = true;
  return true;
}

void audio_render(float *stereo, unsigned frames) {
  if (audio.offline && stereo) mix_all(stereo, frames);
}

bool audio_device_ready() { return audio.open; }

audio_voice audio_sound_load(const char *path) {
  if (!audio.open || !path) return {};
  std::vector<std::uint8_t> bytes;
  if (!image_io_read_file(path, bytes)) return {};
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, channels, device_rate());
  ma_decoder decoder;
  if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS) return {};
  auto pcm = std::make_shared<std::vector<float>>();
  float chunk[4096 * channels];
  for (;;) {
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &read);
    if (read == 0) break;
    pcm->insert(pcm->end(), chunk, chunk + read * channels);
  }
  ma_decoder_uninit(&decoder);
  if (pcm->empty()) return {};
  auto v = std::make_unique<audio_voice_state>();
  v->kind = audio_voice_state::sound;
  v->pcm = std::move(pcm);
  if (!init_converter(*v, device_rate())) return {};
  return {track(std::move(v))};
}

audio_voice audio_sound_samples(const float *samples, int count, int rate) {
  if (!audio.open || !samples || count <= 0 || rate <= 0) return {};
  const ma_uint64 frames = ma_convert_frames(nullptr, 0, ma_format_f32, channels, device_rate(), samples, ma_uint64(count), ma_format_f32, 1, ma_uint32(rate));
  if (frames == 0) return {};
  auto pcm = std::make_shared<std::vector<float>>(size_t(frames) * channels);
  const ma_uint64 made = ma_convert_frames(pcm->data(), frames, ma_format_f32, channels, device_rate(), samples, ma_uint64(count), ma_format_f32, 1, ma_uint32(rate));
  pcm->resize(size_t(made) * channels);
  auto v = std::make_unique<audio_voice_state>();
  v->kind = audio_voice_state::sound;
  v->pcm = std::move(pcm);
  if (!init_converter(*v, device_rate())) return {};
  return {track(std::move(v))};
}

audio_voice audio_sound_alias(audio_voice source) {
  if (!source || source.v->kind != audio_voice_state::sound) return {};
  auto v = std::make_unique<audio_voice_state>();
  v->kind = audio_voice_state::sound;
  v->pcm = source.v->pcm;
  if (!init_converter(*v, device_rate())) return {};
  return {track(std::move(v))};
}

audio_voice audio_music_load(const char *path) {
  if (!audio.open || !path) return {};
  auto v = std::make_unique<audio_voice_state>();
  v->kind = audio_voice_state::music;
  if (!image_io_read_file(path, v->file)) return {};
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, channels, device_rate());
  if (ma_decoder_init_memory(v->file.data(), v->file.size(), &config, &v->decoder) != MA_SUCCESS) return {};
  v->decoder_ready = true;
  v->looping = true;
  if (!init_converter(*v, device_rate())) {
    ma_decoder_uninit(&v->decoder);
    return {};
  }
  return {track(std::move(v))};
}

audio_voice audio_stream_open(int rate, int buffer_frames) {
  if (!audio.open || rate <= 0 || buffer_frames <= 0) return {};
  auto v = std::make_unique<audio_voice_state>();
  v->kind = audio_voice_state::stream;
  v->buffer_frames = size_t(buffer_frames);
  v->ring.assign(size_t(buffer_frames) * 2 * channels, 0.0f);
  if (!init_converter(*v, ma_uint32(rate))) return {};
  return {track(std::move(v))};
}

bool audio_stream_wants(audio_voice stream) {
  if (!stream || stream.v->kind != audio_voice_state::stream) return false;
  std::lock_guard<std::mutex> guard(audio.lock);
  return stream.v->ring_count <= stream.v->buffer_frames;
}

void audio_stream_push(audio_voice stream, const float *frames, int count) {
  if (!stream || stream.v->kind != audio_voice_state::stream || !frames || count <= 0) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  auto &v = *stream.v;
  const size_t cap = v.ring.size() / channels;
  const size_t take = std::min(size_t(count), cap - v.ring_count);
  for (size_t i = 0; i < take; ++i) {
    const size_t at = (v.ring_read + v.ring_count + i) % cap;
    v.ring[at * 2] = frames[i * 2];
    v.ring[at * 2 + 1] = frames[i * 2 + 1];
  }
  v.ring_count += take;
}

void audio_voice_free(audio_voice &voice) {
  if (!voice) return;
  {
    std::lock_guard<std::mutex> guard(audio.lock);
    audio.voices.erase(std::remove(audio.voices.begin(), audio.voices.end(), voice.v), audio.voices.end());
  }
  if (voice.v->decoder_ready) ma_decoder_uninit(&voice.v->decoder);
  ma_data_converter_uninit(&voice.v->converter, nullptr);
  delete voice.v;
  voice.v = nullptr;
}

void audio_voice_play(audio_voice voice) {
  if (!voice) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  if (voice.v->kind != audio_voice_state::stream) rewind(*voice.v);
  voice.v->playing = true;
  voice.v->paused = false;
}

void audio_voice_stop(audio_voice voice) {
  if (!voice) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  voice.v->playing = false;
  voice.v->paused = false;
  if (voice.v->kind != audio_voice_state::stream) rewind(*voice.v);
}

void audio_voice_pause(audio_voice voice) {
  if (!voice) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  voice.v->paused = true;
}

void audio_voice_resume(audio_voice voice) {
  if (!voice) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  voice.v->paused = false;
}

bool audio_voice_playing(audio_voice voice) {
  if (!voice) return false;
  std::lock_guard<std::mutex> guard(audio.lock);
  return voice.v->playing && !voice.v->paused;
}

void audio_voice_set_volume(audio_voice voice, float volume) {
  if (!voice) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  voice.v->volume = volume;
}

void audio_voice_set_pitch(audio_voice voice, float pitch) {
  if (!voice || !(pitch > 0.0f)) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  voice.v->pitch = pitch;
  ma_data_converter_set_rate(&voice.v->converter, voice.v->rate_in, ma_uint32(float(device_rate()) / pitch));
}

void audio_voice_set_pan(audio_voice voice, float pan) {
  if (!voice) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  voice.v->pan = std::clamp(pan, -1.0f, 1.0f);
}

unsigned long long audio_voice_bytes(audio_voice voice) {
  if (!voice) return 0;
  if (voice.v->pcm) return voice.v->pcm->size() * sizeof(float);
  return voice.v->file.size() + voice.v->ring.size() * sizeof(float);
}

unsigned long long audio_voice_frames(audio_voice voice) {
  if (!voice) return 0;
  if (voice.v->pcm) return voice.v->pcm->size() / channels;
  ma_uint64 frames = 0;
  if (voice.v->decoder_ready) {
    std::lock_guard<std::mutex> guard(audio.lock);
    ma_decoder_get_length_in_pcm_frames(&voice.v->decoder, &frames);
  }
  return frames;
}

void audio_voice_set_looping(audio_voice voice, bool looping) {
  if (!voice) return;
  std::lock_guard<std::mutex> guard(audio.lock);
  voice.v->looping = looping;
}
} // namespace njin
