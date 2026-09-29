#include "audio.h"

#include <cmath>
#include <vector>

namespace xiangqi {

namespace {
sound_handle sfx_handles[9]{};
bool initialized = false;

inline f32 noise_val(u32 &seed) {
  seed = seed * 1664525u + 1013904223u;
  return static_cast<f32>(seed) / 4294967295.0f * 2.0f - 1.0f;
}

sound_handle make_click(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.05f);
  std::vector<f32> samples(count);
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::exp(-t * 90.0f);
    const f32 wave = std::sin(2.0f * pi * 1400.0f * t) * 0.7f +
                     std::sin(2.0f * pi * 700.0f * t) * 0.3f;
    samples[i] = clamp(wave * env * 0.4f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_command(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.28f);
  std::vector<f32> samples(count);
  u32 seed = 12345;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::exp(-t * 16.0f);
    const f32 freq = 120.0f * std::exp(-t * 12.0f) + 45.0f;
    const f32 tone = std::sin(2.0f * pi * freq * t);
    const f32 noise = noise_val(seed) * std::exp(-t * 40.0f) * 0.3f;
    samples[i] = clamp((tone * 0.8f + noise) * env * 0.65f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_slash(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.18f);
  std::vector<f32> samples(count);
  u32 seed = 54321;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::exp(-t * 24.0f);
    const f32 metallic = std::sin(2.0f * pi * 2400.0f * t) * 0.5f +
                         std::sin(2.0f * pi * 3650.0f * t) * 0.3f;
    const f32 whoosh = noise_val(seed) * (1.0f - std::exp(-t * 60.0f)) * std::exp(-t * 30.0f);
    samples[i] = clamp((metallic * 0.5f + whoosh * 0.6f) * env * 0.5f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_cannon(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.45f);
  std::vector<f32> samples(count);
  u32 seed = 88991;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::exp(-t * 9.0f);
    const f32 freq = 90.0f * std::exp(-t * 10.0f) + 32.0f;
    const f32 tone = std::sin(2.0f * pi * freq * t);
    const f32 blast = noise_val(seed) * std::exp(-t * 14.0f);
    samples[i] = clamp((tone * 0.6f + blast * 0.7f) * env * 0.8f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_charge(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.35f);
  std::vector<f32> samples(count);
  u32 seed = 99112;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::sin(clamp(t / 0.35f, 0.0f, 1.0f) * pi);
    const f32 rumble = std::sin(2.0f * pi * (60.0f + 140.0f * t) * t);
    const f32 rush = noise_val(seed) * 0.4f;
    samples[i] = clamp((rumble * 0.6f + rush) * env * 0.55f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_river_cross(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.55f);
  std::vector<f32> samples(count);
  const f32 freqs[4] = {523.25f, 659.25f, 783.99f, 1046.50f}; // C5, E5, G5, C6
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    f32 sum = 0.0f;
    for (i32 n = 0; n < 4; ++n) {
      const f32 note_start = n * 0.09f;
      if (t >= note_start) {
        const f32 dt_note = t - note_start;
        const f32 env = std::exp(-dt_note * 8.0f);
        sum += std::sin(2.0f * pi * freqs[n] * dt_note) * env * 0.28f;
      }
    }
    samples[i] = clamp(sum, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_rally(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.75f);
  std::vector<f32> samples(count);
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    f32 freq = (t < 0.25f) ? 220.0f : 330.0f;
    const f32 env = (t < 0.08f) ? (t / 0.08f) : std::exp(-(t - 0.08f) * 3.5f);
    // Brass harmonics
    const f32 horn = std::sin(2.0f * pi * freq * t) * 0.5f +
                     std::sin(2.0f * pi * freq * 2.0f * t) * 0.3f +
                     std::sin(2.0f * pi * freq * 3.0f * t) * 0.2f;
    samples[i] = clamp(horn * env * 0.6f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_victory(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 1.1f);
  std::vector<f32> samples(count);
  const f32 notes[4] = {392.0f, 523.25f, 659.25f, 783.99f}; // G4, C5, E5, G5
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    f32 sum = 0.0f;
    for (i32 n = 0; n < 4; ++n) {
      const f32 start = n * 0.18f;
      if (t >= start) {
        const f32 dt_note = t - start;
        const f32 env = std::exp(-dt_note * 3.2f);
        const f32 wave = std::sin(2.0f * pi * notes[n] * dt_note) * 0.6f +
                         std::sin(2.0f * pi * notes[n] * 2.0f * dt_note) * 0.3f;
        sum += wave * env * 0.25f;
      }
    }
    samples[i] = clamp(sum, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_defeat(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 1.2f);
  std::vector<f32> samples(count);
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::exp(-t * 2.5f);
    const f32 gong = std::sin(2.0f * pi * 82.4f * t) * 0.5f +
                     std::sin(2.0f * pi * 130.8f * t) * 0.3f +
                     std::sin(2.0f * pi * 246.9f * t) * 0.2f;
    samples[i] = clamp(gong * env * 0.7f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

} // namespace

void audio_init(context &ctx) {
  if (initialized)
    return;
  sfx_handles[static_cast<i32>(sfx_type::click)] = make_click(ctx);
  sfx_handles[static_cast<i32>(sfx_type::command)] = make_command(ctx);
  sfx_handles[static_cast<i32>(sfx_type::slash)] = make_slash(ctx);
  sfx_handles[static_cast<i32>(sfx_type::cannon)] = make_cannon(ctx);
  sfx_handles[static_cast<i32>(sfx_type::charge)] = make_charge(ctx);
  sfx_handles[static_cast<i32>(sfx_type::river_cross)] = make_river_cross(ctx);
  sfx_handles[static_cast<i32>(sfx_type::rally)] = make_rally(ctx);
  sfx_handles[static_cast<i32>(sfx_type::victory)] = make_victory(ctx);
  sfx_handles[static_cast<i32>(sfx_type::defeat)] = make_defeat(ctx);
  initialized = true;
}

void audio_play(context &ctx, sfx_type type, f32 volume) {
  const i32 idx = static_cast<i32>(type);
  if (idx >= 0 && idx < 9 && sfx_handles[idx].id != 0) {
    sound_play_once_at(ctx, sfx_handles[idx], 1.0f, volume);
  }
}

sound_handle audio_sound(sfx_type type) { return sfx_handles[static_cast<i32>(type)]; }

} // namespace xiangqi
