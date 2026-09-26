#include "body.h"
#include "_comps.h"
#include "_math.h"
#include "njin_body.h"
#include "njin_collision.h"
#include "njin_ctx.h"
#include "njin_input.h"
#include "njin_ctx_impl.h"
#include <algorithm>
#include <cmath>

namespace njin {
namespace {
f32 sign_of(f32 v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

// Actions to body inputs, once per frame. The press flags only ever get set
// here: the body clears them when a fixed step has used them, so a press on a
// frame without any fixed step still reaches the next one.
void read_inputs(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, body, map] : reg.view<platformer_body, const platformer_input_map>().each()) {
    body.input.move_x = axis_value(ctx, map.move);
    const bool down = map.down.id != 0 && action_held(ctx, map.down);
    if (action_pressed(ctx, map.jump)) {
      if (down)
        body.input.drop = true;
      else
        body.input.jump = true;
    }
    body.input.jump_held = action_held(ctx, map.jump) || action_pressed(ctx, map.jump);
  }
  for (auto [e, body, map] : reg.view<topdown_body, const topdown_input_map>().each()) {
    body.input.move = {axis_value(ctx, map.move_x), axis_value(ctx, map.move_y)};
    if (map.dash.id != 0 && action_pressed(ctx, map.dash))
      body.input.dash = true;
  }
}

void move_paths(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  const f32 dt = delta(ctx);
  for (auto [e, tr, path] : reg.view<transform, path_mover>().each()) {
    const i32 count = (i32)path.points.size();
    if (path.paused || count < 2 || path.speed <= 0.0f)
      continue;
    if (path.wait_timer > 0.0f) {
      path.wait_timer -= dt;
      continue;
    }
    path.target = std::clamp(path.target, 0, count - 1);
    f32 budget = path.speed * dt;
    vec2 pos = tr.pos;
    // Several points may be passed in one step when they are close together.
    for (i32 guard = 0; guard < count * 2 && budget > 0.0f; guard++) {
      const vec2 goal = path.points[(usize)path.target];
      const f32 dist = distance(pos, goal);
      if (dist > budget) {
        pos = move_toward(pos, goal, budget);
        break;
      }
      pos = goal;
      budget -= dist;
      if (path.loop) {
        path.target = (path.target + 1) % count;
      } else {
        if (path.target + path.direction < 0 || path.target + path.direction >= count)
          path.direction = -path.direction;
        path.target += path.direction;
      }
      if (path.wait > 0.0f) {
        path.wait_timer = path.wait;
        break;
      }
    }
    const vec2 d = pos - tr.pos;
    const collider *col = reg.try_get<collider>(e);
    if (col != nullptr && col->shape == collider_box)
      collision_move_platform(ctx, e, d);
    else
      tr.pos = pos;
  }
}

void step_platformers(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  const f32 dt = delta(ctx);
  entt::dispatcher &bus = events(ctx);
  for (auto [e, body] : reg.view<platformer_body>().each()) {
    if (!reg.all_of<transform, collider>(e))
      continue;
    platformer_input &in = body.input;
    body.jumped = false;
    body.landed = false;
    const bool was_grounded = body.grounded;

    if (body.grounded) {
      body.coyote_timer = body.coyote_time;
      body.air_jumps_left = body.air_jumps;
    } else {
      body.coyote_timer -= dt;
    }
    if (in.jump) {
      body.buffer_timer = body.jump_buffer;
      in.jump = false;
    } else {
      body.buffer_timer -= dt;
    }
    body.drop_timer -= dt;
    body.wall_lock_timer -= dt;

    // Dropping through the one-way platform underfoot.
    if (in.drop) {
      in.drop = false;
      if (body.grounded && body.ground_one_way) {
        body.drop_timer = 0.25f;
        body.grounded = false;
        body.coyote_timer = 0.0f;
      } else {
        body.buffer_timer = body.jump_buffer; // no platform: a plain jump
      }
    }

    // Run.
    const f32 move = body.wall_lock_timer > 0.0f ? 0.0f : clamp(in.move_x, -1.0f, 1.0f);
    if (move != 0.0f)
      body.facing = move > 0.0f ? 1 : -1;
    const f32 target = move * body.run_speed;
    const bool speeding = move != 0.0f && (sign_of(body.velocity.x) == sign_of(move) ||
                                            body.velocity.x == 0.0f) &&
                          std::abs(body.velocity.x) < std::abs(target);
    const f32 rate = body.grounded ? (speeding ? body.ground_accel : body.ground_decel)
                                   : (speeding ? body.air_accel : body.air_decel);
    body.velocity.x = move_toward(body.velocity.x, target, rate * dt);

    // Jump: from the ground (or just after leaving it), off a wall, in the air.
    if (body.buffer_timer > 0.0f) {
      if (body.grounded || body.coyote_timer > 0.0f) {
        body.velocity.y = -body.jump_speed;
        body.jumped = true;
        bus.enqueue(body_jumped{e, false, false});
      } else if (body.on_wall != 0 && (body.wall_jump.x != 0.0f || body.wall_jump.y != 0.0f)) {
        body.velocity = {-(f32)body.on_wall * body.wall_jump.x, -body.wall_jump.y};
        body.facing = -body.on_wall;
        body.wall_lock_timer = body.wall_jump_lock;
        body.jumped = true;
        bus.enqueue(body_jumped{e, false, true});
      } else if (body.air_jumps_left > 0) {
        body.air_jumps_left--;
        body.velocity.y = -body.jump_speed;
        body.jumped = true;
        bus.enqueue(body_jumped{e, true, false});
      }
      if (body.jumped) {
        body.buffer_timer = 0.0f;
        body.coyote_timer = 0.0f;
        body.grounded = false;
        body.rising = true;
      }
    }
    // Let go early for a short hop.
    if (body.rising && !in.jump_held && body.velocity.y < 0.0f) {
      body.velocity.y *= body.jump_cut;
      body.rising = false;
    }
    if (body.velocity.y >= 0.0f)
      body.rising = false;

    // Fall.
    const f32 g = body.velocity.y > 0.0f ? body.fall_gravity : body.gravity;
    body.velocity.y = std::min(body.velocity.y + g * dt, body.max_fall);
    if (body.wall_slide_speed > 0.0f && body.on_wall != 0 && !body.grounded &&
        body.velocity.y > body.wall_slide_speed && (f32)body.on_wall * move > 0.0f)
      body.velocity.y = body.wall_slide_speed;

    // Move.
    collision_move_opts opts{};
    opts.drop_through = body.drop_timer > 0.0f;
    const vec2 step = body.velocity * dt;
    if (was_grounded && !body.jumped && body.velocity.y >= 0.0f)
      opts.snap_down = std::abs(step.x) + 2.0f;
    const f32 fall_speed = body.velocity.y;
    const collision_move_result r = collision_move(ctx, e, step, opts);

    if (r.hit_x)
      body.velocity.x = 0.0f;
    if (r.hit_y && body.velocity.y < 0.0f)
      body.velocity.y = 0.0f; // bumped a ceiling
    body.grounded = r.grounded && body.velocity.y >= 0.0f;
    if (body.grounded)
      body.velocity.y = 0.0f;
    body.ground = body.grounded ? r.ground : entt::null;
    body.on_slope = body.grounded && r.on_slope;
    body.ground_one_way = body.grounded && r.ground_one_way;
    if (body.grounded && !was_grounded) {
      body.landed = true;
      bus.enqueue(body_landed{e, fall_speed});
    }

    // Against a wall: probe one pixel to each side.
    body.on_wall = 0;
    if (!body.grounded) {
      collision_move_opts probe{};
      probe.test_only = true;
      probe.drop_through = true;
      if (collision_move(ctx, e, {1.0f, 0.0f}, probe).hit_x)
        body.on_wall = 1;
      else if (collision_move(ctx, e, {-1.0f, 0.0f}, probe).hit_x)
        body.on_wall = -1;
    }
  }
}

void step_topdowns(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  const f32 dt = delta(ctx);
  entt::dispatcher &bus = events(ctx);
  for (auto [e, body] : reg.view<topdown_body>().each()) {
    if (!reg.all_of<transform, collider>(e))
      continue;
    topdown_input &in = body.input;
    vec2 move = in.move;
    if (length_sq(move) > 1.0f)
      move = normalize(move);
    if (length_sq(move) > 1e-4f)
      body.facing = normalize(move);

    body.cooldown_timer -= dt;
    if (in.dash) {
      in.dash = false;
      if (body.dash_speed > 0.0f && body.cooldown_timer <= 0.0f && !body.dashing) {
        body.dashing = true;
        body.dash_timer = body.dash_time;
        body.cooldown_timer = body.dash_cooldown;
        body.velocity = body.facing * body.dash_speed;
        bus.enqueue(body_dashed{e, body.facing});
      }
    }
    if (body.dashing) {
      body.dash_timer -= dt;
      if (body.dash_timer <= 0.0f) {
        body.dashing = false;
        body.velocity = body.velocity * (body.speed / std::max(body.dash_speed, 1.0f));
      }
    } else {
      const vec2 target = move * body.speed;
      const f32 rate = length_sq(move) > 1e-4f ? body.accel : body.decel;
      body.velocity = move_toward(body.velocity, target, rate * dt);
    }

    const collision_move_result r = collision_move(ctx, e, body.velocity * dt);
    if (r.hit_x)
      body.velocity.x = 0.0f;
    if (r.hit_y)
      body.velocity.y = 0.0f;
    body.moving = length_sq(r.moved) > 1e-6f;
  }
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_pre_update, read_inputs, "read_inputs");
  ecs_register(ctx, phase_fixed_update, sys_desc{.fnc = move_paths, .order = 10, .name = "move_paths"});
  ecs_register(ctx, phase_fixed_update, sys_desc{.fnc = step_platformers, .order = 20, .name = "step_platformers"});
  ecs_register(ctx, phase_fixed_update, sys_desc{.fnc = step_topdowns, .order = 20, .name = "step_topdowns"});
}
} // namespace

mod_desc body_module() { return mod_desc{.name = "njin.body", .setup = setup}; }
} // namespace njin
