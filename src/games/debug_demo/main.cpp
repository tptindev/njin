// A sandbox to try njin_inspector on. It opens the debug port on start, so run
// njin_inspector beside it (run_inspected.bat starts both) and watch what the
// keys below cost:
//
//   Up / Down   more / fewer bouncing balls (entities, collisions, draw calls)
//   Space       burst of particles
//   H           a system that burns ~3 ms of CPU per frame ("Systems" window)
//   M           hold 64 MB of RAM (the RAM graph in "Process" steps up)
//   G           make a 2048 x 2048 render texture, 32 MB of GPU memory ("Assets")
//   P           pause
//
// It also shows how a game feeds the inspector: debug_component() gives its
// own components a view and a size, debug_watch() publishes live numbers.
#include <chrono>
#include <njin.h>
#include <vector>

namespace {
using namespace njin;

struct ball {
  vec2 velocity{};
  f32 radius = 6.0f;
  i32 hue = 0;
  i32 bounces = 0;
};

struct demo_state {
  i32 wanted = 40;
  bool busy = false;
  std::vector<char> ram_hog;
  render_texture_handle big_target{};
  i32 collisions = 0;
} demo;

rgba hue_color(i32 hue) {
  static const rgba palette[] = {{0.95f, 0.45f, 0.4f, 1}, {0.95f, 0.75f, 0.3f, 1}, {0.5f, 0.85f, 0.5f, 1},
                                 {0.4f, 0.75f, 0.95f, 1}, {0.75f, 0.55f, 0.95f, 1}};
  return palette[(usize)hue % 5];
}

void spawn_ball(context &ctx) {
  entt::registry &reg = world(ctx);
  const vec2 screen = screen_size(ctx);
  rng &r = random(ctx);
  const entt::entity e = reg.create();
  const f32 radius = r.range(3.0f, 9.0f);
  reg.emplace<transform>(e, transform{.pos = {r.range(20.0f, screen.x - 20.0f), r.range(20.0f, screen.y - 20.0f)}});
  reg.emplace<collider>(e, collider{.shape = collider_circle, .radius = radius});
  reg.emplace<ball>(e, ball{.velocity = from_angle(r.range(0.0f, 360.0f)) * r.range(40.0f, 160.0f),
                            .radius = radius,
                            .hue = r.range(0, 5)});
}

void keep_count(context &ctx) {
  entt::registry &reg = world(ctx);
  const i32 have = (i32)reg.view<ball>().size();
  for (i32 i = have; i < demo.wanted; i++)
    spawn_ball(ctx);
  if (have > demo.wanted) {
    i32 extra = have - demo.wanted;
    std::vector<entt::entity> doomed;
    for (const entt::entity e : reg.view<ball>()) {
      if (extra-- <= 0)
        break;
      doomed.push_back(e);
    }
    reg.destroy(doomed.begin(), doomed.end());
  }
}

void input(context &ctx) {
  if (key_pressed(ctx, key_up))
    demo.wanted = std::min(demo.wanted + 50, 5000);
  if (key_pressed(ctx, key_down))
    demo.wanted = std::max(demo.wanted - 50, 0);
  if (key_pressed(ctx, key_space))
    particles_spawn(ctx, fx::explosion(), mouse_pos(ctx), 60);
  if (key_pressed(ctx, key_h))
    demo.busy = !demo.busy;
  if (key_pressed(ctx, key_m)) {
    if (demo.ram_hog.empty()) {
      demo.ram_hog.assign(64u << 20, 1); // touched, so it is really resident
    } else {
      demo.ram_hog.clear();
      demo.ram_hog.shrink_to_fit();
    }
  }
  if (key_pressed(ctx, key_g)) {
    if (demo.big_target.id == 0)
      demo.big_target = render_texture_load(ctx, 2048, 2048);
    else {
      render_texture_unload(ctx, demo.big_target);
      demo.big_target = {};
    }
  }
  if (key_pressed(ctx, key_p))
    time_set_paused(ctx, !time_paused(ctx));
  keep_count(ctx);
}

void move_balls(context &ctx) {
  entt::registry &reg = world(ctx);
  const vec2 screen = screen_size(ctx);
  const f32 dt = delta(ctx);
  for (auto [e, tr, b] : reg.view<transform, ball>().each()) {
    tr.pos += b.velocity * dt;
    if (tr.pos.x < b.radius || tr.pos.x > screen.x - b.radius) {
      b.velocity.x = -b.velocity.x;
      b.bounces++;
    }
    if (tr.pos.y < b.radius || tr.pos.y > screen.y - b.radius) {
      b.velocity.y = -b.velocity.y;
      b.bounces++;
    }
    tr.pos = clamp(tr.pos, {b.radius, b.radius}, {screen.x - b.radius, screen.y - b.radius});
  }
}

// Burns CPU on purpose, so it shows up in the Systems window.
void busy_work(context &) {
  if (!demo.busy)
    return;
  const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(3000);
  volatile f64 sink = 0;
  while (std::chrono::steady_clock::now() < until)
    sink = sink + 1.0;
}

void on_collision(const collision_enter &) { demo.collisions++; }

void draw(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, tr, b] : reg.view<const transform, const ball>().each())
    draw_circle(ctx, tr.pos, b.radius, hue_color(b.hue));
}

void hud(context &ctx) {
  char text[256];
  std::snprintf(text, sizeof text,
                "balls %d   Up/Down: more/fewer   Space: burst   H: burn CPU (%s)   M: 64 MB RAM (%s)   "
                "G: 32 MB GPU (%s)   P: pause",
                demo.wanted, demo.busy ? "on" : "off", demo.ram_hog.empty() ? "off" : "on",
                demo.big_target.id != 0 ? "on" : "off");
  draw_rect(ctx, rect{{0, 0}, {screen_size(ctx).x, 26}}, {0, 0, 0, 0.6f});
  draw_text(ctx, text, {8, 5}, 16, colors::white);
  draw_text(ctx, "njin_inspector: Process, Systems, Memory and Assets show what these cost", {8, screen_size(ctx).y - 22},
            16, {0.8f, 0.85f, 0.95f, 1});
}

void publish(context &ctx) {
  entt::registry &reg = world(ctx);
  debug_watch(ctx, "balls", (i32)reg.view<ball>().size());
  debug_watch(ctx, "collision events", demo.collisions);
  debug_watch(ctx, "burning cpu", demo.busy);
  debug_watch(ctx, "ram hog MB", (i32)(demo.ram_hog.size() >> 20));
}

void startup(context &ctx) {
  // Views and sizes for the game's own component: without this the inspector
  // shows its name only, and no size in the Memory window.
  debug_component<ball>(ctx, "ball", [](const ball &b) {
    return json_value::make_object().set("velocity", json_value::make_array().push(b.velocity.x).push(b.velocity.y))
        .set("radius", b.radius).set("bounces", b.bounces);
  });
  events(ctx).sink<collision_enter>().connect<&on_collision>();
  keep_count(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_update, input, "input");
  ecs_register(ctx, phase_update, move_balls, "move_balls");
  ecs_register(ctx, phase_update, busy_work, "busy_work");
  ecs_register(ctx, phase_update, publish, "publish");
  ecs_register(ctx, phase_render, draw, "draw_balls");
  ecs_register(ctx, phase_post_render, hud, "hud");
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "njin debug demo",
                                           .width = 1100,
                                           .height = 640,
                                           .target_fps = 60,
                                           .clear_bg_color = {0.09f, 0.10f, 0.13f, 1.0f}});
  njin::mod_register(*ctx, {.name = "demo", .setup = setup});
  // Always on here: this game exists to be inspected.
  njin::debug_server_start(*ctx);
  njin::run(*ctx);
  njin::destroy(ctx);
}
