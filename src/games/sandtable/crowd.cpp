#include "crowd.h"
#include "person.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sandtable {

namespace {

constexpr i32 walkers = 220;
constexpr f32 walk_pace = 9.5f;       // world units a second: the walk clip's stride
constexpr f32 turn_rate = 360.0f;     // degrees a second
constexpr f32 blend_time = 0.25f;     // easing from one motion to the next
constexpr f32 draw_range = 700.0f;    // world units from the middle of the view

struct townsman {
  vec2 pos{};
  f32 facing = 0.0f;
  nav_agent agent;
  act now = act::idle, was = act::idle;
  f32 time = 0.0f, was_time = 0.0f, blend = 0.0f;
  f32 wait = 0.0f;       // seconds before walking on
  bool seated = false;   // on a stool for good
  rgba tint{};
  rng r;
};

std::vector<townsman> folk;

// Clothes: faded everyday colours.
const rgba clothes[] = {{0.85f, 0.82f, 0.76f, 1}, {0.55f, 0.62f, 0.72f, 1}, {0.72f, 0.58f, 0.48f, 1},
                        {0.62f, 0.70f, 0.58f, 1}, {0.80f, 0.70f, 0.52f, 1}, {0.52f, 0.52f, 0.56f, 1},
                        {0.86f, 0.64f, 0.62f, 1}, {0.46f, 0.56f, 0.66f, 1}};

void set_act(townsman &t, act a) {
  if (t.now == a)
    return;
  t.was = t.now;
  t.was_time = t.time;
  t.now = a;
  t.time = 0.0f;
  t.blend = 1.0f;
}

// Somewhere to walk to: a sidewalk or alley cell a few blocks away.
bool pick_goal(townsman &t) {
  const city::city_map &m = world();
  for (i32 tries = 0; tries < 12; ++tries) {
    const vec2 goal = t.pos + t.r.direction() * t.r.range(120.0f, 480.0f);
    const city::cell_info *c = m.cell_at(goal);
    if (!c || c->g != city::ground::road || (c->carriage && c->road != city::road_kind::alley))
      continue;
    t.agent.path.clear();
    t.agent.next = 0;
    if (nav_find_path(m.foot, t.pos, goal, t.agent.path, {.max_nodes = 6000}) && t.agent.path.size() > 1)
      return true;
  }
  return false;
}

vec2 random_sidewalk(rng &r) {
  const city::city_map &m = world();
  for (i32 tries = 0; tries < 200; ++tries) {
    const vec2 p{r.range(0.0f, m.desc.width), r.range(0.0f, m.desc.height)};
    const city::cell_info *c = m.cell_at(p);
    if (c && c->g == city::ground::road && !c->carriage)
      return m.center_of(static_cast<i32>(p.x / m.desc.cell), static_cast<i32>(p.y / m.desc.cell));
  }
  return {m.desc.width * 0.5f, m.desc.height * 0.5f};
}

void walk(townsman &t, f32 dt) {
  if (t.agent.next >= static_cast<i32>(t.agent.path.size())) {
    set_act(t, t.r.chance(0.4f) ? act::talk : act::idle);
    t.wait = t.r.range(2.0f, 9.0f);
    return;
  }
  const vec2 to = nav_steer(t.agent, t.pos);
  const vec2 d = to - t.pos;
  const f32 len = length(d);
  if (len < 0.01f)
    return;
  const vec2 dir = d / len;
  t.pos += dir * std::min(len, walk_pace * dt);
  // Turn toward the way he walks, no faster than a man turns.
  const f32 want = angle_of(dir);
  f32 diff = std::fmod(want - t.facing + 540.0f, 360.0f) - 180.0f;
  diff = clamp(diff, -turn_rate * dt, turn_rate * dt);
  t.facing += diff;
}

} // namespace

void crowd_spawn(u32 seed) {
  folk.clear();
  const city::city_map &m = world();
  rng r(static_cast<u64>(seed) * 7919u + 17u);
  // Customers on the stools before the eateries.
  for (const city::prop &p : m.props) {
    if (p.kind != city::prop_kind::stool || !r.chance(0.5f))
      continue;
    townsman t;
    t.pos = p.pos;
    t.facing = r.range(0.0f, 360.0f);
    t.now = act::sit;
    t.time = r.range(0.0f, 5.0f);
    t.seated = true;
    t.tint = clothes[r.range(0, 7)];
    t.r = rng(r.next_u32());
    folk.push_back(t);
  }
  for (i32 i = 0; i < walkers; ++i) {
    townsman t;
    t.r = rng(r.next_u32());
    t.pos = random_sidewalk(t.r);
    t.facing = t.r.range(0.0f, 360.0f);
    t.tint = clothes[t.r.range(0, 7)];
    t.time = t.r.range(0.0f, 2.0f);
    t.wait = t.r.range(0.0f, 3.0f);
    folk.push_back(t);
  }
}

void crowd_update(f32 dt) {
  for (townsman &t : folk) {
    t.time += dt;
    t.was_time += dt;
    t.blend = std::max(0.0f, t.blend - dt / blend_time);
    if (t.seated)
      continue;
    if (t.wait > 0.0f) {
      t.wait -= dt;
      if (t.wait <= 0.0f) {
        if (pick_goal(t))
          set_act(t, act::walk);
        else
          t.wait = t.r.range(1.0f, 3.0f);
      }
      continue;
    }
    walk(t, dt);
  }
}

void crowd_draw(context &ctx) {
  if (!person_ready() || state.cam_distance > 40.0f)
    return;
  const f32 range = std::min(draw_range, 200.0f + state.cam_distance * 18.0f);
  material3d_set(ctx, {.specular = 0.15f, .shininess = 16.0f});
  for (const townsman &t : folk) {
    if (distance(t.pos, state.cam_target) > range)
      continue;
    draw_person(ctx, {.at = t.pos,
                      .facing = t.facing,
                      .now = t.now,
                      .time = t.time,
                      .was = t.was,
                      .was_time = t.was_time,
                      .blend = t.blend,
                      .tint = t.tint,
                      .lift = 0.04f});
  }
  material3d_set(ctx, {});
}

i32 crowd_size() { return static_cast<i32>(folk.size()); }

} // namespace sandtable
