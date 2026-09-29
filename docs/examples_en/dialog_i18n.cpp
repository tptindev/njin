#include <njin.h>

// assets/lang/vi.json:    { "_name": "Tiếng Việt", "npc": { "hello": "Chào bạn!" } }
// assets/dialog/owl.json: dialogue script; text starting with @ is a translation key.
namespace {
njin::dialog_script owl;

void startup(njin::context &ctx) {
  njin::i18n_load(ctx, "vi", "assets/lang/vi.json"); // the first language loaded is the default
  njin::i18n_load(ctx, "en", "assets/lang/en.json");

  njin::dialog_load("assets/dialog/owl.json", owl);
  // Portraits are referenced by the name used in the script: "portrait": "owl".
  njin::dialog_portrait(ctx, "owl", njin::texture_load(ctx, "assets/portraits.png"),
                        njin::rect{{0.0f, 0.0f}, {32.0f, 32.0f}});
  // Conditions in the script ("if": "has_key") use a syntax the game decides.
  njin::dialog_set_condition(ctx, [](njin::context &, const std::string &cond) { return cond == "always"; });
}

void talk(njin::context &ctx) {
  if (njin::key_pressed(ctx, njin::key_e) && !njin::dialog_active(ctx))
    njin::dialog_start(ctx, owl);
}

// The script sends an event: "event": "got_sword".
void on_dialog_event(const njin::dialog_event &e) { (void)e; }

void ui(njin::context &ctx) {
  njin::ui_begin(ctx, {.id = "menu", .title = njin::tr(ctx, "menu.title")});
  // A sentence with parameters: "hud.coins": "Gold: {0}/{1}"
  njin::ui_label(ctx, njin::trf(ctx, "hud.coins", {"3", "10"}).c_str());
  njin::i32 language = 0;
  if (njin::ui_choice(ctx, njin::tr(ctx, "settings.language"), language,
                      {njin::i18n_language_name(ctx, "vi"), njin::i18n_language_name(ctx, "en")}))
    njin::i18n_set_language(ctx, language == 0 ? "vi" : "en");
  njin::ui_end(ctx);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_update, talk);
  njin::ecs_register(ctx, njin::phase_post_render, ui);
  njin::events(ctx).sink<njin::dialog_event>().connect<&on_dialog_event>();
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Dialog", .width = 1280, .height = 720, .target_fps = 60});
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
}
