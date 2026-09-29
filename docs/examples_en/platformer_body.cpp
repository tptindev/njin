#include <njin.h>

namespace {
// Layer to tell the player apart from walls and objects in the game.
constexpr njin::u32 layer_player = njin::layer_bit(1);

struct game {
  njin::axis_handle move;
  njin::action_handle jump, down;
  njin::level_handle level;
  entt::entity player = entt::null;
} g;

void startup(njin::context &ctx) {
  g.move = njin::axis_define(ctx, "move", {{njin::key_left, njin::key_right}}, {njin::pad_axis_left_x});
  g.jump = njin::action_define(ctx, "jump", {njin::key_space, njin::pad_face_down});
  g.down = njin::action_define(ctx, "down", {njin::key_down});

  // Tiled level: tiles with the property collision = one_way / slope_r / ... (see this page).
  g.level = njin::level_load(ctx, "assets/level1.tmx");

  entt::registry &reg = njin::world(ctx);
  g.player = reg.create();
  reg.emplace<njin::transform>(g.player, njin::transform{.pos = {64.0f, 100.0f}});
  reg.emplace<njin::collider>(g.player, njin::collider{.size = {10.0f, 14.0f},
                                                       .offset = {0.0f, -7.0f},
                                                       .layer = layer_player});
  // Jump feel is tuned right on the component.
  njin::platformer_body body{};
  body.jump_speed = 350.0f;
  body.air_jumps = 1;            // double jump
  body.wall_slide_speed = 55.0f; // wall slide
  body.wall_jump = {150.0f, 300.0f};
  reg.emplace<njin::platformer_body>(g.player, body);
  // Connect the actions to the body: the engine writes the input every frame.
  reg.emplace<njin::platformer_input_map>(g.player, njin::platformer_input_map{g.move, g.jump, g.down});

  // Camera follows the character, has a deadzone, looks ahead, and never shows anything outside the level.
  const entt::entity camera = njin::camera_spawn(ctx, 2.0f);
  reg.emplace<njin::camera_follow>(camera, njin::camera_follow{.target = g.player,
                                                               .deadzone = {24.0f, 40.0f},
                                                               .lookahead = {36.0f, 0.0f},
                                                               .bounds = njin::level_bounds(ctx, g.level),
                                                               .pixel_snap = true});
}

// A hard landing shakes the screen: listen for the body_landed event.
void on_landed(const njin::body_landed &e) { (void)e; }

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::events(ctx).sink<njin::body_landed>().connect<&on_landed>();
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Platformer", .width = 1280, .height = 720, .target_fps = 60});
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
}
