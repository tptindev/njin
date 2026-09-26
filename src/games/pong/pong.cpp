#include "pong.h"
#include <cmath>
#include <string>
#include <vector>

// Pong on njin. Worth reading top to bottom as a tour of the engine:
//   scenes          menu -> play -> over, entities owned by the play scene
//   fixed update    ball and paddle physics at a steady 60 Hz
//   collision       collide_rects, reflect
//   input           actions and axes, keyboard or gamepad
//   camera          a fixed 960x540 field scaled to any window size, shake
//   drawing, text   shapes in world space, UI in screen space
//   audio           blips generated in memory, panned by position
//   time            pause (time_set_paused), slow motion on the winning point
//   files           best rally saved in the user's save folder

namespace pong {
namespace {
using namespace njin;

// The field is always 960x540 world units; the camera scales it to the window.
constexpr vec2 field{960.0f, 540.0f};
constexpr vec2 paddle_size{16.0f, 96.0f};
constexpr f32 ball_size = 14.0f;
constexpr f32 paddle_speed = 520.0f;
constexpr f32 serve_speed = 380.0f;
constexpr f32 max_speed = 900.0f;
constexpr i32 winning_score = 5;

// Components.
struct paddle {
  i32 side = 0; // 0 left (player), 1 right (player 2 or computer)
};
struct ball {
  vec2 velocity{};
};
struct body {
  vec2 size{};
};

// Game state that is not an entity.
struct game_state {
  scene_handle menu, play, over;
  i32 score[2] = {0, 0};
  i32 rally = 0;
  i32 best_rally = 0;
  bool two_players = false;
  bool paused = false;
  timer serve_delay{.duration = 0.8f};
  timer shake{.duration = 0.25f};
  timer finish{.duration = 1.2f}; // slow motion after the winning point
  i32 winner = -1;
  entt::entity camera = entt::null;
  sound_handle blip_paddle, blip_wall, blip_score;
  font_handle font{};
  axis_handle move[2];
  action_handle confirm, back, pause, fullscreen, screenshot, toggle_players;
  std::string save_file;
};
game_state g;

// Helpers.

rect body_rect(const transform &tr, const body &b) {
  return rect_from_center(tr.pos, b.size);
}

// A short square-wave blip, made in memory so the game ships without files.
sound_handle make_blip(njin_ctx &ctx, f32 freq, f32 seconds) {
  const i32 rate = 44100;
  const i32 count = (i32)((f32)rate * seconds);
  std::vector<f32> samples((std::size_t)count);
  for (i32 i = 0; i < count; i++) {
    const f32 t = (f32)i / (f32)rate;
    const f32 fade = 1.0f - (f32)i / (f32)count;
    samples[(std::size_t)i] =
        (std::fmod(t * freq, 1.0f) < 0.5f ? 0.25f : -0.25f) * fade * fade;
  }
  return sound_load_samples(ctx, samples.data(), count, rate);
}

void load_best(njin_ctx &ctx) {
  g.save_file = save_path(ctx, "best_rally.txt");
  std::string text;
  if (file_read(g.save_file.c_str(), text))
    g.best_rally = std::atoi(text.c_str());
}

void save_best() {
  file_write(g.save_file.c_str(), std::to_string(g.best_rally) + "\n");
}

// Draws text centred on `center`, in screen space.
void text_centered(njin_ctx &ctx, const char *text, vec2 center, f32 size,
                   rgba color) {
  const vec2 m = text_measure(ctx, text, size, g.font);
  draw_text(ctx, text, center - m * 0.5f, size, color, g.font);
}

void serve(njin_ctx &ctx, entt::registry &reg, i32 toward) {
  for (auto [e, tr, b] : reg.view<transform, ball>().each()) {
    tr.pos = field * 0.5f;
    // A random angle within 35 degrees of horizontal, never straight up.
    const f32 angle = random(ctx).range(-35.0f, 35.0f);
    b.velocity = from_angle(toward == 0 ? 180.0f + angle : angle) * serve_speed;
  }
  g.rally = 0;
  g.serve_delay.reset();
}

// Scenes.

void enter_play(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  g.score[0] = g.score[1] = 0;
  g.winner = -1;
  g.finish.reset();
  g.paused = false;
  time_set_paused(ctx, false);
  time_set_scale(ctx, 1.0f);
  for (i32 side = 0; side < 2; side++) {
    const entt::entity e = reg.create();
    const f32 x = side == 0 ? 40.0f : field.x - 40.0f;
    reg.emplace<transform>(e, transform{.pos = {x, field.y * 0.5f}});
    reg.emplace<body>(e, body{paddle_size});
    reg.emplace<paddle>(e, paddle{side});
    // Destroyed automatically when the play scene is left.
    reg.emplace<scene_owned>(e, scene_owned{g.play});
  }
  const entt::entity e = reg.create();
  reg.emplace<transform>(e);
  reg.emplace<body>(e, body{{ball_size, ball_size}});
  reg.emplace<ball>(e);
  reg.emplace<scene_owned>(e, scene_owned{g.play});
  serve(ctx, reg, random(ctx).range(0, 1));
}

void exit_play(njin_ctx &ctx) {
  time_set_paused(ctx, false);
  time_set_scale(ctx, 1.0f);
  save_best();
}

// Systems.

void startup(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  g.camera = reg.create();
  reg.emplace<transform>(g.camera, transform{.pos = field * 0.5f});
  reg.emplace<camera_2d>(g.camera);
  reg.emplace<camera_on>(g.camera);

  g.blip_paddle = make_blip(ctx, 440.0f, 0.08f);
  g.blip_wall = make_blip(ctx, 220.0f, 0.06f);
  g.blip_score = make_blip(ctx, 110.0f, 0.4f);
  // A system font with Vietnamese glyphs when there is one; otherwise the
  // handle stays 0 and the engine's default font is used.
  g.font = font_load(ctx, "C:/Windows/Fonts/segoeui.ttf", 48);
  audio_set_range(ctx, 300.0f, 1400.0f);

  g.move[0] = axis_register(ctx, "p1_move");
  axis_bind_keys(ctx, g.move[0], key_w, key_s);
  axis_bind_pad(ctx, g.move[0], pad_axis_left_y);
  g.move[1] = axis_register(ctx, "p2_move");
  axis_bind_keys(ctx, g.move[1], key_up, key_down);
  g.confirm = action_register(ctx, "confirm");
  action_bind_key(ctx, g.confirm, key_enter);
  action_bind_key(ctx, g.confirm, key_space);
  action_bind_pad(ctx, g.confirm, pad_face_down);
  g.back = action_register(ctx, "back");
  action_bind_key(ctx, g.back, key_escape);
  action_bind_pad(ctx, g.back, pad_face_right);
  g.pause = action_register(ctx, "pause");
  action_bind_key(ctx, g.pause, key_p);
  action_bind_pad(ctx, g.pause, pad_start);
  g.fullscreen = action_register(ctx, "fullscreen");
  action_bind_key(ctx, g.fullscreen, key_f11);
  g.screenshot = action_register(ctx, "screenshot");
  action_bind_key(ctx, g.screenshot, key_f12);
  g.toggle_players = action_register(ctx, "toggle_players");
  action_bind_key(ctx, g.toggle_players, key_tab);

  load_best(ctx);
  scene_set(ctx, g.menu);
}

// Fits the 960x540 field into the window and applies screen shake.
void fit_camera(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.camera))
    return;
  const vec2 screen = screen_size(ctx);
  camera_2d &cam = reg.get<camera_2d>(g.camera);
  transform &tr = reg.get<transform>(g.camera);
  cam.zoom = std::fmin(screen.x / field.x, screen.y / field.y);
  cam.offset = screen * 0.5f;
  tr.pos = field * 0.5f;
  if (!g.shake.finished) {
    g.shake.tick(delta_real(ctx));
    const f32 strength = 8.0f * (1.0f - g.shake.progress());
    tr.pos += random(ctx).direction() * strength;
  }
}

void global_input(njin_ctx &ctx) {
  if (action_pressed(ctx, g.fullscreen))
    window_set_fullscreen(ctx, !window_fullscreen(ctx));
  // Saved to %APPDATA%/njin pong/screenshots, taken at the end of the frame.
  if (action_pressed(ctx, g.screenshot))
    screenshot(ctx);
}

void menu_update(njin_ctx &ctx) {
  if (action_pressed(ctx, g.toggle_players))
    g.two_players = !g.two_players;
  if (action_pressed(ctx, g.confirm))
    scene_set(ctx, g.play);
  if (action_pressed(ctx, g.back))
    njin_quit(ctx);
}

void play_input(njin_ctx &ctx) {
  if (action_pressed(ctx, g.pause) || action_pressed(ctx, g.back)) {
    g.paused = !g.paused;
    time_set_paused(ctx, g.paused);
  }
  if (g.paused && action_pressed(ctx, g.confirm))
    scene_set(ctx, g.menu);
  // Real time, so the slow motion lasts the same however slow it is.
  if (g.winner >= 0 && !g.paused && g.finish.tick(delta_real(ctx)))
    scene_set(ctx, g.over);
}

// Physics runs at a fixed 60 Hz, so the ball behaves the same at any FPS.
void play_physics(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  const f32 dt = delta(ctx); // one fixed step here

  // Paddles: player input, or a simple computer that follows the ball.
  vec2 ball_pos = field * 0.5f;
  for (auto [e, tr, b] : reg.view<const transform, const ball>().each())
    ball_pos = tr.pos;
  for (auto [e, tr, p, b] : reg.view<transform, const paddle, const body>().each()) {
    f32 dir = axis_value(ctx, g.move[p.side]);
    if (p.side == 1 && !g.two_players) {
      const f32 diff = ball_pos.y - tr.pos.y;
      dir = std::fabs(diff) < 10.0f ? 0.0f : (diff > 0.0f ? 0.8f : -0.8f);
    }
    tr.pos.y = clamp(tr.pos.y + dir * paddle_speed * dt, b.size.y * 0.5f,
                     field.y - b.size.y * 0.5f);
  }

  if (!g.serve_delay.finished) {
    g.serve_delay.tick(dt);
    return;
  }

  for (auto [e, tr, bl, bd] : reg.view<transform, ball, const body>().each()) {
    tr.pos += bl.velocity * dt;

    // Top and bottom walls.
    const f32 half = bd.size.y * 0.5f;
    if ((tr.pos.y < half && bl.velocity.y < 0.0f) ||
        (tr.pos.y > field.y - half && bl.velocity.y > 0.0f)) {
      tr.pos.y = clamp(tr.pos.y, half, field.y - half);
      bl.velocity = reflect(bl.velocity, {0.0f, bl.velocity.y < 0.0f ? 1.0f : -1.0f});
      sound_play_at(ctx, g.blip_wall, tr.pos);
    }

    // Paddles.
    for (auto [pe, ptr, pd, pb] : reg.view<const transform, const paddle, const body>().each()) {
      const contact hit = collide_rects(body_rect(tr, bd), body_rect(ptr, pb));
      // Only bounce when moving toward the paddle, so the ball never sticks.
      if (!hit.hit || dot(bl.velocity, hit.normal) >= 0.0f)
        continue;
      tr.pos += hit.normal * hit.depth;
      // Where it hits the paddle sets the new angle: the edge sends it steep.
      const f32 offset = clamp((tr.pos.y - ptr.pos.y) / (pb.size.y * 0.5f), -1.0f, 1.0f);
      const f32 speed = std::fmin(length(bl.velocity) * 1.06f, max_speed);
      const f32 angle = offset * 55.0f;
      bl.velocity = from_angle(pd.side == 0 ? angle : 180.0f - angle) * speed;
      g.rally++;
      sound_play_once_at(ctx, g.blip_paddle, 1.0f + 0.03f * (f32)g.rally, 1.0f);
    }

    // Scoring. After the winning point the ball just flies on, slowly.
    if (g.winner < 0 && (tr.pos.x < -ball_size || tr.pos.x > field.x + ball_size)) {
      const i32 scorer = tr.pos.x < 0.0f ? 1 : 0;
      g.score[scorer]++;
      g.best_rally = g.rally > g.best_rally ? g.rally : g.best_rally;
      sound_play_once(ctx, g.blip_score);
      g.shake.reset();
      if (g.score[scorer] >= winning_score) {
        g.winner = scorer;
        g.finish.reset();
        time_set_scale(ctx, 0.25f);
      } else {
        serve(ctx, reg, scorer == 0 ? 1 : 0);
      }
    }
  }
}

void over_update(njin_ctx &ctx) {
  if (action_pressed(ctx, g.confirm))
    scene_set(ctx, g.play);
  if (action_pressed(ctx, g.back))
    scene_set(ctx, g.menu);
}

// Drawing: the field in world space (through the camera)...
void draw_field(njin_ctx &ctx) {
  draw_rect_lines(ctx, rect{{0.0f, 0.0f}, field}, 3.0f, rgba{1, 1, 1, 0.25f});
  for (f32 y = 10.0f; y < field.y; y += 36.0f)
    draw_rect(ctx, rect{{field.x * 0.5f - 2.0f, y}, {4.0f, 18.0f}}, rgba{1, 1, 1, 0.2f});
  entt::registry &reg = world(ctx);
  for (auto [e, tr, b, p] : reg.view<const transform, const body, const paddle>().each())
    draw_rect(ctx, body_rect(tr, b), p.side == 0 ? colors::blue : colors::red);
  for (auto [e, tr, b, bl] : reg.view<const transform, const body, const ball>().each()) {
    // A short trail, brighter with speed, drawn additively.
    blend_begin(ctx, blend_additive);
    const vec2 dir = normalize(bl.velocity);
    for (i32 i = 1; i <= 4; i++)
      draw_circle(ctx, tr.pos - dir * (f32)i * 8.0f, ball_size * 0.5f,
                  rgba{1.0f, 0.8f, 0.3f, 0.15f});
    blend_end(ctx);
    draw_rect(ctx, body_rect(tr, b), colors::white);
  }
}

// ...and the UI in screen space.
void draw_play_ui(njin_ctx &ctx) {
  const vec2 screen = screen_size(ctx);
  const std::string score =
      std::to_string(g.score[0]) + "   " + std::to_string(g.score[1]);
  text_centered(ctx, score.c_str(), {screen.x * 0.5f, 50.0f}, 56.0f, colors::white);
  const std::string rally = "Rally " + std::to_string(g.rally) +
                            "    Best " + std::to_string(g.best_rally);
  text_centered(ctx, rally.c_str(), {screen.x * 0.5f, screen.y - 30.0f}, 22.0f,
                rgba{1, 1, 1, 0.6f});
  if (g.paused) {
    draw_rect(ctx, rect{{0, 0}, screen}, rgba{0, 0, 0, 0.6f});
    text_centered(ctx, "Tạm dừng", screen * 0.5f - vec2{0, 30}, 56.0f, colors::yellow);
    text_centered(ctx, "P / Esc: chơi tiếp    Enter: về menu",
                  screen * 0.5f + vec2{0, 30}, 24.0f, colors::white);
  }
}

void draw_menu(njin_ctx &ctx) {
  const vec2 screen = screen_size(ctx);
  // A title that bobs, eased with a sine wave.
  const f32 bob = std::sin(elapsed(ctx) * 2.0f) * 6.0f;
  text_centered(ctx, "PONG", {screen.x * 0.5f, screen.y * 0.3f + bob}, 110.0f,
                colors::white);
  text_centered(ctx, g.two_players ? "2 người chơi" : "1 người chơi (đấu máy)",
                {screen.x * 0.5f, screen.y * 0.55f}, 30.0f, colors::yellow);
  text_centered(ctx, "Enter: bắt đầu    Tab: đổi chế độ    F11: toàn màn hình    F12: chụp màn hình    Esc: thoát",
                {screen.x * 0.5f, screen.y * 0.7f}, 22.0f, rgba{1, 1, 1, 0.7f});
  text_centered(ctx, "Người 1: W / S    Người 2: Mũi tên lên / xuống",
                {screen.x * 0.5f, screen.y * 0.78f}, 22.0f, rgba{1, 1, 1, 0.7f});
  const std::string best = "Rally dài nhất: " + std::to_string(g.best_rally);
  text_centered(ctx, best.c_str(), {screen.x * 0.5f, screen.y * 0.9f}, 22.0f,
                rgba{1, 1, 1, 0.5f});
}

void draw_over(njin_ctx &ctx) {
  const vec2 screen = screen_size(ctx);
  const char *title = g.winner == 0 ? "Người 1 thắng!"
                      : g.two_players ? "Người 2 thắng!"
                                      : "Máy thắng!";
  text_centered(ctx, title, {screen.x * 0.5f, screen.y * 0.4f}, 72.0f,
                g.winner == 0 ? colors::blue : colors::red);
  const std::string score =
      std::to_string(g.score[0]) + " - " + std::to_string(g.score[1]);
  text_centered(ctx, score.c_str(), {screen.x * 0.5f, screen.y * 0.55f}, 40.0f,
                colors::white);
  text_centered(ctx, "Enter: chơi lại    Esc: về menu",
                {screen.x * 0.5f, screen.y * 0.7f}, 24.0f, rgba{1, 1, 1, 0.7f});
}

void setup(njin_ctx &ctx) {
  g.menu = scene_register(ctx, {.name = "menu"});
  g.play = scene_register(ctx, {.name = "play", .on_enter = enter_play, .on_exit = exit_play});
  g.over = scene_register(ctx, {.name = "over"});

  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_pre_update, global_input, "global_input");
  ecs_register(ctx, phase_pre_update, fit_camera, "fit_camera");
  ecs_register(ctx, phase_update, sys_desc{.fnc = menu_update, .scene = g.menu, .name = "menu_update"});
  ecs_register(ctx, phase_update, sys_desc{.fnc = play_input, .scene = g.play, .name = "play_input"});
  ecs_register(ctx, phase_update, sys_desc{.fnc = over_update, .scene = g.over, .name = "over_update"});
  ecs_register(ctx, phase_fixed_update, sys_desc{.fnc = play_physics, .scene = g.play, .name = "play_physics"});
  ecs_register(ctx, phase_render, sys_desc{.fnc = draw_field, .scene = g.play, .name = "draw_field"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = draw_menu, .scene = g.menu, .name = "draw_menu"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = draw_play_ui, .scene = g.play, .name = "draw_play_ui"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = draw_over, .scene = g.over, .name = "draw_over"});
}
} // namespace

njin::mod_desc pong_module() { return {.name = "pong", .setup = setup}; }
} // namespace pong
