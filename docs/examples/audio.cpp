#include <njin.h>

namespace {
njin::sound_handle click;
njin::sound_handle hit;
njin::sound_handle engine_hum;
njin::music_handle theme;
njin::action_handle fire;
njin::action_handle pause_music;
bool paused = false;

void load(njin::context &ctx) {
  // Sound: ngắn, nằm hết trong bộ nhớ. Music: dài, stream từ đĩa.
  click = njin::sound_load(ctx, "assets/click.wav");
  hit = njin::sound_load(ctx, "assets/hit.wav");
  engine_hum = njin::sound_load(ctx, "assets/hum.ogg");
  theme = njin::music_load(ctx, "assets/theme.ogg");

  // Nạp thất bại (file thiếu, không có loa) trả về handle id 0 và ghi log.
  // Mọi hàm nhận handle đó đều không làm gì, nên không cần kiểm tra ở mọi chỗ.

  njin::sound_set_volume(ctx, engine_hum, 0.4f);
  njin::music_set_volume(ctx, theme, 0.7f);
  njin::music_play(ctx, theme); // lặp mặc định
  njin::sound_play_loop(ctx, engine_hum);

  fire = njin::action_register(ctx, "fire");
  njin::action_bind_key(ctx, fire, njin::key_space);
  pause_music = njin::action_register(ctx, "pause_music");
  njin::action_bind_key(ctx, pause_music, njin::key_p);
}

void update(njin::context &ctx) {
  // Bấm dồn cũng nghe rõ từng tiếng nhấp: mỗi lần cắt bản trước rồi phát lại.
  if (njin::key_pressed(ctx, njin::key_enter))
    njin::sound_play_restart(ctx, click);

  // Tiếng va chạm: nhiều thứ cùng kích hoạt thì chồng lên nhau (tối đa 8 bản).
  // Vật lớn hơn thì trầm hơn và to hơn: cùng một bản ghi cho mọi kích cỡ.
  if (njin::action_pressed(ctx, fire)) {
    const njin::f32 size = 2.0f; // ví dụ
    njin::sound_play_once_at(ctx, hit, 1.0f / size, 0.5f + 0.25f * size);
  }

  // Tạm dừng giữ nguyên vị trí; phát tiếp từ chỗ đó.
  if (njin::action_pressed(ctx, pause_music)) {
    paused = !paused;
    if (paused)
      njin::music_pause(ctx, theme);
    else
      njin::music_resume(ctx, theme);
  }
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_update, update);
}
} // namespace

njin::mod_desc audio_demo_module() {
  return {.name = "audio_demo", .setup = setup};
}
