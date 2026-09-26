#include <njin.h>

namespace {
// Layer để phân biệt người chơi với tường và vật thể trong game.
constexpr njin::u32 layer_player = njin::layer_bit(1);

struct game {
  njin::axis_handle move;
  njin::action_handle jump, down;
  njin::level_handle level;
  entt::entity player = entt::null;
} g;

void startup(njin::njin_ctx &ctx) {
  g.move = njin::axis_register(ctx, "move");
  njin::axis_bind_keys(ctx, g.move, njin::key_left, njin::key_right);
  njin::axis_bind_pad(ctx, g.move, njin::pad_axis_left_x);
  g.jump = njin::action_register(ctx, "jump");
  njin::action_bind_key(ctx, g.jump, njin::key_space);
  njin::action_bind_pad(ctx, g.jump, njin::pad_face_down);
  g.down = njin::action_register(ctx, "down");
  njin::action_bind_key(ctx, g.down, njin::key_down);

  // Màn Tiled: ô có thuộc tính collision = one_way / slope_r / ... (xem trang này).
  g.level = njin::level_load(ctx, "assets/level1.tmx");

  entt::registry &reg = njin::world(ctx);
  g.player = reg.create();
  reg.emplace<njin::transform>(g.player, njin::transform{.pos = {64.0f, 100.0f}});
  reg.emplace<njin::collider>(g.player, njin::collider{.size = {10.0f, 14.0f},
                                                       .offset = {0.0f, -7.0f},
                                                       .layer = layer_player});
  // Cảm giác nhảy chỉnh ngay trên component.
  njin::platformer_body body{};
  body.jump_speed = 350.0f;
  body.air_jumps = 1;            // nhảy đôi
  body.wall_slide_speed = 55.0f; // trượt tường
  body.wall_jump = {150.0f, 300.0f};
  reg.emplace<njin::platformer_body>(g.player, body);
  // Nối action với body: engine tự ghi input mỗi frame.
  reg.emplace<njin::platformer_input_map>(g.player, njin::platformer_input_map{g.move, g.jump, g.down});

  // Camera bám nhân vật, có vùng chết, nhìn trước, và không lộ ra ngoài màn.
  const entt::entity camera = njin::camera_spawn(ctx, 2.0f);
  reg.emplace<njin::camera_follow>(camera, njin::camera_follow{.target = g.player,
                                                               .deadzone = {24.0f, 40.0f},
                                                               .lookahead = {36.0f, 0.0f},
                                                               .bounds = njin::level_bounds(ctx, g.level),
                                                               .pixel_snap = true});
}

// Rơi mạnh thì rung màn hình: nghe sự kiện body_landed.
void on_landed(const njin::body_landed &e) { (void)e; }

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::events(ctx).sink<njin::body_landed>().connect<&on_landed>();
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "Platformer", .width = 1280, .height = 720, .target_fps = 60});
  njin::njin_mod_register(*ctx, {.name = "game", .setup = setup});
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
