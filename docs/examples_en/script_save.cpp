#include <njin.h>

namespace {
using namespace njin;

i32 level = 1;

// Builds the level: each entity has a script and its own save name, the same every time.
void build_level(context &ctx) {
  entt::registry &w = world(ctx);
  const entt::entity chest = w.create();
  script_attach(ctx, chest, "scripts/chest.lua");
  script_set_save_id(ctx, chest, "chest_1");
  const entt::entity guard = w.create();
  script_attach(ctx, guard, "scripts/guard.lua");
  script_set_save_id(ctx, guard, "guard_east");
}

void save_game(context &ctx) {
  json_value save = json_value::make_object();
  save.set("level", level).set("scripts", script_save_state(ctx));
  json_save(save_path(ctx, "save.json").c_str(), save);
}

void load_game(context &ctx) {
  json_value save;
  if (!json_load(save_path(ctx, "save.json").c_str(), save))
    return;
  level = save["level"].int_or(1);
  build_level(ctx);                        // new entities, new numbers
  script_load_state(ctx, save["scripts"]); // self back by save name, then on_load
}

void update(context &ctx) {
  if (key_pressed(ctx, key_f5))
    save_game(ctx);
  if (key_pressed(ctx, key_f9)) {
    world(ctx).clear();
    load_game(ctx);
  }
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, build_level, "build_level");
  ecs_register(ctx, phase_update, update, "update");
}
} // namespace

mod_desc save_module() { return {.name = "save", .setup = setup}; }
