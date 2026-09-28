#include <njin.h>
#include <string_view>

namespace {
njin::ui_layout menu;
bool in_game = false;

void startup(njin::njin_ctx &ctx) {
  // File do njin_ui_editor lưu. Nạp lỗi thì game vẫn chạy, chỉ không có menu.
  njin::ui_layout_load(ctx, "assets/ui/menu.ui.json", menu);
}

void draw_menu(njin::njin_ctx &ctx) {
  if (in_game)
    return;
  // Vẽ mọi panel đang `visible` và mọi popup đang `open` của file.
  njin::ui_draw_layout(ctx, menu, [&](const njin::ui_layout_event &ev) {
    // widget_id là const char*: so bằng string_view, không bằng ==.
    const std::string_view id = ev.widget_id;
    if (ev.kind == njin::ui_layout_event::button_clicked) {
      if (id == "btn_play")
        in_game = true;
      else if (id == "btn_settings")
        njin::ui_layout_set_bool(menu, "tog_fullscreen", false);
      else if (id == "btn_quit")
        njin::njin_quit(ctx);
    }
  });
  // Đọc giá trị widget bất kỳ lúc nào, không cần callback.
  const njin::f32 volume = njin::ui_layout_get_float(menu, "sld_volume", 0.8f);
  (void)volume;
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_post_render, draw_menu);
}
} // namespace

njin::mod_desc ui_layout_module() { return {.name = "ui_layout", .setup = setup}; }
