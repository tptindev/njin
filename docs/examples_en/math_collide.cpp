#include <njin.h>
#include <vector>

namespace {
struct bullet {
  njin::vec2 pos;
  njin::vec2 vel;
};
std::vector<bullet> bullets;
njin::rect enemy{{300, 100}, {40, 40}};

void shoot(njin::njin_ctx &ctx) {
  if (!njin::mouse_pressed(ctx, njin::mouse_left))
    return;
  const njin::vec2 from{100, 200};
  const njin::vec2 target = njin::scr2w(ctx, njin::mouse_pos(ctx));
  // Aims at the mouse, with a random deviation of up to 5 degrees.
  const njin::f32 spread = njin::random(ctx).range(-5.0f, 5.0f);
  const njin::vec2 dir = njin::rotate(njin::normalize(target - from), spread);
  bullets.push_back({from, dir * 400.0f});
}

void move(njin::njin_ctx &ctx) {
  const njin::f32 dt = njin::delta(ctx);
  for (bullet &b : bullets) {
    b.pos += b.vel * dt;
    // Bounce off the enemy: push out, then reflect the velocity.
    const njin::contact hit = njin::collide_circle_rect({b.pos, 4}, enemy);
    if (hit.hit) {
      b.pos += hit.normal * hit.depth;
      b.vel = njin::reflect(b.vel, hit.normal);
    }
  }
  // Remove bullets that have flown far away.
  std::erase_if(bullets, [](const bullet &b) {
    return njin::length_sq(b.pos) > 2000.0f * 2000.0f;
  });
}

void draw(njin::njin_ctx &ctx) {
  njin::draw_rect(ctx, enemy, njin::colors::red);
  for (const bullet &b : bullets)
    njin::draw_circle(ctx, b.pos, 4, njin::colors::yellow);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_update, shoot);
  njin::ecs_register(ctx, njin::phase_fixed_update, move);
  njin::ecs_register(ctx, njin::phase_render, draw);
}
} // namespace

njin::mod_desc shooter_module() { return {.name = "shooter", .setup = setup}; }
