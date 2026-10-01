#include "crowd.h"
#include "clock.h"
#include "person.h"
#include "physics.h"
#include "world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sandtable {

namespace {

constexpr i32 residents = 800;
// World units a second: the walk clip's stride, which scales with the man.
constexpr f32 walk_pace = 0.73f * city::person_height;
constexpr f32 turn_rate = 360.0f;     // degrees a second
constexpr f32 blend_time = 0.25f;     // easing from one motion to the next
constexpr f32 draw_range = 700.0f;    // world units from the middle of the view
constexpr f32 arrive = 5.0f;          // world units from a door
// Out of view a man walks this many times faster. The clock runs faster than
// feet (an hour in a minute, about as long as a walk across a district):
// unseen, the trip to work or to dinner takes a quarter of an hour of the
// town's time, not an hour. Those the camera sees walk at their true pace.
constexpr f32 unseen_boost = 4.0f;
// How far a man goes for a meal, an errand, a night out; and to work.
constexpr f32 near_reach = 500.0f, out_reach = 900.0f, work_reach = 900.0f;

// What a townsman is about.
enum class doing : u8 {
  sleep,  // at home, in the night
  home,   // at home, awake
  work,   // at his workplace, for his shift
  eat,    // a meal at an eatery or a café
  fun,    // a night out: karaoke, a bar, billiards
  errand, // the grocer's, the market, the pharmacy
  stroll, // walking about
};

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
  f32 wait = 0.0f;       // game seconds standing before walking on (a stroll's stops)
  bool seated = false;   // on a stool for good
  i32 stool_shop = -1;   // seated: the eatery he sits at, -1 none near
  u32 identity = 0;
  rgba tint{};
  rng r;

  // His day.
  i32 home = -1;                       // building
  i32 work = -1;                       // business, -1 none
  f32 wake = 6.0f, sleep = 22.0f;      // hours of the day
  f32 shift_from = 0.0f, shift_to = 0.0f;
  doing what = doing::home;
  i32 place = -1;    // building he goes to or is in; -1 strolling
  vec2 door{};       // its door
  bool inside = false;
  f64 until = 0.0;   // when he leaves where he is (clock_now())
  f32 stay = 0.0f;   // hours to stay once there
  i32 tries = 0;     // repaths on this leg
};

std::vector<townsman> folk;
i32 repaths = 0;
// Businesses of each kind, and the buildings people live in.
std::array<std::vector<i32>, static_cast<size_t>(city::business_kind::count)> by_kind;
std::vector<i32> homes;

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

// Whether a man can stand at `p`: any ground the foot grid lets him onto
// (street, sidewalk, square, yard, park), not a house, water or a stall.
bool walkable(vec2 p) {
  const city::city_map &m = world();
  return nav_cost(m.foot, nav_cell_at(m.foot, p)) > 0;
}

// Hours from `h` on to the hour of the day `to`.
f32 hours_to(f32 h, f32 to) { return std::fmod(to - h + 48.0f, 24.0f); }
f32 wrap(f32 h) { return std::fmod(h + 48.0f, 24.0f); }

vec2 random_spot(rng &r) {
  const city::city_map &m = world();
  for (i32 tries = 0; tries < 200; ++tries) {
    const vec2 p{r.range(0.0f, m.desc.width), r.range(0.0f, m.desc.height)};
    if (walkable(p))
      return m.center_of(static_cast<i32>(p.x / m.desc.cell), static_cast<i32>(p.y / m.desc.cell));
  }
  return {m.desc.width * 0.5f, m.desc.height * 0.5f};
}

// --- His day -------------------------------------------------------------------------

// A home, a job (or none) and the hours he keeps.
void give_life(townsman &t) {
  const city::city_map &m = world();
  t.home = homes.empty() ? -1 : homes[static_cast<size_t>(t.r.range(0, static_cast<i32>(homes.size()) - 1))];
  const vec2 at = t.home >= 0 ? m.buildings[static_cast<size_t>(t.home)].door : random_spot(t.r);
  if (t.r.chance(0.65f)) {
    std::vector<i32> near;
    for (i32 b = 0; b < static_cast<i32>(m.businesses.size()); ++b)
      if (distance(m.businesses[static_cast<size_t>(b)].door, at) < work_reach)
        near.push_back(b);
    if (!near.empty())
      t.work = near[static_cast<size_t>(t.r.range(0, static_cast<i32>(near.size()) - 1))];
  }
  if (t.work >= 0) {
    const opening o = business_hours(m.businesses[static_cast<size_t>(t.work)].kind);
    const f32 open_for = o.open == o.close ? 24.0f : hours_to(o.open, o.close);
    const f32 len = std::min(open_for, t.r.range(8.0f, 9.5f));
    if (o.open == o.close) { // round the clock: one of three shifts
      static constexpr f32 starts[] = {6.0f, 14.0f, 22.0f};
      t.shift_from = starts[t.r.range(0, 2)];
    } else {
      // The early shift, or for a long day the late one.
      t.shift_from = open_for > 11.0f && t.r.chance(0.4f) ? wrap(o.close - len) : wrap(o.open + t.r.range(0.0f, 0.5f));
    }
    t.shift_to = wrap(t.shift_from + len);
    t.wake = wrap(t.shift_from - t.r.range(1.0f, 2.0f));
  } else if (t.r.chance(0.15f)) { // a night owl
    t.wake = t.r.range(8.5f, 10.5f);
  } else {
    t.wake = t.r.range(5.0f, 7.5f);
  }
  t.sleep = wrap(t.wake - t.r.range(7.0f, 8.5f));
}

bool asleep_at(const townsman &t, f32 h) { return hour_between(h, t.sleep, t.wake); }
bool working_at(const townsman &t, f32 h) { return t.work >= 0 && hour_between(h, t.shift_from, t.shift_to); }

// An open business of one of `kinds` within `reach` of him, at random; -1 if none.
i32 find_open(townsman &t, std::initializer_list<city::business_kind> kinds, f32 reach) {
  const city::city_map &m = world();
  std::vector<i32> cand;
  for (const city::business_kind k : kinds)
    for (const i32 b : by_kind[static_cast<size_t>(k)])
      if (business_open(b) && distance(m.businesses[static_cast<size_t>(b)].door, t.pos) < reach)
        cand.push_back(b);
  if (cand.empty())
    return -1;
  return cand[static_cast<size_t>(t.r.range(0, static_cast<i32>(cand.size()) - 1))];
}

// What he does next, from now: where (t.place, -1 for a stroll) and how
// long once there (t.stay, hours).
void decide(townsman &t) {
  const city::city_map &m = world();
  const f32 h = state.hour;
  using bk = city::business_kind;
  const auto go_to_business = [&](i32 b, doing w, f32 stay) {
    t.what = w;
    t.place = m.businesses[static_cast<size_t>(b)].building;
    t.door = m.businesses[static_cast<size_t>(b)].door;
    t.stay = stay;
  };
  const auto go_home = [&](doing w, f32 stay) {
    t.what = w;
    t.place = t.home;
    t.door = t.home >= 0 ? m.buildings[static_cast<size_t>(t.home)].door : t.pos;
    t.stay = stay;
  };
  if (asleep_at(t, h)) {
    go_home(doing::sleep, hours_to(h, t.wake));
    return;
  }
  if (working_at(t, h)) {
    go_to_business(t.work, doing::work, hours_to(h, t.shift_to));
    return;
  }
  // Free: but not past the next thing he must do.
  f32 free_for = hours_to(h, t.sleep);
  if (t.work >= 0)
    free_for = std::min(free_for, hours_to(h, t.shift_from));
  const bool meal = hour_between(h, 6.0f, 8.5f) || hour_between(h, 11.0f, 13.0f) || hour_between(h, 17.5f, 20.0f);
  const bool evening = hour_between(h, 19.0f, 23.5f);
  const f32 roll = t.r.unit();
  i32 b = -1;
  if (hour_between(h, 23.5f, 5.0f)) {
    // Up late: a bar still open, or home.
    if (roll < 0.3f && (b = find_open(t, {bk::bar, bk::karaoke, bk::gambling_den, bk::street_food}, out_reach)) >= 0)
      go_to_business(b, doing::fun, t.r.range(0.5f, 1.5f));
    else
      go_home(doing::home, t.r.range(0.5f, 1.5f));
  } else if (meal && roll < 0.55f && (b = find_open(t, {bk::street_food, bk::cafe, bk::restaurant}, near_reach)) >= 0) {
    go_to_business(b, doing::eat, t.r.range(0.4f, 1.2f));
  } else if (evening && roll < 0.5f &&
             (b = find_open(t, {bk::karaoke, bk::bar, bk::billiards, bk::cafe, bk::street_food, bk::massage},
                            out_reach)) >= 0) {
    go_to_business(b, doing::fun, t.r.range(1.0f, 2.5f));
  } else if (roll < 0.35f || free_for < 0.75f) {
    go_home(doing::home, t.r.range(0.5f, 2.0f));
  } else if (roll < 0.6f && (b = find_open(t, {bk::grocery, bk::market, bk::pharmacy, bk::gold_shop, bk::pawn_shop,
                                               bk::bike_repair, bk::gas_station},
                                           near_reach)) >= 0) {
    go_to_business(b, doing::errand, t.r.range(0.15f, 0.5f));
  } else {
    t.what = doing::stroll;
    t.place = -1;
    t.stay = 0.0f;
  }
  t.stay = std::min(t.stay, std::max(0.1f, free_for));
}

bool path_to(townsman &t, vec2 goal, i32 max_nodes = 30000) {
  t.agent.path.clear();
  t.agent.next = 0;
  t.agent.reach = 3.0f;
  // A path in the open can be smoothed down to its one end point.
  return nav_find_path(world().foot, t.pos, goal, t.agent.path, {.max_nodes = max_nodes}) && !t.agent.path.empty();
}

// Somewhere to stroll to, a few blocks away: anywhere he can stand.
bool pick_stroll(townsman &t) {
  for (i32 tries = 0; tries < 4; ++tries) {
    const vec2 goal = t.pos + t.r.direction() * t.r.range(120.0f, 480.0f);
    if (walkable(goal) && path_to(t, goal, 6000))
      return true;
  }
  return false;
}

void go_inside(context &ctx, townsman &t) {
  t.inside = true;
  t.agent.path.clear();
  t.pos = t.door;
  t.until = clock_now() + static_cast<f64>(t.stay);
  if (t.body.id != 0 && character3d_active(ctx, t.body))
    character3d_set_active(ctx, t.body, false);
  set_act(t, act::idle);
}

// Sets off for what he decided: out of the door if he is in, on his way.
void set_off(context &ctx, townsman &t) {
  if (t.inside) {
    t.inside = false;
    t.pos = t.door;
    if (t.body.id != 0)
      character3d_set_position(ctx, t.body, to_phys(t.pos));
  }
  t.tries = 0;
  t.check_at = t.pos;
  t.check_time = 0.0f;
  const bool ok = t.place >= 0 ? path_to(t, t.door) : pick_stroll(t);
  if (!ok) {
    // Nowhere to go from here: a pause, then think again.
    t.place = -1;
    t.what = doing::stroll;
    t.agent.path.clear();
    t.wait = t.r.range(1.0f, 3.0f);
    set_act(t, act::idle);
    return;
  }
  set_act(t, act::walk);
}

// Inside (or about to be) and staying where he is: what next.
void think(context &ctx, townsman &t) {
  const i32 was_place = t.place;
  const bool was_inside = t.inside;
  decide(t);
  if (was_inside && t.place == was_place && t.place >= 0) {
    t.until = clock_now() + static_cast<f64>(t.stay); // stays on
    return;
  }
  set_off(ctx, t);
}

// Whether he is where the camera looks, close enough to be drawn: those are
// physics characters, walls and one another stopping them; the rest walk
// their paths untouched, as no one sees them (and they would cost a
// collision pass each every step).
bool in_view(vec2 p) {
  return state.cam_distance <= 40.0f && city::view_sees(p, 120.0f);
}

// The way he wants to go this step, world units a second at speed 1: along
// his path, or nothing when he has arrived.
vec2 want_velocity(townsman &t, f32 dt) {
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

// A seated customer is there while his eatery is open.
bool seated_now(const townsman &t) { return t.stool_shop < 0 || business_open(t.stool_shop); }

} // namespace

void crowd_spawn(context &ctx, u32 seed) {
  for (const townsman &t : folk)
    if (t.body.id != 0)
      character3d_destroy(ctx, t.body);
  folk.clear();
  repaths = 0;
  const city::city_map &m = world();
  for (auto &v : by_kind)
    v.clear();
  for (i32 b = 0; b < static_cast<i32>(m.businesses.size()); ++b)
    if (m.businesses[static_cast<size_t>(b)].building >= 0)
      by_kind[static_cast<size_t>(m.businesses[static_cast<size_t>(b)].kind)].push_back(b);
  homes.clear();
  for (i32 i = 0; i < static_cast<i32>(m.buildings.size()); ++i) {
    const city::building &b = m.buildings[static_cast<size_t>(i)];
    if (!b.door_ok || b.business >= 0)
      continue;
    if (b.kind == city::building_kind::tube_house || b.kind == city::building_kind::house)
      homes.push_back(i);
    else if (b.kind == city::building_kind::apartment) // many flats
      homes.insert(homes.end(), 4, i);
  }
  rng r(static_cast<u64>(seed) * 7919u + 17u);
  // Customers on the stools before the eateries, while they are open.
  for (const city::prop &p : m.props) {
    if (p.kind != city::prop_kind::stool || !r.chance(0.5f))
      continue;
    townsman t;
    t.pos = p.pos;
    t.facing = r.range(0.0f, 360.0f);
    t.now = act::sit;
    t.time = r.range(0.0f, 5.0f);
    t.seated = true;
    f32 best = 60.0f;
    for (const city::business_kind k : {city::business_kind::street_food, city::business_kind::cafe,
                                        city::business_kind::restaurant})
      for (const i32 b : by_kind[static_cast<size_t>(k)])
        if (const f32 d = distance(m.businesses[static_cast<size_t>(b)].door, p.pos); d < best) {
          best = d;
          t.stool_shop = b;
        }
    physics_seated(ctx, t.pos);
    t.tint = clothes[r.range(0, 7)];
    t.r = rng(r.next_u32());
    t.identity = seed * 7919u + static_cast<u32>(folk.size()) + 1u;
    folk.push_back(t);
  }
  // The townsfolk, each where his day has him now: most indoors, leaving at
  // staggered times; the strollers out on the street.
  for (i32 i = 0; i < residents; ++i) {
    townsman t;
    t.r = rng(r.next_u32());
    give_life(t);
    t.pos = t.home >= 0 ? m.buildings[static_cast<size_t>(t.home)].door : random_spot(t.r);
    decide(t);
    t.facing = t.r.range(0.0f, 360.0f);
    t.tint = clothes[t.r.range(0, 7)];
    t.time = t.r.range(0.0f, 2.0f);
    t.identity = seed * 7919u + static_cast<u32>(folk.size()) + 1u;
    if (t.place >= 0) {
      t.inside = true;
      t.pos = t.door;
      t.until = clock_now() + static_cast<f64>(t.r.range(0.0f, std::min(t.stay, 1.5f)));
    } else {
      t.pos = random_spot(t.r);
      t.wait = t.r.range(0.0f, 3.0f);
    }
    t.body = physics_person(ctx, t.pos);
    character3d_set_active(ctx, t.body, false);
    t.check_at = t.pos;
    folk.push_back(t);
  }
}

void crowd_step(context &ctx, f32 dt) {
  const f32 speed = clock_speed();
  const f32 gdt = dt * speed; // game seconds this step
  const f64 now = clock_now();
  for (townsman &t : folk) {
    if (t.seated || t.body.id == 0)
      continue;
    if (t.inside) {
      // Time to go (or the clock was set back by hand: think again).
      if (speed > 0.0f && (now >= t.until || t.until - now > 16.0))
        think(ctx, t);
      if (t.inside)
        continue;
    }
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
    turn(t, moved, dt * std::max(speed, 1.0f));

    vec2 want{};
    if (speed <= 0.0f) {
      // The town is paused: he stands where he is.
    } else if (t.wait > 0.0f) {
      t.wait -= gdt;
      if (t.wait <= 0.0f)
        think(ctx, t);
    } else if (t.agent.done() || (t.place >= 0 && distance(t.pos, t.door) < arrive)) {
      if (t.place >= 0) {
        go_inside(ctx, t);
        continue;
      }
      // The end of a stroll: a word with someone, or a look round.
      set_act(t, t.r.chance(0.4f) ? act::talk : act::idle);
      t.wait = t.r.range(2.0f, 9.0f);
    } else {
      const f32 boost = on ? 1.0f : unseen_boost;
      want = want_velocity(t, gdt * boost) * (speed * boost);
      // Stuck: another way there; after a few, he gives it up.
      t.check_time += gdt;
      if (t.check_time >= stuck_time) {
        if (distance(t.pos, t.check_at) < walk_pace * stuck_time * stuck_share) {
          ++repaths;
          const bool again = ++t.tries <= 3 && (t.place >= 0 ? path_to(t, t.door) : pick_stroll(t));
          if (!again) {
            t.place = -1;
            t.what = doing::stroll;
            t.agent.path.clear();
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
  const f32 speed = clock_speed();
  for (townsman &t : folk) {
    if (t.inside)
      continue;
    // Walking, the clip keeps pace with his real speed: held up, he steps
    // slower instead of sliding.
    const f32 pace = t.now == act::walk ? clamp(t.speed / walk_pace, 0.25f, 3.5f) : std::max(speed, 0.2f);
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
    if (a.seated) {
      r.seated += seated_now(a) ? 1 : 0;
      continue;
    }
    ++r.residents;
    if (a.inside) {
      r.asleep += a.what == doing::sleep ? 1 : 0;
      r.at_work += a.what == doing::work ? 1 : 0;
      continue;
    }
    ++r.walkers;
    for (size_t j = i + 1; j < folk.size(); ++j)
      if (!folk[j].seated && !folk[j].inside && distance(a.pos, folk[j].pos) < touch * 0.8f)
        ++r.overlapping;
    if (const city::cell_info *c = m.cell_at(a.pos))
      if (c->building >= 0 && m.buildings[static_cast<size_t>(c->building)].box.contains(a.pos))
        ++r.in_buildings;
  }
  return r;
}

namespace {
void draw_townsman(context &ctx, const townsman &t) {
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

bool shown(const townsman &t) { return t.seated ? seated_now(t) : !t.inside; }
} // namespace

void crowd_draw(context &ctx) {
  if (!person_ready() || state.cam_distance > 40.0f)
    return;
  const f32 range = std::min(draw_range, 200.0f + state.cam_distance * 18.0f);
  material3d_set(ctx, {.specular = 0.15f, .shininess = 16.0f});
  const city::view_options &v = world_view();
  for (const townsman &t : folk) {
    // Only those in view; with something in focus, only those round it.
    if (!shown(t) || distance(t.pos, state.cam_target) > range || !city::view_sees(t.pos) ||
        (v.focused && distance(t.pos, v.focus) > v.focus_radius * 1.3f))
      continue;
    draw_townsman(ctx, t);
  }
  material3d_set(ctx, {});
}

void crowd_draw_around(context &ctx, vec2 at, f32 range) {
  if (!person_ready())
    return;
  material3d_set(ctx, {.specular = 0.15f, .shininess = 16.0f});
  for (const townsman &t : folk)
    if (shown(t) && distance(t.pos, at) <= range)
      draw_townsman(ctx, t);
  material3d_set(ctx, {});
}

i32 crowd_size() { return static_cast<i32>(folk.size()); }

} // namespace sandtable
