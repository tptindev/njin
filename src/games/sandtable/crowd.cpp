#include "crowd.h"
#include "person.h"
#include "physics.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sandtable {

namespace {

constexpr i32 walkers = 220;
// World units a second: the walk clip's stride, which scales with the man.
constexpr f32 walk_pace = 0.73f * city::person_height;
constexpr f32 turn_rate = 360.0f;     // degrees a second
constexpr f32 blend_time = 0.25f;     // easing from one motion to the next
constexpr f32 draw_range = 700.0f;    // world units from the middle of the view

struct townsman {
  vec2 pos{};
  f32 facing = 0.0f;
  character3d_handle body{}; // walkers only
  vec2 walked{};             // out of view: the step he takes himself
  f32 speed = 0.0f;          // how fast he really moved, world units a second
  vec2 check_at{};           // where he was when last checked for being stuck
  f32 check_time = 0.0f;
  nav_agent agent;
  act now = act::idle, was = act::idle;
  f32 time = 0.0f, was_time = 0.0f, blend = 0.0f;
  f32 wait = 0.0f;       // seconds before walking on
  bool seated = false;   // on a stool for good
  u32 identity = 0;
  rgba tint{};
  rng r;
};

std::vector<townsman> folk;
i32 repaths = 0;

// A walker making less than this share of his pace over `stuck_time` is stuck
// (against a wall, a crowd, a parked bike) and looks for another way.
constexpr f32 stuck_time = 1.5f;
constexpr f32 stuck_share = 0.25f;

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

// The way he wants to go this step, world units a second: along his path,
// or nothing when he has arrived.
vec2 want_velocity(townsman &t, f32 dt) {
  if (t.agent.next >= static_cast<i32>(t.agent.path.size())) {
    set_act(t, t.r.chance(0.4f) ? act::talk : act::idle);
    t.wait = t.r.range(2.0f, 9.0f);
    return {};
  }
  // nav_steer is the way to go (length 1), toward the next point of the path.
  const vec2 dir = nav_steer(t.agent, t.pos);
  if (length_sq(dir) < 1e-6f)
    return {};
  // Slowing only for the last point, so as not to walk past it in one step.
  const bool last = t.agent.next + 1 >= static_cast<i32>(t.agent.path.size());
  const f32 left = distance(t.agent.path[static_cast<size_t>(t.agent.next)], t.pos);
  return dir * (last ? std::min(walk_pace, left / std::max(dt, 1e-4f)) : walk_pace);
}

// Faces the way he really moves, no faster than a man turns.
void turn(townsman &t, vec2 moved, f32 dt) {
  if (length_sq(moved) < 1e-6f)
    return;
  f32 diff = std::fmod(angle_of(moved) - t.facing + 540.0f, 360.0f) - 180.0f;
  diff = clamp(diff, -turn_rate * dt, turn_rate * dt);
  t.facing += diff;
}

} // namespace

void crowd_spawn(context &ctx, u32 seed) {
  for (const townsman &t : folk)
    if (t.body.id != 0)
      character3d_destroy(ctx, t.body);
  folk.clear();
  repaths = 0;
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
    physics_seated(ctx, t.pos);
    t.tint = clothes[r.range(0, 7)];
    t.r = rng(r.next_u32());
    t.identity = seed * 7919u + static_cast<u32>(folk.size()) + 1u;
    folk.push_back(t);
  }
  for (i32 i = 0; i < walkers; ++i) {
    townsman t;
    t.r = rng(r.next_u32());
    t.pos = random_sidewalk(t.r);
    t.body = physics_person(ctx, t.pos);
    t.check_at = t.pos;
    t.facing = t.r.range(0.0f, 360.0f);
    t.tint = clothes[t.r.range(0, 7)];
    t.time = t.r.range(0.0f, 2.0f);
    t.wait = t.r.range(0.0f, 3.0f);
    t.identity = seed * 7919u + static_cast<u32>(folk.size()) + 1u;
    folk.push_back(t);
  }
}

// Whether he is where the camera looks, close enough to be drawn: those are
// physics characters, walls and one another stopping them; the rest walk
// their paths untouched, as no one sees them (and they would cost a
// collision pass each every step).
bool in_view(vec2 p) {
  return state.cam_distance <= 40.0f && city::view_sees(p, 120.0f);
}

void crowd_step(context &ctx, f32 dt) {
  for (townsman &t : folk) {
    if (t.seated || t.body.id == 0)
      continue;
    const bool on = in_view(t.pos);
    if (on != character3d_active(ctx, t.body)) {
      if (on)
        character3d_set_position(ctx, t.body, to_phys(t.pos));
      character3d_set_active(ctx, t.body, on);
    }
    // Where the last step took him: the physics, or his own walk.
    const vec2 was = t.pos;
    if (on)
      t.pos = from_phys(character3d_position(ctx, t.body));
    else
      t.pos += t.walked;
    t.walked = {};
    const vec2 moved = t.pos - was;
    t.speed = length(moved) / std::max(dt, 1e-4f);
    turn(t, moved, dt);

    vec2 want{};
    if (t.wait > 0.0f) {
      t.wait -= dt;
      if (t.wait <= 0.0f) {
        if (pick_goal(t)) {
          set_act(t, act::walk);
          t.check_at = t.pos;
          t.check_time = 0.0f;
        } else {
          t.wait = t.r.range(1.0f, 3.0f);
        }
      }
    } else {
      want = want_velocity(t, dt);
      // Stuck: another way, or a pause and then somewhere else.
      t.check_time += dt;
      if (t.check_time >= stuck_time) {
        if (distance(t.pos, t.check_at) < walk_pace * stuck_time * stuck_share) {
          ++repaths;
          if (!pick_goal(t)) {
            set_act(t, act::idle);
            t.wait = t.r.range(0.5f, 2.0f);
            want = {};
          }
        }
        t.check_at = t.pos;
        t.check_time = 0.0f;
      }
    }
    // On the ground he walks; off it he falls.
    const f32 fall = character3d_grounded(ctx, t.body) ? 0.0f
                                                       : character3d_velocity(ctx, t.body).y - 9.81f * dt;
    const vec3 v = to_phys(want);
    character3d_set_velocity(ctx, t.body, {v.x, fall, v.z});
    t.walked = want * dt;
  }
}

void crowd_update(f32 dt) {
  for (townsman &t : folk) {
    // Walking, the clip keeps pace with his real speed: held up, he steps
    // slower instead of sliding.
    const f32 pace = t.now == act::walk ? clamp(t.speed / walk_pace, 0.25f, 1.2f) : 1.0f;
    t.time += dt * pace;
    t.was_time += dt;
    t.blend = std::max(0.0f, t.blend - dt / blend_time);
  }
}

crowd_report crowd_check() {
  crowd_report r;
  r.repaths = repaths;
  const city::city_map &m = world();
  const f32 touch = 2.0f * 0.22f / metres_per_unit;
  for (size_t i = 0; i < folk.size(); ++i) {
    const townsman &a = folk[i];
    if (a.seated)
      continue;
    ++r.walkers;
    for (size_t j = i + 1; j < folk.size(); ++j)
      if (!folk[j].seated && distance(a.pos, folk[j].pos) < touch * 0.8f)
        ++r.overlapping;
    if (const city::cell_info *c = m.cell_at(a.pos))
      if (c->building >= 0 && m.buildings[static_cast<size_t>(c->building)].box.contains(a.pos))
        ++r.in_buildings;
  }
  return r;
}

void crowd_draw(context &ctx) {
  if (!person_ready() || state.cam_distance > 40.0f)
    return;
  const f32 range = std::min(draw_range, 200.0f + state.cam_distance * 18.0f);
  material3d_set(ctx, {.specular = 0.15f, .shininess = 16.0f});
  for (const townsman &t : folk) {
    // Only those in view; with something in focus, only those round it.
    const city::view_options &v = world_view();
    if (distance(t.pos, state.cam_target) > range || !city::view_sees(t.pos) ||
        (v.focused && distance(t.pos, v.focus) > v.focus_radius * 1.3f))
      continue;
    draw_person(ctx, {.at = t.pos,
                      .facing = t.facing,
                      .now = t.now,
                      .time = t.time,
                      .was = t.was,
                      .was_time = t.was_time,
                      .blend = t.blend,
                      .tint = t.tint,
                      .lift = 0.04f,
                      .identity = t.identity});
  }
  material3d_set(ctx, {});
}

i32 crowd_size() { return static_cast<i32>(folk.size()); }

} // namespace sandtable
