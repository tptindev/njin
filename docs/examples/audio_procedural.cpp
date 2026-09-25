#include <cmath>
#include <njin.h>
#include <vector>

namespace {
njin::sound_handle beep;

// Âm thanh do game tự sinh ra lúc khởi động, không đọc từ file.
void make_beep(njin::njin_ctx &ctx) {
  const njin::i32 sample_rate = 44100;
  const njin::i32 count = sample_rate / 5; // 0,2 giây

  std::vector<njin::f32> samples((std::size_t)count);
  for (njin::i32 i = 0; i < count; i++) {
    const njin::f32 t = (njin::f32)i / (njin::f32)sample_rate;
    const njin::f32 fade = 1.0f - (njin::f32)i / (njin::f32)count; // tắt dần
    samples[(std::size_t)i] = 0.4f * fade * std::sin(2.0f * 3.14159265f * 880.0f * t);
  }

  // Mono, số thực 32 bit. Dữ liệu được chép đi, nên `samples` bị hủy ngay sau
  // đó cũng không sao.
  beep = njin::sound_load_samples(ctx, samples.data(), count, sample_rate);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, make_beep);
}
} // namespace

njin::mod_desc procedural_audio_module() {
  return {.name = "procedural_audio", .setup = setup};
}
