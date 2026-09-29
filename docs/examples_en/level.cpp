#include <njin.h>
#include <string>

namespace {
njin::scene_handle play{};
njin::level_handle level{};
constexpr njin::u32 layer_wall = njin::layer_bit(1);
constexpr njin::u32 layer_player = njin::layer_bit(0);

struct coin {
  njin::i32 value = 1;
};
struct door {
  std::string target;
};

// The prefab name matches the object's class in Tiled / the entity name in LDtk.
// level_object is attached before the build function runs, so properties can be read.
void build_coin(njin::context &ctx, entt::entity e) {
  entt::registry &reg = njin::world(ctx);
  const auto &obj = reg.get<njin::level_object>(e);
  reg.emplace<coin>(e, coin{obj.props["value"].int_or(1)});
  reg.emplace<njin::collider>(e, njin::collider{.shape = njin::collider_circle, .radius = 6,
                                                .mask = layer_player, .trigger = true});
}

void build_door(njin::context &ctx, entt::entity e) {
  entt::registry &reg = njin::world(ctx);
  const auto &obj = reg.get<njin::level_object>(e);
  reg.emplace<door>(e, door{obj.props["target"].string_or("start")});
  reg.emplace<njin::collider>(e, njin::collider{.size = obj.size, .mask = layer_player,
                                                .trigger = true});
}

void enter_play(njin::context &ctx) {
  // Tiled (.tmx / .tmj) or LDtk (.ldtk): the same call.
  level = njin::level_load(ctx, "assets/levels/forest.tmx",
                           {.solid = njin::collider{.layer = layer_wall}});

  // The player appears at the object named "spawn".
  entt::registry &reg = njin::world(ctx);
  const entt::entity spawn = njin::level_find(ctx, level, "spawn");
  const njin::vec2 start = spawn != entt::null ? reg.get<njin::transform>(spawn).pos
                                               : njin::vec2{32, 32};
  const entt::entity hero = reg.create();
  reg.emplace<njin::transform>(hero, njin::transform{.pos = start});
  reg.emplace<njin::collider>(hero, njin::collider{.size = {12, 16}, .layer = layer_player});
  reg.emplace<njin::scene_owned>(hero, njin::scene_owned{play});

  // Properties set on the whole map in the editor.
  const njin::json_value &props = njin::level_properties(ctx, level);
  NJIN_INFO("background music: %s", props["music"].string_or("none"));
}

void startup(njin::context &ctx) {
  njin::prefab_register(ctx, {.name = "coin", .build = build_coin});
  njin::prefab_register(ctx, {.name = "door", .build = build_door});
  // On leaving the scene, the level's entities are destroyed and its image is freed.
  play = njin::scene_register(ctx, {.name = "play", .on_enter = enter_play});
  njin::scene_set(ctx, play);
}

void setup(njin::context &ctx) { njin::ecs_register(ctx, njin::phase_startup, startup); }
} // namespace

njin::mod_desc level_module() { return {.name = "level", .setup = setup}; }
