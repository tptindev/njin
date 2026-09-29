#include <njin.h>
#include <string>

namespace {
bool menu_open = true;
std::string typed;

// Text input: use text_count/text_char, not key_pressed. Characters have already
// gone through the keyboard layout, so typing Vietnamese diacritics or capital letters still works.
void read_text(njin::context &ctx) {
  for (njin::i32 i = 0; i < njin::text_count(ctx); i++) {
    const njin::i32 codepoint = njin::text_char(ctx, i);
    if (codepoint >= 32 && codepoint < 127) // example: accept ASCII only
      typed.push_back((char)codepoint);
  }
  if (njin::key_pressed(ctx, njin::key_backspace) && !typed.empty())
    typed.pop_back();
  if (njin::key_pressed(ctx, njin::key_enter)) {
    NJIN_INFO("typed: %s", typed.c_str());
    typed.clear();
  }
}

// The menu runs BEFORE the world and swallows the click, so the world underneath
// does not receive the same click.
void menu_click(njin::context &ctx) {
  if (menu_open && njin::mouse_pressed(ctx, njin::mouse_left)) {
    NJIN_INFO("menu received the click");
    njin::mouse_consume(ctx, njin::mouse_left);
  }
}

void world_click(njin::context &ctx) {
  // Once the menu has swallowed the click, this returns false.
  if (njin::mouse_pressed(ctx, njin::mouse_left))
    NJIN_INFO("world received the click");
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_update, read_text);
  njin::ecs_register(ctx, njin::phase_pre_update, menu_click);
  njin::ecs_register(ctx, njin::phase_update, world_click);
}
} // namespace

njin::mod_desc text_consume_module() {
  return {.name = "text_consume", .setup = setup};
}
