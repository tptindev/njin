#include <njin.h>

namespace {
njin::i32 level = njin::quality_high;
bool open_settings = true;

void startup(njin::context &ctx) {
  // Các lần sau settings_load() nạp lại bộ giới hạn người chơi đã chọn (khóa
  // "graphics"); mức hiện trong menu là dữ liệu riêng của game (khóa "game").
  njin::json_value game;
  if (njin::settings_load(ctx, "settings.json", &game)) {
    level = (njin::i32)game["quality"].f32_or(njin::quality_high);
    return;
  }
  // Lần chạy đầu chưa có file cài đặt: đoán mức hợp với máy.
  level = njin::graphics_quality_suggest(ctx);
  njin::graphics_quality_set(ctx, njin::graphics_quality_preset((njin::quality_level)level));
}

void settings_menu(njin::context &ctx) {
  if (!open_settings)
    return;
  njin::ui_begin(ctx, {.id = "settings", .title = "Cài đặt"});
  if (njin::ui_choice(ctx, "Đồ họa", level, {"Thấp", "Vừa", "Cao"}))
    njin::graphics_quality_set(ctx, njin::graphics_quality_preset((njin::quality_level)level));
  // Một mức dựng sẵn, chỉnh thêm một trường: vẽ thế giới ở nửa độ phân giải.
  if (njin::ui_button(ctx, "Siêu nhẹ")) {
    njin::graphics_quality q = njin::graphics_quality_preset(njin::quality_low);
    q.world_scale = 0.5f;
    njin::graphics_quality_set(ctx, q);
  }
  if (njin::ui_button(ctx, "Xong")) {
    njin::json_value game = njin::json_value::make_object();
    game.set("quality", level);
    njin::settings_save(ctx, "settings.json", &game);
    open_settings = false;
  }
  njin::ui_end(ctx);
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Graphics quality", .width = 960, .height = 540, .target_fps = 60});
  njin::ecs_register(*ctx, njin::phase_startup, startup);
  njin::ecs_register(*ctx, njin::phase_post_render, settings_menu);
  njin::run(*ctx);
  njin::destroy(ctx);
}
