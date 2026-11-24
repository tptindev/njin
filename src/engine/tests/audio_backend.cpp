// Njin's sound output (njin_audio_backend, miniaudio, no raylib) mixed offline: raudio's
// pan law and volume, pitch through the converter (twice the pitch, half the length),
// aliases sharing a sound's samples, music looping without a gap, a pushed stream two
// buffers deep, and the sample files in the repository decoded.
#include "njin_audio_backend.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string &what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
constexpr unsigned rate = 48000;
std::vector<float> render(unsigned frames) {
  std::vector<float> out(size_t(frames) * 2);
  for (unsigned at = 0; at < frames; at += 480) njin::audio_render(out.data() + size_t(at) * 2, std::min(480u, frames - at));
  return out;
}
// Frames until the output goes silent for good.
unsigned audible(const std::vector<float> &out) {
  unsigned last = 0;
  for (size_t f = 0; f < out.size() / 2; ++f)
    if (std::abs(out[f * 2]) > 1e-6f || std::abs(out[f * 2 + 1]) > 1e-6f) last = unsigned(f) + 1;
  return last;
}
} // namespace

int main(int argc, char **argv) {
  check(njin::audio_device_open_offline(rate), "offline device");
  // A constant 0.5 for 0.1 s at the device rate (no resampling on load).
  const std::vector<float> dc(rate / 10, 0.5f);
  njin::audio_voice sound = njin::audio_sound_samples(dc.data(), int(dc.size()), int(rate));
  check(bool(sound), "sound from samples");

  // raudio's pan law: hard left at volume 0.5 is 0.5 x 0.5 on the left, nothing right.
  njin::audio_voice_set_volume(sound, 0.5f);
  njin::audio_voice_set_pan(sound, -1.0f);
  njin::audio_voice_play(sound);
  auto out = render(1000);
  check(std::abs(out[200] - 0.25f) < 1e-5f && std::abs(out[201]) < 1e-6f, "hard left: L 0.25, R 0 (" + std::to_string(out[200]) + ", " + std::to_string(out[201]) + ")");
  // Centred, volume 1: 0.5 x 0.5 x 0.5 x (3 - 0.25) = 0.34375 on each side.
  njin::audio_voice_set_volume(sound, 1.0f);
  njin::audio_voice_set_pan(sound, 0.0f);
  njin::audio_voice_play(sound);
  out = render(rate / 5);
  check(std::abs(out[200] - 0.34375f) < 1e-5f && std::abs(out[201] - 0.34375f) < 1e-5f, "centre: L = R = 0.34375");
  check(audible(out) == rate / 10, "plays its 4800 frames, then stops (" + std::to_string(audible(out)) + ")");
  check(!njin::audio_voice_playing(sound), "not playing once it ended");

  // Twice the pitch: half as long.
  njin::audio_voice_set_pitch(sound, 2.0f);
  njin::audio_voice_play(sound);
  out = render(rate / 5);
  const unsigned fast = audible(out);
  check(fast > rate / 20 - 30 && fast < rate / 20 + 30, "pitch 2 plays about 2400 frames (" + std::to_string(fast) + ")");
  njin::audio_voice_set_pitch(sound, 1.0f);

  // Two aliases of one sound add up.
  njin::audio_voice alias = njin::audio_sound_alias(sound);
  check(bool(alias), "alias");
  njin::audio_voice_play(sound);
  njin::audio_voice_play(alias);
  out = render(1000);
  check(std::abs(out[200] - 2 * 0.34375f) < 1e-5f, "sound and alias mix (" + std::to_string(out[200]) + ")");
  njin::audio_voice_free(alias);
  njin::audio_voice_stop(sound);

  // A stream: room for two buffers, then none until one is played.
  njin::audio_voice stream = njin::audio_stream_open(int(rate), 1024);
  const std::vector<float> chunk(1024 * 2, 0.25f);
  int pushed = 0;
  while (njin::audio_stream_wants(stream) && pushed < 10) { njin::audio_stream_push(stream, chunk.data(), 1024); ++pushed; }
  check(pushed == 2, "stream takes two buffers (" + std::to_string(pushed) + ")");
  njin::audio_voice_play(stream);
  render(1024);
  check(njin::audio_stream_wants(stream), "a played buffer makes room again");
  njin::audio_voice_free(stream);

  // The repository's sample files decode; music loops past its own length.
  int decoded = 0;
  for (int a = 1; a < argc; ++a) {
    if (!std::filesystem::is_directory(argv[a])) continue;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[a])) {
      const auto ext = entry.path().extension().string();
      if (ext != ".wav" && ext != ".ogg" && ext != ".mp3" && ext != ".flac") continue;
      njin::audio_voice file = njin::audio_sound_load(entry.path().string().c_str());
      if (!file || njin::audio_voice_frames(file) == 0) { check(false, "decode " + entry.path().string()); continue; }
      ++decoded;
      njin::audio_voice_free(file);
    }
  }
  check(decoded > 0, std::to_string(decoded) + " sample files decoded");
  for (int a = 1; a < argc; ++a) {
    const auto title = std::filesystem::path(argv[a]) / "platformer/assets/sounds/coin.wav";
    if (!std::filesystem::exists(title)) continue;
    njin::audio_voice music = njin::audio_music_load(title.string().c_str());
    const auto length = njin::audio_voice_frames(music);
    njin::audio_voice_play(music);
    out = render(unsigned(length * 3 + 1000));
    check(music && audible(out) > length * 3, "music loops past three lengths (" + std::to_string(audible(out)) + " of " + std::to_string(length * 3) + ")");
    njin::audio_voice_set_looping(music, false);
    njin::audio_voice_play(music);
    out = render(unsigned(length * 2));
    check(audible(out) <= length + 2, "not looping, it plays once");
    njin::audio_voice_free(music);
  }
  njin::audio_voice_free(sound);
  njin::audio_device_close();
  std::printf("audio_backend: %d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
