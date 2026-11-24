#include <njin.h>
#include <njin_rml.h>

namespace {
using namespace njin;

// Trạng thái của game mà HUD hiện ra.
struct hud_state {
  i64 coins = 0;
  f64 health = 1.0;
  bool paused = false;
  std::string player = "Lan";
};
hud_state hud;
rml_document_handle hud_doc;

void load(context &ctx) {
  // Font dùng chung cho mọi tài liệu; RCSS gọi nó bằng tên họ font trong file.
  rml_font_load(ctx, "ui/fonts/BeVietnamPro-Bold.ttf");
  // Model trước, tài liệu sau: tài liệu đọc model lúc nạp.
  const rml_model_handle model = rml_model(ctx, "hud");
  rml_bind_int(ctx, model, "coins", [] { return hud.coins; });
  rml_bind_float(ctx, model, "health", [] { return hud.health; });
  rml_bind_bool(ctx, model, "paused", [] { return hud.paused; });
  // Ô nhập tên: tài liệu ghi lại khi người chơi gõ (data-value).
  rml_bind_string(ctx, model, "player", [] { return hud.player; }, [](const std::string &v) { hud.player = v; });
  // Nút "Chơi tiếp" (data-event-click="resume").
  rml_bind_event(ctx, model, "resume", [](context &) { hud.paused = false; });
  hud_doc = rml_document_load(ctx, "ui/hud.rml");
  // Một sự kiện gắn theo id, không qua model.
  rml_on(ctx, hud_doc, "quit", "click", [](context &c) { quit(c); });
}

void update(context &ctx) {
  if (key_pressed(ctx, key_escape))
    hud.paused = !hud.paused;
  // Bấm chuột vào nút của HUD không bắn súng: chuột lúc đó thuộc về UI.
  if (!hud.paused && mouse_pressed(ctx, mouse_left))
    hud.coins += 1;
  if (key_pressed(ctx, key_f8))
    rml_debugger(ctx, true); // cây phần tử, style, sự kiện
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
}
} // namespace

mod_desc hud_module() { return {.name = "hud", .setup = setup}; }
