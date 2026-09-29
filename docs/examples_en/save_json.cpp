#include <njin.h>
#include <string>
#include <vector>

namespace {
struct progress {
  njin::i32 level = 1;
  njin::f32 volume = 0.8f;
  std::vector<std::string> items;
};
progress p;

std::string file(njin::context &ctx) { return njin::save_path(ctx, "save.json"); }

void save(njin::context &ctx) {
  njin::json_value items = njin::json_value::make_array();
  for (const std::string &it : p.items)
    items.push(it);
  njin::json_value doc = njin::json_value::make_object();
  doc.set("version", 1).set("level", p.level).set("volume", p.volume).set("items", std::move(items));
  njin::json_save(file(ctx).c_str(), doc); // writes to a temp file then renames: never left half-written
}

void load(njin::context &ctx) {
  njin::json_value doc;
  if (!njin::json_load(file(ctx).c_str(), doc))
    return; // first run: keep the default values
  // A missing key reads as null, so every read has a fallback value: an old
  // save without "volume" still loads.
  p.level = doc["level"].int_or(1);
  p.volume = doc["volume"].f32_or(0.8f);
  p.items.clear();
  for (const njin::json_value &it : doc["items"].items)
    p.items.push_back(it.string_or(""));
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_shutdown, save);
}
} // namespace

njin::mod_desc save_module() { return {.name = "save", .setup = setup}; }
