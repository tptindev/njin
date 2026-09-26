#include "settings_menu.h"
#include <string>
#include <vector>

namespace shared {
using namespace njin;

namespace {
// Corners are square in the pixel style: a rounded corner is a curve, and there
// is no curve in a pixel grid.
void square(ui_look &look) {
  for (ui_skin *skin : {&look.normal, &look.focused, &look.pressed, &look.disabled})
    skin->roundness = 0.0f;
}
} // namespace

void apply_style(njin_ctx &ctx, const char *font_path, font_style style) {
  const bool pixel = style == font_pixel;
  const font_handle font = font_load(ctx, font_path, 16, style);
  ui_style s = ui_default_style();
  s.font = font;
  s.font_size = 16.0f;
  s.padding = pixel ? 10.0f : 12.0f;
  s.spacing = pixel ? 4.0f : 5.0f;
  s.widget_height = pixel ? 22.0f : 24.0f;
  s.width = 300.0f;
  s.toast_width = 260.0f;
  s.toast_margin = {10.0f, 10.0f};
  if (pixel)
    for (ui_look *look : {&s.panel, &s.button, &s.track, &s.fill, &s.knob, &s.toast})
      square(*look);
  ui_style_set(ctx, s);

  dialog_style d = dialog_default_style();
  d.font = font;
  d.font_size = 16.0f;
  d.padding = 10.0f;
  d.margin = 10.0f;
  d.max_width = 520.0f;
  d.lines = 3;
  d.chars_per_second = 50.0f;
  d.portrait_size = {48.0f, 48.0f};
  dialog_set_style(ctx, d);
}

bool settings_panel(njin_ctx &ctx, std::span<const rebind_row> rows) {
  ui_begin(ctx, {.id = "settings", .title = tr(ctx, "settings.title"), .width = 340.0f});

  static const struct {
    const char *key;
    audio_bus bus;
  } volumes[] = {{"settings.master", bus_master}, {"settings.music", bus_music},
                 {"settings.sfx", bus_sfx}};
  for (const auto &v : volumes) {
    f32 value = audio_bus_volume(ctx, v.bus);
    if (ui_slider(ctx, tr(ctx, v.key), value, 0.0f, 1.0f, 0.1f, true))
      audio_set_bus_volume(ctx, v.bus, value);
  }

  bool full = window_fullscreen(ctx);
  bool vsync = window_vsync(ctx);
  ui_row(ctx, 2);
  if (ui_toggle(ctx, tr(ctx, "settings.fullscreen"), full))
    window_set_fullscreen(ctx, full);
  if (ui_toggle(ctx, tr(ctx, "settings.vsync"), vsync))
    window_set_vsync(ctx, vsync);

  // Language: every loaded table, shown by its own name.
  const std::vector<std::string> langs = i18n_languages(ctx);
  if (langs.size() > 1) {
    i32 index = 0;
    for (usize i = 0; i < langs.size(); i++)
      if (langs[i] == i18n_language(ctx))
        index = (i32)i;
    // ui_choice takes an initializer list: two or three languages are enough
    // for the samples.
    const char *a = i18n_language_name(ctx, langs[0].c_str());
    const char *b = i18n_language_name(ctx, langs[1].c_str());
    const char *c = langs.size() > 2 ? i18n_language_name(ctx, langs[2].c_str()) : nullptr;
    const bool changed = c != nullptr ? ui_choice(ctx, tr(ctx, "settings.language"), index, {a, b, c})
                                      : ui_choice(ctx, tr(ctx, "settings.language"), index, {a, b});
    if (changed)
      i18n_set_language(ctx, langs[(usize)index].c_str());
  }

  ui_space(ctx, 4.0f);
  ui_label(ctx, tr(ctx, "settings.controls"));
  for (const rebind_row &r : rows) {
    ui_row(ctx, 2);
    ui_keybind(ctx, tr(ctx, r.label_key), r.action);
    const std::string pad_label = std::string("##pad.") + r.label_key;
    ui_keybind(ctx, pad_label.c_str(), r.action, true);
  }

  ui_space(ctx, 4.0f);
  bool done = ui_button(ctx, tr(ctx, "menu.back"));
  if (!ui_keybind_listening(ctx) && ui_back(ctx))
    done = true;
  ui_end(ctx);
  if (done)
    settings_save(ctx);
  return done;
}
} // namespace shared
