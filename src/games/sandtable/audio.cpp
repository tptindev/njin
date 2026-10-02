#include "audio.h"

#include <cmath>
#include <vector>

namespace sandtable {

namespace {
constexpr i32 sfx_count = static_cast<i32>(sfx_type::count);
sound_handle sfx_handles[sfx_count]{};
f32 last_played[sfx_count]{};
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

sound_handle make_drum(context &ctx) {
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

// Band-limited noise keeps the impact texture without a white-noise hiss.
struct noise_filter {
  f32 low = 0.0f;
  f32 high = 0.0f;
  f32 band(f32 value, f32 cutoff, f32 floor) {
    low += cutoff * (value - low);
    high += floor * (low - high);
    return low - high;
  }
};

f32 edge_envelope(f32 t, f32 duration, f32 attack = 0.002f) {
  return clamp(t / attack, 0.0f, 1.0f) * clamp((duration - t) / 0.025f, 0.0f, 1.0f);
}

// A short scrape followed by an inharmonic steel ring; spear adds shaft body.
sound_handle make_clash(context &ctx, bool spear) {
  constexpr i32 rate = 44100;
  constexpr f32 duration = 0.32f;
  constexpr i32 count = static_cast<i32>(rate * duration);
  std::vector<f32> samples(count);
  u32 seed = spear ? 45671u : 54321u;
  noise_filter filter;
  const f32 base = spear ? 620.0f : 880.0f;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 grit = filter.band(noise_val(seed), 0.22f, 0.025f);
    const f32 strike = std::max(0.0f, t - 0.018f);
    const f32 contact = edge_envelope(strike, duration - 0.018f, 0.0015f);
    const f32 ring = std::sin(2.0f * pi * base * strike) * std::exp(-strike * 23.0f) * 0.32f +
                     std::sin(2.0f * pi * base * 1.47f * strike) * std::exp(-strike * 32.0f) * 0.19f +
                     std::sin(2.0f * pi * base * 2.09f * strike) * std::exp(-strike * 48.0f) * 0.08f;
    const f32 body = std::sin(2.0f * pi * (spear ? 240.0f : 360.0f) * strike) *
                     std::exp(-strike * 45.0f) * (spear ? 0.42f : 0.18f);
    const f32 scrape = grit * std::exp(-t * 38.0f) * 0.65f;
    samples[i] = (scrape + (ring + body) * contact) * edge_envelope(t, duration) * 0.65f;
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

// Separate muzzle crack and earthier shell impact, with a low rolling tail.
sound_handle make_cannon(context &ctx, bool impact = false) {
  constexpr i32 rate = 44100;
  constexpr f32 duration = 0.85f;
  constexpr i32 count = static_cast<i32>(rate * duration);
  std::vector<f32> samples(count);
  u32 seed = impact ? 18731u : 88991u;
  noise_filter crack_filter, rumble_filter;
  f32 phase = 0.0f;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 noise = noise_val(seed);
    const f32 crack = crack_filter.band(noise, impact ? 0.12f : 0.3f, 0.012f);
    const f32 rumble = rumble_filter.band(noise, 0.035f, 0.003f);
    phase += 2.0f * pi * (42.0f + (impact ? 65.0f : 100.0f) * std::exp(-t * 18.0f)) / rate;
    const f32 body = std::sin(phase) * std::exp(-t * 8.0f);
    samples[i] = (crack * std::exp(-t * 38.0f) * 0.7f + body * 0.48f +
                  rumble * std::exp(-t * 5.0f) * 1.5f) * edge_envelope(t, duration) * 0.75f;
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_charge(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.35f);
  std::vector<f32> samples(count);
  u32 seed = 99112;
  noise_filter filter;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::sin(clamp(t / 0.35f, 0.0f, 1.0f) * pi);
    const f32 rumble = std::sin(2.0f * pi * (60.0f + 140.0f * t) * t);
    const f32 rush = filter.band(noise_val(seed), 0.08f, 0.006f) * 0.4f;
    samples[i] = clamp((rumble * 0.6f + rush) * env * 0.55f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

// Two retro chips knocking together: short bright clicks with a woody body.
sound_handle make_chip(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.12f);
  std::vector<f32> samples(count);
  u32 seed = 424242;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    f32 sum = 0.0f;
    for (const f32 start : {0.0f, 0.035f}) {
      if (t < start)
        continue;
      const f32 dt_hit = t - start;
      const f32 env = std::exp(-dt_hit * 140.0f);
      sum += (std::sin(2.0f * pi * 2900.0f * dt_hit) * 0.5f + std::sin(2.0f * pi * 1150.0f * dt_hit) * 0.4f +
              noise_val(seed) * 0.35f) *
             env * (start > 0.0f ? 0.6f : 1.0f);
    }
    samples[i] = clamp(sum * 0.5f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

// Bow release, then a shaped air pass with a falling, quiet whistle.
sound_handle make_arrow(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr f32 duration = 0.38f;
  constexpr i32 count = static_cast<i32>(rate * duration);
  std::vector<f32> samples(count);
  u32 seed = 7777;
  noise_filter filter;
  f32 phase = 0.0f;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 pass = std::sin(pi * t / duration);
    const f32 air = filter.band(noise_val(seed), 0.18f, 0.045f);
    phase += 2.0f * pi * (1700.0f - 850.0f * t / duration) / rate;
    const f32 string = std::sin(2.0f * pi * 420.0f * t) * std::exp(-t * 65.0f) * 0.18f;
    samples[i] = (air * pass * pass * 1.3f + std::sin(phase) * pass * pass * 0.07f + string) *
                 edge_envelope(t, duration, 0.004f) * 0.75f;
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

// An elephant's trumpet: a nasal tone that swoops up, with a rough edge.
sound_handle make_trumpet(context &ctx) {
  constexpr i32 rate = 44100;
  constexpr i32 count = static_cast<i32>(rate * 0.7f);
  std::vector<f32> samples(count);
  u32 seed = 31337;
  f32 phase = 0.0f;
  for (i32 i = 0; i < count; ++i) {
    const f32 t = static_cast<f32>(i) / rate;
    const f32 env = std::min(1.0f, t / 0.05f) * std::exp(-t * 2.5f);
    const f32 freq = 330.0f + 380.0f * std::min(1.0f, t / 0.25f) + std::sin(t * 60.0f) * 18.0f;
    phase += 2.0f * pi * freq / rate;
    const f32 tone = std::sin(phase) * 0.5f + std::sin(phase * 2.0f) * 0.3f + std::sin(phase * 3.0f) * 0.2f;
    samples[i] = clamp((tone * 0.8f + noise_val(seed) * 0.15f) * env * 0.6f, -1.0f, 1.0f);
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

sound_handle make_horn(context &ctx) {
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
  sfx_handles[static_cast<i32>(sfx_type::drum)] = make_drum(ctx);
  sfx_handles[static_cast<i32>(sfx_type::slash)] = make_clash(ctx, false);
  sfx_handles[static_cast<i32>(sfx_type::spear)] = make_clash(ctx, true);
  sfx_handles[static_cast<i32>(sfx_type::explosion)] = make_cannon(ctx, true);
  sfx_handles[static_cast<i32>(sfx_type::cannon)] = make_cannon(ctx);
  sfx_handles[static_cast<i32>(sfx_type::charge)] = make_charge(ctx);
  sfx_handles[static_cast<i32>(sfx_type::chip)] = make_chip(ctx);
  sfx_handles[static_cast<i32>(sfx_type::horn)] = make_horn(ctx);
  sfx_handles[static_cast<i32>(sfx_type::victory)] = make_victory(ctx);
  sfx_handles[static_cast<i32>(sfx_type::defeat)] = make_defeat(ctx);
  sfx_handles[static_cast<i32>(sfx_type::arrow)] = make_arrow(ctx);
  sfx_handles[static_cast<i32>(sfx_type::trumpet)] = make_trumpet(ctx);
  sound_set_bus(ctx, sfx_handles[static_cast<i32>(sfx_type::click)], bus_ui);
  sound_set_bus(ctx, sfx_handles[static_cast<i32>(sfx_type::chip)], bus_ui);
  initialized = true;
}

void audio_play(context &ctx, sfx_type type, f32 volume) {
  const i32 idx = static_cast<i32>(type);
  if (idx >= 0 && idx < sfx_count && sfx_handles[idx].id != 0) {
    const bool combat = type == sfx_type::slash || type == sfx_type::spear ||
                        type == sfx_type::cannon || type == sfx_type::explosion ||
                        type == sfx_type::arrow || type == sfx_type::charge || type == sfx_type::trumpet;
    // Audio variation must not consume the simulation RNG.
    static u32 variation = 918273u;
    const f32 pitch = combat ? 1.0f + noise_val(variation) * 0.065f : 1.0f;
    const f32 gain = combat ? std::pow(10.0f, -4.0f / 20.0f) * (0.95f + noise_val(variation) * 0.05f) : 1.0f;
    sound_play_once_at(ctx, sfx_handles[idx], pitch, clamp(volume, 0.0f, 1.0f) * gain);
  }
}

void audio_play_gated(context &ctx, sfx_type type, f32 volume, f32 gap) {
  const i32 idx = static_cast<i32>(type);
  if (idx < 0 || idx >= sfx_count)
    return;
  const f32 now = elapsed(ctx);
  if (now - last_played[idx] < gap && last_played[idx] > 0.0f)
    return;
  last_played[idx] = now;
  audio_play(ctx, type, volume);
}

sound_handle audio_sound(sfx_type type) { return sfx_handles[static_cast<i32>(type)]; }

} // namespace sandtable
