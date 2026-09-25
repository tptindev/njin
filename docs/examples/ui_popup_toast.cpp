#include <njin.h>

namespace {
bool quit_popup_open = false;
bool has_unsaved = true;

void save_game(njin::njin_ctx &ctx) {
  // ... ghi file ...
  has_unsaved = false;
  // Toast: gọi từ bất cứ đâu, không cần ui_begin.
  njin::ui_toast(ctx, "Đã lưu game", {.kind = njin::ui_toast_success});
}

void menus(njin::njin_ctx &ctx) {
  njin::ui_begin(ctx, {.id = "pause", .title = "Tạm dừng"});
  if (njin::ui_button(ctx, "Lưu"))
    save_game(ctx);
  if (njin::ui_button(ctx, "Thoát"))
    quit_popup_open = true;
  njin::ui_end(ctx);

  // Popup: gọi mỗi frame, `open` do game giữ. Menu phía trên vẫn được vẽ,
  // nhưng đứng yên (modal) cho đến khi popup đóng.
  if (quit_popup_open) {
    const njin::i32 pick = njin::ui_popup(
        ctx,
        {.id = "quit",
         .title = "Thoát game?",
         .message = has_unsaved ? "Tiến trình chưa lưu sẽ mất." : "Hẹn gặp lại!",
         .buttons = {"Ở lại", "Lưu và thoát", "Thoát"},
         .default_button = 0, // nút an toàn được chọn sẵn
         .cancel_button = 0}, // Esc / Backspace / B là "Ở lại"
        quit_popup_open);
    if (pick == 1) {
      save_game(ctx);
      njin::njin_quit(ctx);
    } else if (pick == 2) {
      njin::njin_quit(ctx);
    }
  }
}

// Popup có nội dung tùy ý: ui_popup_begin / ui_popup_end bao quanh widget thường.
bool settings_open = false;
njin::f32 volume = 0.8f;

void settings_popup(njin::njin_ctx &ctx) {
  if (!settings_open)
    return;
  njin::ui_popup_begin(ctx, {.id = "settings", .title = "Cài đặt", .width = 460});
  njin::ui_slider(ctx, "Âm lượng", volume, 0.0f, 1.0f, 0.05f, true);
  if (njin::ui_button(ctx, "Xong") || njin::ui_back(ctx)) // ui_back chỉ báo cho popup
    settings_open = false;
  njin::ui_popup_end(ctx);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_post_render, menus);
  njin::ecs_register(ctx, njin::phase_post_render, settings_popup);
}
} // namespace

njin::mod_desc ui_popup_toast_module() { return {.name = "ui_popup_toast", .setup = setup}; }
