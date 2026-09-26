#include <njin.h>

namespace {
njin::action_handle jump;
bool open_settings = true;

void startup(njin::njin_ctx &ctx) {
  jump = njin::action_register(ctx, "jump");
  njin::action_bind_key(ctx, jump, njin::key_space);
  njin::action_bind_pad(ctx, jump, njin::pad_face_down);
  // Sau khi đăng ký hết action và nạp ngôn ngữ: phím người chơi đã đổi, âm
  // lượng, toàn màn hình, ngôn ngữ thay cho giá trị mặc định.
  njin::settings_load(ctx);
}

void settings_menu(njin::njin_ctx &ctx) {
  if (!open_settings)
    return;
  njin::ui_begin(ctx, {.id = "settings", .title = "Cài đặt"});

  // Âm lượng từng kênh. Kênh master nhân vào tất cả.
  njin::f32 music = njin::audio_bus_volume(ctx, njin::bus_music);
  if (njin::ui_slider(ctx, "Nhạc", music, 0.0f, 1.0f, 0.1f, true))
    njin::audio_set_bus_volume(ctx, njin::bus_music, music);

  // Đổi phím: bấm vào dòng rồi bấm phím mới. Dòng thứ hai cho tay cầm.
  njin::ui_keybind(ctx, "Nhảy", jump);
  njin::ui_keybind(ctx, "Nhảy##pad", jump, true);

  // Không coi Esc là "đóng menu" khi đang chờ phím.
  if (njin::ui_button(ctx, "Xong") || (!njin::ui_keybind_listening(ctx) && njin::ui_back(ctx))) {
    njin::settings_save(ctx); // ghi vào thư mục lưu game của người dùng
    open_settings = false;
  }
  njin::ui_end(ctx);
}

// Rung tay cầm khi trúng đòn.
void on_hit(njin::njin_ctx &ctx) { njin::pad_rumble(ctx, 0, 0.6f, 0.8f, 0.25f); }

// Chuyển nhạc mượt giữa hai bản, theo giờ thật nên chạy cả khi game pause.
void enter_boss(njin::njin_ctx &ctx, njin::music_handle boss) { njin::music_crossfade(ctx, boss, 1.5f); }

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_post_render, settings_menu);
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "Settings", .width = 1280, .height = 720, .target_fps = 60});
  njin::njin_mod_register(*ctx, {.name = "game", .setup = setup});
  (void)on_hit;
  (void)enter_boss;
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
