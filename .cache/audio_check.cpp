#include <cassert>
#include <cstdio>
#include <vector>
#include "../src/games/sandtable/audio.cpp"
namespace njin {
struct context {};
std::vector<std::vector<float>> captured;
float clock_value = 1.0f;
int plays = 0;
sound_handle sound_load_samples(context &, const f32 *s, i32 n, i32 rate) {
  assert(rate == 44100);
  captured.emplace_back(s, s+n);
  return {static_cast<u32>(captured.size())};
}
void sound_set_bus(context &, sound_handle, audio_bus) {}
void sound_play_once_at(context &, sound_handle, f32 pitch, f32 gain) {
  assert(pitch > 0.9f && pitch < 1.1f && gain >= 0 && gain <= 1);
  ++plays;
}
f32 elapsed(const context &) { return clock_value; }
}
int main() {
  njin::context ctx;
  using namespace sandtable;
  audio_init(ctx);
  for (auto type : {sfx_type::slash, sfx_type::spear, sfx_type::cannon, sfx_type::explosion, sfx_type::arrow}) {
    const auto &s = njin::captured[audio_sound(type).id-1];
    float peak=0, energy=0;
    for (float v:s) { assert(std::isfinite(v)); peak=std::max(peak,std::abs(v)); energy+=v*v; }
    assert(peak < 0.9f && peak > 0.05f);
    assert(std::abs(s.front()) < 0.001f && std::abs(s.back()) < 0.001f);
    std::printf("SFX %d: peak %.3f, RMS %.3f, smooth edges OK\n",int(type),peak,std::sqrt(energy/s.size()));
    int before=njin::plays;
    for(int i=0;i<1000;++i) audio_play_gated(ctx,type,0.7f,0.18f);
    assert(njin::plays == before+1);
    njin::clock_value+=0.2f;
    audio_play_gated(ctx,type,0.7f,0.18f);
    assert(njin::plays == before+2);
  }
  std::puts("Dense event gating OK");
}
