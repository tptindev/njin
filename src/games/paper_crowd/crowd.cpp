// The crowd: nobody is steered by the player. Each person holds one activity
// until its timer runs out, then picks the next one by weight. Some choices
// reach out to someone else (follow a friend, chase, join a ring), but the
// decision is always the chooser's own. Movement goes through topdown_body,
// exactly as moteswarm's NPCs do: the AI writes body.input.move and
// body.speed, the body module does the rest.
#include "game.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace paper_crowd {
namespace {
constexpr f32 pi = 3.14159265f;

constexpr std::array<rgba, 4> pet_colors{{
    rgb(222, 214, 200), rgb(60, 56, 58), rgb(170, 118, 70), rgb(214, 184, 140),
}};

struct weight {
  activity act;
  f32 w;
};
constexpr std::array<weight, 14> weights{{
    {act_idle, 9.0f},      {act_stroll, 30.0f},   {act_run, 7.0f},  {act_wave, 5.0f},
    {act_jump, 5.0f},      {act_jacks, 3.0f},     {act_dance, 6.0f}, {act_cartwheel, 4.0f},
    {act_handstand, 2.0f}, {act_lie, 3.0f},       {act_sit, 5.0f},  {act_follow, 9.0f},
    {act_chase, 4.0f},     {act_ring, 8.0f},
}};

vec2 keep_on_paper(vec2 p) {
  // The head stands ~20 px above the feet, so the top edge needs more room.
  return clamp(p, {margin, margin + 18.0f}, {world_w - margin, world_h - margin});
}

// A person busy with something they would drop to run from a chaser.
bool easy_to_bother(activity a) {
  return a == act_idle || a == act_stroll || a == act_wave || a == act_sit || a == act_dance;
}

entt::entity nearest_other(njin_ctx &ctx, entt::entity self, vec2 pos, f32 radius,
                           bool (*accept)(activity)) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  entt::entity best = entt::null;
  f32 best_score = radius * radius;
  for (auto [e, p, tr] : reg.view<const person, const transform>().each()) {
    if (e == self || p.ring >= 0 || !accept(p.act))
      continue;
    // A little noise so the same neighbour is not always the one picked.
    const f32 score = length_sq(tr.pos - pos) * r.range(0.6f, 1.4f);
    if (score < best_score) {
      best_score = score;
      best = e;
    }
  }
  return best;
}

vec2 stroll_target(rng &r, vec2 from) {
  return keep_on_paper(from + r.direction() * r.range(50.0f, 260.0f));
}

void begin(person &p, activity act, f32 timer) {
  p.act = act;
  p.timer = timer;
  p.clock = 0.0f;
}

bool join_or_start_ring(njin_ctx &ctx, entt::entity e, person &p, vec2 pos) {
  rng &r = random(ctx);
  // Join the nearest ring with a free hand first.
  i32 best = -1;
  f32 best_d = 260.0f * 260.0f;
  for (usize i = 0; i < g.rings.size(); ++i) {
    const ring &rg = g.rings[i];
    if (!rg.alive || static_cast<i32>(rg.members.size()) >= rg.capacity)
      continue;
    const f32 d = length_sq(rg.center - pos);
    if (d < best_d) {
      best_d = d;
      best = static_cast<i32>(i);
    }
  }
  if (best < 0) {
    i32 live = 0;
    for (const ring &rg : g.rings)
      live += rg.alive ? 1 : 0;
    if (live >= 6)
      return false;
    for (usize i = 0; i < g.rings.size() && best < 0; ++i)
      if (!g.rings[i].alive)
        best = static_cast<i32>(i);
    if (best < 0) {
      g.rings.emplace_back();
      best = static_cast<i32>(g.rings.size()) - 1;
    }
    ring &rg = g.rings[static_cast<usize>(best)];
    rg = ring{};
    rg.alive = true;
    rg.center = clamp(pos, {margin + 30.0f, margin + 50.0f},
                      {world_w - margin - 30.0f, world_h - margin - 20.0f});
    rg.capacity = r.range(4, 8);
    rg.turn = r.range(22.0f, 40.0f) * (r.chance(0.5f) ? 1.0f : -1.0f);
    rg.angle = r.range(0.0f, 360.0f);
    rg.timer = 9.0f; // time to gather at least three before giving up
  }
  g.rings[static_cast<usize>(best)].members.push_back(e);
  p.ring = best;
  begin(p, act_ring, 999.0f); // the ring ends it, not the timer
  return true;
}

void pick_next(njin_ctx &ctx, entt::entity e, person &p, vec2 pos) {
  rng &r = random(ctx);
  // drive_rings drops anyone whose ring index no longer points at it.
  p.ring = -1;
  p.other = entt::null;

  f32 total = 0.0f;
  for (const weight &w : weights)
    total += w.w;
  f32 roll = r.range(0.0f, total);
  activity act = act_stroll;
  for (const weight &w : weights) {
    if (roll < w.w) {
      act = w.act;
      break;
    }
    roll -= w.w;
  }

  switch (act) {
  case act_idle:
    begin(p, act, r.range(0.8f, 3.0f));
    p.face = r.chance(0.5f) ? 1.0f : -1.0f;
    return;
  case act_stroll:
    begin(p, act, r.range(5.0f, 11.0f));
    p.target = stroll_target(r, pos);
    return;
  case act_run:
    begin(p, act, r.range(2.0f, 4.0f));
    p.target = keep_on_paper(pos + r.direction() * r.range(160.0f, 380.0f));
    return;
  case act_wave:
    begin(p, act, r.range(1.5f, 3.2f));
    return;
  case act_jump:
    begin(p, act, r.range(1.4f, 3.0f));
    return;
  case act_jacks:
    begin(p, act, r.range(2.0f, 3.6f));
    return;
  case act_dance:
    begin(p, act, r.range(3.0f, 6.5f));
    p.spin = r.chance(0.5f) ? 1.0f : -1.0f;
    return;
  case act_cartwheel: {
    // One cartwheel takes 0.8 s; do one to three, all the way round.
    begin(p, act, 0.8f * static_cast<f32>(r.range(1, 3)));
    const f32 room_right = world_w - margin - pos.x;
    const f32 room_left = pos.x - margin;
    p.face = room_right > room_left ? 1.0f : -1.0f;
    p.spin = p.face;
    return;
  }
  case act_handstand:
    begin(p, act, r.range(1.5f, 3.0f));
    return;
  case act_lie:
    begin(p, act, r.range(4.0f, 10.0f));
    p.face = r.chance(0.5f) ? 1.0f : -1.0f;
    return;
  case act_sit:
    begin(p, act, r.range(4.0f, 9.0f));
    p.face = r.chance(0.5f) ? 1.0f : -1.0f;
    return;
  case act_follow: {
    const entt::entity f = nearest_other(ctx, e, pos, 220.0f, [](activity a) {
      return a == act_stroll || a == act_idle || a == act_run;
    });
    if (f == entt::null)
      break;
    p.other = f;
    begin(p, act, r.range(5.0f, 11.0f));
    return;
  }
  case act_chase: {
    const entt::entity victim = nearest_other(ctx, e, pos, 200.0f, easy_to_bother);
    if (victim == entt::null)
      break;
    p.other = victim;
    begin(p, act, r.range(3.0f, 6.0f));
    person &v = world(ctx).get<person>(victim);
    v.other = e;
    begin(v, act_flee, r.range(2.5f, 4.5f));
    return;
  }
  case act_ring:
    if (join_or_start_ring(ctx, e, p, pos))
      return;
    break;
  default:
    break;
  }
  // The choice needed someone who was not there: just go for a walk.
  begin(p, act_stroll, r.range(5.0f, 11.0f));
  p.target = stroll_target(r, pos);
}

constexpr f32 push_radius = 12.0f; // personal space a walker keeps

// Everyone's position bucketed into push_radius cells (a counting sort, so
// rebuilding it every frame is two passes over the crowd and no allocation
// once the vectors have grown).
struct space_grid {
  static constexpr i32 cols = static_cast<i32>(world_w / push_radius) + 1;
  static constexpr i32 rows = static_cast<i32>(world_h / push_radius) + 1;
  std::vector<u32> start; // cols * rows + 1 offsets into `points`
  std::vector<vec2> points;
  std::vector<u32> cell_of;

  static i32 cell(vec2 p) {
    const i32 x = std::clamp(static_cast<i32>(p.x / push_radius), 0, cols - 1);
    const i32 y = std::clamp(static_cast<i32>(p.y / push_radius), 0, rows - 1);
    return y * cols + x;
  }

  void build(const entt::registry &reg) {
    start.assign(static_cast<usize>(cols * rows + 1), 0);
    cell_of.clear();
    for (auto [e, p, tr] : reg.view<const person, const transform>().each()) {
      const u32 c = static_cast<u32>(cell(tr.pos));
      cell_of.push_back(c);
      ++start[c + 1];
    }
    for (usize i = 1; i < start.size(); ++i)
      start[i] += start[i - 1];
    points.resize(cell_of.size());
    std::vector<u32> fill(start.begin(), start.end() - 1);
    usize i = 0;
    for (auto [e, p, tr] : reg.view<const person, const transform>().each())
      points[fill[cell_of[i++]]++] = tr.pos;
  }

  template <class Fn> void each_near(vec2 p, Fn &&fn) const {
    const i32 cx = std::clamp(static_cast<i32>(p.x / push_radius), 0, cols - 1);
    const i32 cy = std::clamp(static_cast<i32>(p.y / push_radius), 0, rows - 1);
    for (i32 y = std::max(cy - 1, 0); y <= std::min(cy + 1, rows - 1); ++y)
      for (i32 x = std::max(cx - 1, 0); x <= std::min(cx + 1, cols - 1); ++x) {
        const usize c = static_cast<usize>(y * cols + x);
        for (u32 k = start[c]; k < start[c + 1]; ++k)
          fn(points[k]);
      }
  }
};

// Steering toward a point: full speed far away, easing off near it so the
// walker settles instead of orbiting.
f32 arrive(vec2 from, vec2 to, f32 speed, vec2 &dir) {
  const vec2 d = to - from;
  const f32 len = length(d);
  if (len < 1.5f) {
    dir = {};
    return 0.0f;
  }
  dir = d * (1.0f / len);
  return std::min(speed, len * 5.0f);
}

vec2 away_from_edges(vec2 pos) {
  vec2 push{};
  const f32 reach = 70.0f;
  if (pos.x < margin + reach)
    push.x += 1.0f - (pos.x - margin) / reach;
  if (pos.x > world_w - margin - reach)
    push.x -= 1.0f - (world_w - margin - pos.x) / reach;
  if (pos.y < margin + 18.0f + reach)
    push.y += 1.0f - (pos.y - margin - 18.0f) / reach;
  if (pos.y > world_h - margin - reach)
    push.y -= 1.0f - (world_h - margin - pos.y) / reach;
  return push;
}
} // namespace

entt::entity spawn_person(njin_ctx &ctx, vec2 pos) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  pos = keep_on_paper(pos);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  // topdown_body moves through collision_move, which needs a collider, but
  // collision_move tests every other collider in the world: with a crowd of
  // hundreds that is O(n^2) per fixed step and the game drops to 2 fps. People
  // collide with nothing, so the collider stays disabled, which makes
  // collision_move a plain move; drive_people keeps them on the paper.
  reg.emplace<collider>(e, collider{.shape = collider_circle, .radius = 2.5f, .enabled = false});
  reg.emplace<topdown_body>(e, topdown_body{.speed = 0.0f, .accel = 420.0f, .decel = 700.0f});

  person p;
  p.cloth = static_cast<u8>(r.range(0, static_cast<i32>(cloth_colors.size()) - 1));
  p.size = r.chance(0.2f) ? r.range(0.72f, 0.82f) : r.range(0.92f, 1.1f);
  p.face = r.chance(0.5f) ? 1.0f : -1.0f;
  p.walk = r.range(0.0f, 10.0f);
  // A fresh person looks around a moment before deciding anything, and not in
  // step with everyone spawned the same frame.
  begin(p, act_idle, r.range(0.2f, 2.0f));
  reg.emplace<person>(e, p);
  return e;
}

entt::entity spawn_pet(njin_ctx &ctx, vec2 pos, entt::entity owner) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = keep_on_paper(pos)});
  // Disabled for the same reason as a person's: see spawn_person.
  reg.emplace<collider>(e, collider{.shape = collider_circle, .radius = 2.0f, .enabled = false});
  reg.emplace<topdown_body>(e, topdown_body{.speed = 0.0f, .accel = 600.0f, .decel = 900.0f});
  pet a;
  a.owner = owner;
  a.cat = r.chance(0.3f);
  a.coat = pet_colors[static_cast<usize>(r.range(0, static_cast<i32>(pet_colors.size()) - 1))];
  a.offset = r.direction() * r.range(10.0f, 22.0f);
  a.timer = r.range(1.0f, 3.0f);
  reg.emplace<pet>(e, a);
  return e;
}

void spawn_crowd(njin_ctx &ctx) {
  rng &r = random(ctx);
  // The painting is not spread evenly: people bunch into little knots with
  // open paper between them. Half spawn around a few knots, half anywhere.
  std::array<vec2, 9> knots{};
  for (vec2 &k : knots)
    k = r.point_in({{margin + 60.0f, margin + 80.0f},
                    {world_w - 2.0f * margin - 120.0f, world_h - 2.0f * margin - 140.0f}});
  std::vector<entt::entity> people;
  for (i32 i = 0; i < start_people; ++i) {
    vec2 pos;
    if (i % 2 == 0) {
      const vec2 k = knots[static_cast<usize>(r.range(0, static_cast<i32>(knots.size()) - 1))];
      pos = k + r.direction() * r.range(0.0f, 60.0f);
    } else {
      pos = r.point_in({{margin, margin + 18.0f}, {world_w - 2.0f * margin, world_h - 2.0f * margin - 18.0f}});
    }
    people.push_back(spawn_person(ctx, pos));
  }
  for (i32 i = 0; i < start_pets; ++i) {
    const entt::entity owner = people[static_cast<usize>(r.range(0, static_cast<i32>(people.size()) - 1))];
    spawn_pet(ctx, world(ctx).get<transform>(owner).pos + r.direction() * 14.0f, owner);
  }
}

void clear_crowd(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  std::vector<entt::entity> doomed;
  for (const entt::entity e : reg.view<person>())
    doomed.push_back(e);
  for (const entt::entity e : reg.view<pet>())
    doomed.push_back(e);
  reg.destroy(doomed.begin(), doomed.end());
  g.rings.clear();
}

void call_people(njin_ctx &ctx, vec2 pos, f32 radius) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  for (auto [e, p, tr] : reg.view<person, const transform>().each()) {
    // Hands in a ring stay joined; everyone else nearby comes over.
    if (p.ring >= 0 || distance(tr.pos, pos) > radius)
      continue;
    p.other = entt::null;
    begin(p, act_come, 12.0f);
    p.target = keep_on_paper(pos + r.direction() * r.range(8.0f, 34.0f));
  }
}

void cheer_all(njin_ctx &ctx) {
  rng &r = random(ctx);
  for (auto [e, p] : world(ctx).view<person>().each()) {
    if (p.ring >= 0 || p.act == act_cartwheel)
      continue;
    p.other = entt::null;
    // A short random delay, so the crowd goes up as a ripple, not in unison.
    begin(p, act_jump, r.range(1.4f, 2.4f));
    p.clock = -r.range(0.0f, 0.35f);
  }
}

void drive_rings(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const f32 dt = delta(ctx);
  for (usize i = 0; i < g.rings.size(); ++i) {
    ring &rg = g.rings[i];
    if (!rg.alive)
      continue;
    const i32 index = static_cast<i32>(i);
    // Drop anyone who left (called away, reset) or no longer exists.
    std::erase_if(rg.members, [&](entt::entity e) {
      return !reg.valid(e) || reg.get<person>(e).ring != index;
    });

    const usize n = rg.members.size();
    const f32 radius = std::max(12.0f, static_cast<f32>(n) * 17.0f / (2.0f * pi));
    i32 in_place = 0;
    for (usize k = 0; k < n; ++k) {
      const f32 a = rg.angle + 360.0f * static_cast<f32>(k) / static_cast<f32>(std::max<usize>(n, 1));
      const vec2 around = from_angle(a);
      // Seen from above at a slant, a ring on the ground reads as an ellipse.
      const vec2 slot = rg.center + vec2{around.x * radius, around.y * radius * 0.62f};
      reg.get<person>(rg.members[k]).target = slot;
      if (distance(reg.get<transform>(rg.members[k]).pos, slot) < 5.0f)
        ++in_place;
    }

    if (!rg.started && n >= 3 && in_place >= static_cast<i32>(n)) {
      rg.started = true;
      rg.timer = r.range(10.0f, 20.0f);
    }
    if (rg.started)
      rg.angle += rg.turn * dt;
    rg.timer -= dt;

    if (rg.timer <= 0.0f || (rg.started && n < 2)) {
      for (const entt::entity e : rg.members) {
        person &p = reg.get<person>(e);
        p.ring = -1;
        // Let go one by one, and more often than not with a hop.
        begin(p, r.chance(0.45f) ? act_jump : act_idle, r.range(0.3f, 1.4f));
      }
      rg.members.clear();
      rg.alive = false;
    }
  }
}

void drive_people(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const f32 dt = delta(ctx);

  // Positions of everyone, once, for the walkers' personal-space push, sorted
  // into a grid of push-radius cells so a walker only looks at the 3x3 cells
  // around it: linear in the crowd, not quadratic.
  static space_grid grid;
  grid.build(reg);

  for (auto [e, p, body, tr] : reg.view<person, topdown_body, transform>().each()) {
    tr.pos = clamp_to_paper(tr.pos);
    p.timer -= dt;
    p.clock += dt;
    const vec2 pos = tr.pos;
    vec2 dir{};
    f32 speed = 0.0f;

    // An activity that needs someone else ends when that someone is gone.
    if ((p.act == act_follow || p.act == act_chase || p.act == act_flee) && !reg.valid(p.other))
      p.timer = 0.0f;

    switch (p.act) {
    case act_stroll:
      speed = arrive(pos, p.target, 34.0f * p.size, dir);
      if (speed == 0.0f)
        p.timer = std::min(p.timer, 0.0f);
      break;
    case act_come:
      speed = arrive(pos, p.target, 52.0f, dir);
      if (speed == 0.0f) {
        begin(p, act_wave, r.range(2.0f, 3.5f));
        p.face = g.call_pos.x > pos.x ? 1.0f : -1.0f;
      }
      break;
    case act_run:
      speed = arrive(pos, p.target, 96.0f, dir);
      if (speed == 0.0f)
        p.timer = 0.0f;
      break;
    case act_cartwheel:
      dir = {p.face, 0.0f};
      speed = 58.0f;
      break;
    case act_follow: {
      const transform &ft = reg.get<transform>(p.other);
      const topdown_body &fb = reg.get<topdown_body>(p.other);
      // Walk beside the friend, on whichever side we came from.
      const vec2 side = vec2{p.face < 0.0f ? 9.0f : -9.0f, 1.5f};
      speed = arrive(pos, ft.pos + side, std::max(40.0f, length(fb.velocity) * 1.25f), dir);
      break;
    }
    case act_chase: {
      const vec2 them = reg.get<transform>(p.other).pos;
      speed = arrive(pos, them, 104.0f, dir);
      if (p.clock > 0.8f && distance(pos, them) < 9.0f) {
        // Tag! Half the time the roles swap, else both hop and it is over.
        person &v = reg.get<person>(p.other);
        const entt::entity victim = p.other;
        if (r.chance(0.5f)) {
          begin(v, act_chase, r.range(3.0f, 5.0f));
          v.other = e;
          begin(p, act_flee, r.range(2.5f, 4.0f));
          p.other = victim;
        } else {
          begin(v, act_jump, r.range(1.0f, 1.8f));
          v.other = entt::null;
          begin(p, act_jump, r.range(1.0f, 1.8f));
          p.other = entt::null;
        }
        speed = 0.0f;
      }
      break;
    }
    case act_flee: {
      const vec2 them = reg.get<transform>(p.other).pos;
      const vec2 away = pos - them;
      dir = normalize(normalize(away) + away_from_edges(pos) * 1.6f);
      speed = 90.0f;
      break;
    }
    case act_ring:
      speed = arrive(pos, p.target, 50.0f, dir);
      break;
    default:
      break;
    }

    if (speed > 0.0f) {
      // Walkers step around whoever is in the way; the push fades with
      // distance and never beats the wish to go somewhere.
      vec2 push{};
      grid.each_near(pos, [&](vec2 s) {
        const vec2 d = pos - s;
        const f32 dd = length_sq(d);
        if (dd > 0.01f && dd < push_radius * push_radius)
          push = push + d * ((push_radius - std::sqrt(dd)) / (push_radius * std::sqrt(dd)));
      });
      if (p.act != act_ring && p.act != act_chase)
        dir = normalize(dir + push * 0.9f);
      if (std::abs(dir.x) > 0.2f && p.act != act_cartwheel)
        p.face = dir.x > 0.0f ? 1.0f : -1.0f;
    }
    if (p.act == act_cartwheel && (pos.x < margin + 4.0f || pos.x > world_w - margin - 4.0f))
      p.timer = 0.0f;

    body.speed = speed;
    body.input.move = speed > 0.0f ? dir : vec2{};
    p.walk += length(body.velocity) * dt;

    if (p.timer <= 0.0f && p.act != act_ring)
      pick_next(ctx, e, p, pos);
  }
}

void drive_pets(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const f32 dt = delta(ctx);
  for (auto [e, a, body, tr] : reg.view<pet, topdown_body, transform>().each()) {
    tr.pos = clamp_to_paper(tr.pos);
    if (!reg.valid(a.owner)) {
      const entt::entity someone = nearest_other(ctx, entt::null, tr.pos, 1e5f, [](activity) { return true; });
      a.owner = someone;
      if (someone == entt::null) {
        body.input.move = {};
        continue;
      }
    }
    const transform &ot = reg.get<transform>(a.owner);
    const topdown_body &ob = reg.get<topdown_body>(a.owner);
    a.timer -= dt;
    if (a.timer <= 0.0f) {
      // Every few seconds: a new spot around the owner, and maybe a sniff.
      a.offset = r.direction() * r.range(10.0f, ob.moving ? 18.0f : 34.0f);
      a.sniffing = !ob.moving && r.chance(0.5f);
      a.timer = r.range(1.2f, 3.5f);
    }
    vec2 dir{};
    const f32 want = std::max(46.0f, length(ob.velocity) * 1.3f);
    f32 speed = arrive(tr.pos, keep_on_paper(ot.pos + a.offset), want, dir);
    if (a.sniffing && distance(tr.pos, ot.pos) < 40.0f)
      speed = std::min(speed, 14.0f);
    body.speed = speed;
    body.input.move = speed > 0.0f ? dir : vec2{};
    a.walk += length(body.velocity) * dt;
  }
}
} // namespace paper_crowd
