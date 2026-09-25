#include <njin.h>
#include <string>

namespace {
bool menu_open = true;
std::string typed;

// Nhập văn bản: dùng text_count/text_char, không dùng key_pressed. Ký tự đã qua
// bố cục bàn phím, nên gõ dấu tiếng Việt hay chữ hoa vẫn đúng.
void read_text(njin::njin_ctx &ctx) {
  for (njin::i32 i = 0; i < njin::text_count(ctx); i++) {
    const njin::i32 codepoint = njin::text_char(ctx, i);
    if (codepoint >= 32 && codepoint < 127) // ví dụ chỉ nhận ASCII
      typed.push_back((char)codepoint);
  }
  if (njin::key_pressed(ctx, njin::key_backspace) && !typed.empty())
    typed.pop_back();
  if (njin::key_pressed(ctx, njin::key_enter)) {
    NJIN_INFO("đã gõ: %s", typed.c_str());
    typed.clear();
  }
}

// Menu chạy TRƯỚC thế giới và nuốt cú nhấp, để thế giới bên dưới không nhận
// cùng cú nhấp đó.
void menu_click(njin::njin_ctx &ctx) {
  if (menu_open && njin::mouse_pressed(ctx, njin::mouse_left)) {
    NJIN_INFO("menu nhận cú nhấp");
    njin::mouse_consume(ctx, njin::mouse_left);
  }
}

void world_click(njin::njin_ctx &ctx) {
  // Khi menu đã nuốt cú nhấp, hàm này trả về false.
  if (njin::mouse_pressed(ctx, njin::mouse_left))
    NJIN_INFO("thế giới nhận cú nhấp");
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_update, read_text);
  njin::ecs_register(ctx, njin::phase_pre_update, menu_click);
  njin::ecs_register(ctx, njin::phase_update, world_click);
}
} // namespace

njin::mod_desc text_consume_module() {
  return {.name = "text_consume", .setup = setup};
}
