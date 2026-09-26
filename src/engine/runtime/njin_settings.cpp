#include "njin_settings.h"
#include "njin_bindings.h"
#include "njin_ctx.h"
#include "njin_file.h"
#include "njin_i18n.h"
#include "njin_log.h"
#include "njin_window.h"
#include <string>

namespace njin {
namespace {
constexpr const char *bus_names[audio_bus_count] = {"master", "music", "sfx", "ui", "voice"};
} // namespace

json_value settings_to_json(const njin_ctx &ctx) {
  json_value audio = json_value::make_object();
  json_value muted = json_value::make_array();
  for (i32 b = 0; b < audio_bus_count; b++) {
    audio.set(bus_names[b], audio_bus_volume(ctx, (audio_bus)b));
    if (audio_bus_muted(ctx, (audio_bus)b))
      muted.push(json_value(bus_names[b]));
  }
  audio.set("muted", std::move(muted));
  json_value out = json_value::make_object();
  out.set("audio", std::move(audio));
  out.set("input", input_bindings_save(ctx));
  out.set("fullscreen", window_fullscreen(ctx));
  out.set("vsync", window_vsync(ctx));
  if (*i18n_language(ctx) != '\0')
    out.set("language", i18n_language(ctx));
  return out;
}

void settings_apply(njin_ctx &ctx, const json_value &json) {
  const json_value &audio = json["audio"];
  if (audio.is(json_value::object)) {
    for (i32 b = 0; b < audio_bus_count; b++) {
      if (audio[bus_names[b]].is(json_value::number))
        audio_set_bus_volume(ctx, (audio_bus)b, audio[bus_names[b]].f32_or(1.0f));
      bool mute = false;
      for (const json_value &m : audio["muted"].items)
        mute = mute || m.str == bus_names[b];
      audio_set_bus_muted(ctx, (audio_bus)b, mute);
    }
  }
  if (json["input"].is(json_value::object))
    input_bindings_load(ctx, json["input"]);
  if (json["fullscreen"].is(json_value::boolean))
    window_set_fullscreen(ctx, json["fullscreen"].b);
  if (json["vsync"].is(json_value::boolean))
    window_set_vsync(ctx, json["vsync"].b);
  if (const char *lang = json["language"].string_or(nullptr)) {
    for (const std::string &l : i18n_languages(ctx))
      if (l == lang)
        i18n_set_language(ctx, lang);
  }
}

bool settings_save(const njin_ctx &ctx, const char *file, const json_value *game) {
  json_value root = settings_to_json(ctx);
  if (game != nullptr)
    root.set("game", *game);
  const std::string path = save_path(ctx, file != nullptr ? file : "settings.json");
  if (!json_save(path.c_str(), root)) {
    NJIN_WARN("settings_save: cannot write %s", path.c_str());
    return false;
  }
  return true;
}

bool settings_load(njin_ctx &ctx, const char *file, json_value *game) {
  const std::string path = save_path(ctx, file != nullptr ? file : "settings.json");
  if (!file_exists(path.c_str()))
    return false;
  json_value root;
  if (!json_load(path.c_str(), root)) {
    NJIN_WARN("settings_load: cannot read %s", path.c_str());
    return false;
  }
  settings_apply(ctx, root);
  if (game != nullptr)
    *game = root["game"];
  return true;
}
} // namespace njin
