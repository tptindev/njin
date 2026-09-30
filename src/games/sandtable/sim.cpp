#include "sim.h"
#include "audio.h"
#include "levels.h"
#include "view.h"
#include "weather.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable {

game_state state;

namespace {

rng sim_rng{98765};

// Rebuilt every battle step: one index per side for target searches, one over
// everybody for the push that keeps figures from overlapping.
spatial_index side_index[2];
spatial_index all_index;
std::vector<spatial_item> side_items[2];
std::vector<u32> side_ids[2]; // index item -> soldier
std::vector<spatial_item> all_items;
std::vector<u32> all_ids;
std::vector<vec2> push;
std::vector<spatial_hit> hits;

constexpr f32 melee_aggro = 150.0f;
constexpr f32 charge_run = 90.0f; // cavalry needs this much run-up to charge
constexpr f32 min_flag_gap = 48.0f; // world units between two troops' flags
constexpr f32 step_max = 1.0f / 30.0f;
constexpr rect table_bounds{{0.0f, 0.0f}, {world_width, world_height}};

i32 side_idx(side s) { return static_cast<i32>(s); }
i32 ai(arm a) { return static_cast<i32>(a); }

f32 range_of(const soldier &s) { return spec(s.type).range; }

f32 speed_of(const soldier &s) {
  return spec(s.type).speed * terrain_speed(terrain_at(s.pos), sails(s.type));
}

// Moves `pos` by `step` without leaving the ground it can be on (the water,
// for a boat), sliding along the edge when the step runs into it at an angle.
vec2 move_on_ground(vec2 pos, vec2 step, bool boat = false) {
  if (!walkable(pos, boat))
    return pos + step; // already stuck outside it (pushed in a crowd): let it get back
  if (walkable(pos + step, boat))
    return pos + step;
  if (walkable(pos + vec2{step.x, 0.0f}, boat))
    return pos + vec2{step.x, 0.0f};
  if (walkable(pos + vec2{0.0f, step.y}, boat))
    return pos + vec2{0.0f, step.y};
  return pos;
}

// Paths of their own that soldiers may still look for this step: the rest
// head for their block's anchor and look again later.
i32 own_paths_left = 0;

// Where to head for `goal`: straight there if nothing is in the way; else the
// group's anchor, a point of the group's path, or a path of its own. Looked
// at every half second or so, not every step: a battle has thousands of men.
vec2 route_to(soldier &s, const group &g, vec2 goal, f32 dt) {
  s.route -= dt;
  if (s.use_via && distance(s.pos, s.via) < 6.0f)
    s.route = 0.0f; // reached the way point: look again
  if (s.route > 0.0f)
    return s.use_via ? s.via : goal;
  s.route = sim_rng.range(0.4f, 0.7f);
  const nav_grid &nav = terrain_nav(sails(s.type));
  s.use_via = !nav_line_clear(nav, s.pos, goal);
  if (!s.use_via)
    return goal;
  if (distance(s.pos, g.anchor) > 8.0f && nav_line_clear(nav, s.pos, g.anchor)) {
    s.via = g.anchor;
    return s.via;
  }
  const i32 last = static_cast<i32>(g.path.path.size()) - 1;
  for (i32 k = std::min(last, g.path.next + 3); k >= g.path.next; --k) {
    const vec2 p = g.path.path[static_cast<usize>(k)];
    if (nav_line_clear(nav, s.pos, p)) {
      s.via = p;
      return s.via;
    }
  }
  s.via = g.anchor;
  if (own_paths_left > 0) {
    --own_paths_left;
    std::vector<vec2> own;
    nav_path_opts opts{};
    opts.max_nodes = 400;
    nav_find_path(nav, s.pos, goal, own, opts);
    if (!own.empty())
      s.via = own.front();
  }
  return s.via;
}

vec2 slot_world(const group &g, vec2 slot) {
  const vec2 right{-g.dir.y, g.dir.x};
  return g.anchor + right * slot.x - g.dir * slot.y;
}

void kill(soldier &s) {
  s.alive = false;
  s.hp = 0.0f;
  state.corpses.push_back({s.pos, s.facing, s.radius * 0.85f, s.type, s.owner});
  const rgba c = s.owner == side::player ? col_player : col_enemy;
  for (i32 i = 0; i < 2; ++i)
    add_particle(fx_kind::spark, s.pos, from_angle(sim_rng.range(0.0f, 360.0f)) * sim_rng.range(20.0f, 50.0f), 2.0f,
                 0.3f, c);
  fx_dust(s.pos, 1, 3.0f);
}

void hurt(soldier &target, f32 damage) {
  if (!target.alive)
    return;
  target.hp -= damage;
  target.flash = 0.12f;
  if (target.hp <= 0.0f)
    kill(target);
}

// Damage of one blow or one arrow of `from` against `to`.
f32 blow(const soldier &from, const soldier &to) {
  return spec(from.type).damage * from.weight * counter[ai(from.type)][ai(to.type)];
}

void fire(context &ctx, const soldier &s, const soldier &target, i32 target_id) {
  projectile p{};
  p.from = s.pos;
  p.owner = s.owner;
  p.source = s.type;
  const f32 d = distance(s.pos, target.pos);
  if (s.type == arm::artillery) {
    // Shells land near where the target stands, not on it.
    const f32 spread = d * 0.07f;
    p.to = target.pos + vec2{sim_rng.range(-spread, spread), sim_rng.range(-spread, spread)};
    p.duration = std::max(0.5f, d / 320.0f);
    p.arc = d * 0.22f;
    p.splash = spec(s.type).splash * (1.0f + 0.15f * std::log(std::max(1.0f, s.weight)));
    p.damage = spec(s.type).damage * s.weight;
    fx_muzzle(s.pos + normalize(target.pos - s.pos) * 6.0f, normalize(target.pos - s.pos));
    audio_play_gated(ctx, sfx_type::cannon, 0.7f, 0.32f);
  } else {
    p.to = target.pos + vec2{sim_rng.range(-5.0f, 5.0f), sim_rng.range(-5.0f, 5.0f)};
    p.duration = std::max(0.15f, d / 480.0f);
    p.arc = d * 0.12f;
    p.target = target_id;
    p.damage = blow(s, target);
    audio_play_gated(ctx, sfx_type::arrow, 0.65f, 0.24f);
  }
  state.projectiles.push_back(p);
}

// An elephant's blow lands on everyone around its target too, and throws them
// back.
void trample(context &ctx, const soldier &s, const soldier &target) {
  const i32 foe = side_idx(other(s.owner));
  const f32 radius = spec(arm::elephant).splash * (1.0f + 0.15f * std::log(std::max(1.0f, s.weight)));
  const u32 n = spatial_nearest(side_index[foe], {.at = target.pos, .radius = radius, .touching = true}, hits, 24);
  for (u32 k = 0; k < n; ++k) {
    soldier &o = state.soldiers[side_ids[foe][hits[k].item]];
    if (&o == &target || !o.alive)
      continue;
    const vec2 away = o.pos - s.pos;
    if (length_sq(away) > 0.01f)
      o.pos = move_on_ground(o.pos, normalize(away) * 5.0f, sails(o.type));
    hurt(o, blow(s, o) * 0.5f);
  }
  fx_dust(target.pos, 2, radius * 0.5f);
  audio_play_gated(ctx, sfx_type::trumpet, 0.25f, 2.0f);
}

void strike(context &ctx, soldier &s, soldier &target) {
  f32 dmg = blow(s, target);
  if (s.type == arm::archer)
    dmg *= 0.4f; // an archer caught in melee
  if (s.type == arm::cavalry && s.run >= charge_run && target.type != arm::spear) {
    dmg *= 3.0f;
    target.pos = move_on_ground(target.pos, normalize(target.pos - s.pos) * 10.0f, sails(target.type));
    add_particle(fx_kind::ring, target.pos, {}, 14.0f, 0.25f, col_gold_light);
    fx_dust(target.pos, 3, 6.0f);
    audio_play_gated(ctx, sfx_type::charge, 0.4f, 0.5f);
  }
  s.run = 0.0f;
  if (s.type == arm::elephant)
    trample(ctx, s, target);
  hurt(target, dmg);
  if (sim_rng.chance(0.3f))
    add_particle(fx_kind::spark, target.pos, from_angle(sim_rng.range(0.0f, 360.0f)) * 50.0f, 1.5f, 0.15f,
                 rgb(255, 240, 200));
  if (s.type != arm::elephant)
    audio_play_gated(ctx, s.type == arm::spear ? sfx_type::spear : sfx_type::slash, 0.48f, 0.18f);
}

void build_indices() {
  for (i32 k = 0; k < 2; ++k) {
    side_items[k].clear();
    side_ids[k].clear();
  }
  all_items.clear();
  all_ids.clear();
  for (u32 i = 0; i < state.soldiers.size(); ++i) {
    const soldier &s = state.soldiers[i];
    if (!s.alive)
      continue;
    const spatial_item it{.pos = s.pos, .radius = s.radius};
    side_items[side_idx(s.owner)].push_back(it);
    side_ids[side_idx(s.owner)].push_back(i);
    all_items.push_back(it);
    all_ids.push_back(i);
  }
  for (i32 k = 0; k < 2; ++k)
    spatial_build(side_index[k], {.kind = spatial_grid, .bounds = table_bounds, .cell_size = 40.0f}, side_items[k]);
}

// The nearest living enemy of `s` within `radius`, or -1.
i32 find_enemy(const soldier &s, f32 radius) {
  const i32 foe = side_idx(other(s.owner));
  spatial_hit hit{};
  if (spatial_nearest(side_index[foe], {.at = s.pos, .radius = radius, .touching = true}, &hit, 1) == 0)
    return -1;
  return static_cast<i32>(side_ids[foe][hit.item]);
}

// Turns `dir` toward `to` a little; never through zero when they are opposite.
vec2 turn_to(vec2 dir, vec2 to, f32 rate) {
  const vec2 d = lerp(dir, to, clamp(rate, 0.0f, 1.0f));
  return length_sq(d) > 0.0001f ? normalize(d) : vec2{-dir.y, dir.x};
}

// How far round its anchor a garrison fights: whoever comes this close to
// the block is attacked, nobody further out is chased.
f32 guard_radius(const group &g) { return melee_aggro + g.span; }

// A garrison marches to its post round what cannot be crossed, facing the
// way it goes, then turns to face the way it was told.
void hold_post(group &g, f32 dt) {
  const arm_spec &sp = spec(g.type);
  const bool boat = sails(g.type);
  const vec2 to_post = g.post - g.anchor;
  const f32 dist = length(to_post);
  if (dist < 2.0f) {
    g.dir = turn_to(g.dir, g.face, dt * 1.5f);
    return;
  }
  vec2 steer = to_post / dist;
  if (dist > tile_world) {
    g.repath -= dt;
    if (g.repath <= 0.0f || g.path.done()) {
      g.repath = sim_rng.range(1.5f, 2.5f);
      std::vector<vec2> way;
      nav_find_path(terrain_nav(boat), g.anchor, g.post, way);
      g.path.set(std::move(way));
      g.path.reach = tile_world * 0.5f;
    }
    if (!g.path.done())
      steer = nav_steer(g.path, g.anchor);
  }
  if (length_sq(steer) > 0.0001f)
    g.dir = turn_to(g.dir, normalize(steer), dt * 1.5f);
  // Wait for stragglers, and for men caught in a fight on the way.
  const bool strung_out = distance(g.anchor, g.centroid) > 60.0f + 4.0f * std::sqrt(static_cast<f32>(g.figures));
  if (!strung_out) {
    const f32 step = std::min(dist, sp.speed * 0.85f * terrain_speed(terrain_at(g.anchor), boat) * dt);
    g.anchor = move_on_ground(g.anchor, steer * step, boat);
  }
}

void update_groups(f32 dt) {
  for (group &g : state.groups) {
    g.alive = 0;
    g.centroid = {};
  }
  for (const soldier &s : state.soldiers) {
    if (!s.alive)
      continue;
    group &g = state.groups[static_cast<usize>(s.group)];
    g.alive++;
    g.centroid += s.pos;
  }
  for (group &g : state.groups) {
    if (g.alive > 0)
      g.centroid = g.centroid / static_cast<f32>(g.alive);
  }

  for (group &g : state.groups) {
    if (g.alive == 0)
      continue;
    if (g.garrison) {
      hold_post(g, dt);
      continue;
    }
    // March on the nearest enemy block.
    const group *foe = nullptr;
    f32 best = 1e12f;
    // Troops on foot do not march on boats they cannot reach, unless boats
    // are all the enemy has left.
    bool foe_on_land = false;
    for (const group &o : state.groups)
      foe_on_land = foe_on_land || (o.owner != g.owner && o.alive > 0 && !sails(o.type));
    for (const group &o : state.groups) {
      if (o.owner == g.owner || o.alive == 0)
        continue;
      if (!sails(g.type) && sails(o.type) && foe_on_land)
        continue;
      const f32 d = length_sq(g.anchor - o.centroid);
      if (d < best) {
        best = d;
        foe = &o;
      }
    }
    if (!foe)
      continue;
    const vec2 to_foe = foe->centroid - g.anchor;
    const f32 dist = length(to_foe);
    // The block faces the enemy...
    if (dist > 1.0f)
      g.dir = normalize(lerp(g.dir, to_foe / dist, clamp(dt * 1.5f, 0.0f, 1.0f)));
    // ...and marches along a way round what cannot be crossed.
    g.repath -= dt;
    if (g.repath <= 0.0f || g.path.done()) {
      g.repath = sim_rng.range(0.8f, 1.2f);
      std::vector<vec2> way;
      nav_find_path(terrain_nav(sails(g.type)), g.anchor, foe->centroid, way);
      g.path.set(std::move(way));
      g.path.reach = tile_world * 0.5f;
    }
    const vec2 steer = nav_steer(g.path, g.anchor);

    const arm_spec &sp = spec(g.type);
    const f32 hold = sp.range > 0.0f ? sp.range * 0.85f : 20.0f;
    // Wait for stragglers so the block keeps its shape.
    const bool strung_out = distance(g.anchor, g.centroid) > 60.0f + 4.0f * std::sqrt(static_cast<f32>(g.figures));
    if (dist > hold && !strung_out)
      g.anchor = move_on_ground(
          g.anchor, steer * sp.speed * 0.85f * terrain_speed(terrain_at(g.anchor), sails(g.type)) * dt,
          sails(g.type));
  }
}

void update_soldiers(context &ctx, f32 dt) {
  own_paths_left = 16;
  for (u32 i = 0; i < state.soldiers.size(); ++i) {
    soldier &s = state.soldiers[i];
    if (!s.alive)
      continue;
    const arm_spec &sp = spec(s.type);
    s.cooldown = std::max(0.0f, s.cooldown - dt);
    s.flash = std::max(0.0f, s.flash - dt);
    s.think -= dt;

    const bool ranged = sp.range > 0.0f;
    const f32 reach_range = ranged ? range_of(s) : 0.0f;
    if (s.target >= 0 && !state.soldiers[static_cast<usize>(s.target)].alive)
      s.target = -1;
    const group &g = state.groups[static_cast<usize>(s.group)];
    if (s.think <= 0.0f) {
      s.think = sim_rng.range(0.2f, 0.35f);
      s.target = find_enemy(s, ranged ? reach_range + 30.0f : melee_aggro);
      // A garrison's men go for no one outside its guard; archers and guns
      // shoot whatever is in range from where they stand.
      if (s.target >= 0 && g.garrison && !ranged &&
          distance(state.soldiers[static_cast<usize>(s.target)].pos, g.anchor) > guard_radius(g))
        s.target = -1;
    }
    vec2 goal = slot_world(g, s.slot);
    bool chase = false;
    s.fighting = false;
    if (s.target >= 0) {
      soldier &t = state.soldiers[static_cast<usize>(s.target)];
      const f32 d = distance(s.pos, t.pos);
      const f32 touch = s.radius + t.radius + sp.reach;
      if (ranged && d <= reach_range && d >= sp.min_range) {
        goal = s.pos;
        s.fighting = true;
        if (s.cooldown <= 0.0f) {
          s.cooldown = sp.interval * sim_rng.range(0.9f, 1.15f);
          fire(ctx, s, t, s.target);
        }
      } else if (d <= touch && s.type != arm::artillery) {
        goal = s.pos;
        s.fighting = true;
        if (s.cooldown <= 0.0f) {
          s.cooldown = sp.interval * sim_rng.range(0.9f, 1.15f);
          strike(ctx, s, t);
        }
      } else if (g.garrison ? !ranged && distance(t.pos, g.anchor) <= guard_radius(g)
                            : !ranged || d > reach_range) {
        goal = t.pos;
        chase = true;
      }
      // Artillery with the enemy too close holds still and hopes.
      if (s.fighting && d > 0.01f)
        s.facing = (t.pos - s.pos) / d;
    }

    s.anim += dt;
    if (distance(goal, s.pos) > 2.0f)
      goal = route_to(s, g, goal, dt);
    const vec2 to_goal = goal - s.pos;
    const f32 gd = length(to_goal);
    s.moving = gd > 2.0f;
    if (gd > 2.0f) {
      const f32 step = std::min(gd, speed_of(s) * dt);
      const vec2 dir = to_goal / gd;
      s.pos = move_on_ground(s.pos, dir * step, sails(s.type));
      s.facing = dir;
      s.run = (chase && s.type == arm::cavalry) ? s.run + step : 0.0f;
      const f32 dust_rate = s.type == arm::cavalry ? 2.5f : 0.25f;
      if (sim_rng.chance(dust_rate * dt))
        fx_dust(s.pos + vec2{0.0f, s.radius}, 1, 2.0f);
    } else if (!s.fighting) {
      s.run = 0.0f;
    }
  }
}

void separate() {
  for (u32 k = 0; k < all_ids.size(); ++k)
    all_items[k].pos = state.soldiers[all_ids[k]].pos;
  spatial_build(all_index, {.kind = spatial_grid, .bounds = table_bounds}, all_items);
  spatial_separate(all_index, push, 6);
  for (u32 k = 0; k < all_ids.size(); ++k) {
    soldier &s = state.soldiers[all_ids[k]];
    // The crowd does not shove anyone off a cliff or into the river.
    s.pos = move_on_ground(s.pos, push[k], sails(s.type));
    s.pos.x = clamp(s.pos.x, table_margin + s.radius, world_width - table_margin - s.radius);
    s.pos.y = clamp(s.pos.y, table_margin + s.radius, world_height - table_margin - s.radius);
  }
}

void land_shell(const projectile &p) {
  const i32 foe = side_idx(other(p.owner));
  const u32 n = spatial_nearest(side_index[foe], {.at = p.to, .radius = p.splash, .touching = true}, hits, 64);
  for (u32 k = 0; k < n; ++k) {
    soldier &t = state.soldiers[side_ids[foe][hits[k].item]];
    const f32 falloff = 1.0f - 0.5f * clamp(std::sqrt(hits[k].distance_sq) / p.splash, 0.0f, 1.0f);
    hurt(t, p.damage * counter[ai(arm::artillery)][ai(t.type)] * falloff);
    // The blast throws them back, but not off a cliff or into the river.
    const vec2 away = t.pos - p.to;
    if (length_sq(away) > 0.01f)
      t.pos = move_on_ground(t.pos, normalize(away) * 10.0f * falloff, sails(t.type));
  }
}

void update_projectiles(context &ctx, f32 dt) {
  for (projectile &p : state.projectiles) {
    p.t += dt / p.duration;
    if (p.t < 1.0f)
      continue;
    if (p.source == arm::artillery) {
      land_shell(p);
      fx_explosion(ctx, p.to, p.splash);
      audio_play_gated(ctx, sfx_type::explosion, 0.65f, 0.32f);
    } else if (p.target >= 0) {
      soldier &t = state.soldiers[static_cast<usize>(p.target)];
      if (t.alive && distance(t.pos, p.to) <= t.radius + 12.0f)
        hurt(t, terrain_covers(terrain_at(t.pos)) ? p.damage * 0.5f : p.damage);
      else if (sim_rng.chance(0.3f))
        fx_dust(p.to, 1, 1.0f); // a miss kicks up sand
    }
  }
  std::erase_if(state.projectiles, [](const projectile &p) { return p.t >= 1.0f; });
}

void count_men() {
  state.men_now[0] = state.men_now[1] = 0.0f;
  for (const soldier &s : state.soldiers) {
    if (s.alive)
      state.men_now[side_idx(s.owner)] += s.weight * (s.hp / s.max_hp);
  }
}

void end_battle(context &ctx, bool won) {
  state.screen = phase::result;
  state.won = won;
  audio_play(ctx, won ? sfx_type::victory : sfx_type::defeat, 1.0f);
  time_set_scale(ctx, 1.0f);
}

void battle_step(context &ctx, f32 dt) {
  state.battle_time += dt;
  state.hour = std::fmod(state.hour + dt / seconds_per_hour, 24.0f);
  build_indices();
  update_groups(dt);
  update_soldiers(ctx, dt);
  separate();
  update_projectiles(ctx, dt);
  count_men();

  i32 alive[2]{};
  for (const soldier &s : state.soldiers)
    alive[side_idx(s.owner)] += s.alive ? 1 : 0;
  if (alive[1] == 0)
    end_battle(ctx, true);
  else if (alive[0] == 0)
    end_battle(ctx, false);
  else if (state.battle_time >= battle_time_limit)
    end_battle(ctx, state.men_now[0] / state.men_start[0] > state.men_now[1] / state.men_start[1]);
}

void spawn_troop(const troop &c) {
  const i32 gi = static_cast<i32>(state.groups.size());
  group g{};
  g.type = c.type;
  g.tier = c.tier;
  g.owner = c.owner;
  // The player's troops start at home and march to their flags; the enemy
  // stands at its flags. Boats are launched at the nearest water.
  const vec2 flag = sails(c.type) ? nearest_water(c.pos) : c.pos;
  const vec2 start = c.owner == side::player ? troop_home(c) : flag;
  g.anchor = start;
  g.centroid = start;
  g.dir = c.owner == side::player ? vec2{0.0f, -1.0f} : vec2{0.0f, 1.0f};
  // The player's blocks hold their flags.
  if (c.owner == side::player) {
    g.garrison = true;
    g.post = flag;
    g.face = c.face;
  }
  const std::vector<vec2> slots = formation_slots(c.type, c.tier);
  for (const vec2 &slot : slots)
    g.span = std::max(g.span, length(slot));
  g.figures = static_cast<i32>(slots.size());
  g.alive = g.figures;
  state.groups.push_back(g);

  const arm_spec &sp = spec(c.type);
  const f32 weight = static_cast<f32>(tiers[c.tier].men) / static_cast<f32>(slots.size());
  for (const vec2 &slot : slots) {
    soldier s{};
    s.slot = slot;
    // Out of a huddle at home: every figure marches to its place.
    s.pos = slot_world(g, slot * 0.2f) + vec2{sim_rng.range(-2.0f, 2.0f), sim_rng.range(-2.0f, 2.0f)};
    if (!walkable(s.pos, sails(c.type)))
      s.pos = start;
    s.facing = g.dir;
    s.weight = weight;
    s.max_hp = s.hp = sp.hp * weight;
    s.radius = figure_radius(c.type, weight);
    s.cooldown = sim_rng.range(0.0f, sp.interval);
    s.think = sim_rng.range(0.0f, 0.3f);
    s.anim = sim_rng.range(0.0f, 1.0f);
    s.group = gi;
    s.type = c.type;
    s.owner = c.owner;
    state.soldiers.push_back(s);
  }
}


void clear_battle() {
  state.soldiers.clear();
  state.groups.clear();
  state.projectiles.clear();
  state.corpses.clear();
  state.scorches.clear();
  state.shockwaves.clear();
  state.particles.clear();
  state.popups.clear();
  state.battle_time = 0.0f;
}

} // namespace

// --- FX ---

void add_popup(vec2 pos, rgba col, const char *text, f32 time) {
  state.popups.push_back({pos, time, time, col, text});
}

void add_particle(fx_kind kind, vec2 pos, vec2 vel, f32 size, f32 life, rgba col, f32 delay) {
  // Enough for several big blasts at once; past it, new puffs are skipped.
  if (state.particles.size() > 4000)
    return;
  state.particles.push_back({pos, vel, life, life, size, col, kind, delay});
}

void fx_explosion(context &ctx, vec2 pos, f32 radius) {
  // The shockwave, seen only in what it bends, and the dust it lifts off the
  // ground as it passes.
  fx_shockwave(pos, radius * 3.0f, 4.0f, 0.5f);
  for (i32 i = 0; i < 12; ++i) {
    const vec2 dir = from_angle(static_cast<f32>(i) * 30.0f + sim_rng.range(-10.0f, 10.0f));
    add_particle(fx_kind::dust, pos + dir * radius * 0.6f, dir * radius * sim_rng.range(3.5f, 5.0f),
                 sim_rng.range(4.0f, 7.0f), sim_rng.range(0.4f, 0.6f), rgb(214, 190, 142));
  }
  add_particle(fx_kind::fire, pos, {}, radius * 0.7f, 0.18f); // the flash
  const i32 flames = 6 + static_cast<i32>(radius / 5.0f);
  for (i32 i = 0; i < flames; ++i) {
    const vec2 dir = from_angle(sim_rng.range(0.0f, 360.0f));
    add_particle(fx_kind::fire, pos + dir * sim_rng.range(0.0f, radius * 0.4f),
                 dir * sim_rng.range(20.0f, 70.0f), sim_rng.range(4.0f, 8.0f), sim_rng.range(0.3f, 0.6f));
  }
  for (i32 i = 0; i < 6; ++i) {
    const vec2 dir = from_angle(sim_rng.range(0.0f, 360.0f));
    add_particle(fx_kind::debris, pos, dir * sim_rng.range(80.0f, 160.0f), 2.0f, sim_rng.range(0.3f, 0.5f),
                 rgb(70, 56, 42));
  }
  // Smoke rises from the fire once it dies down.
  for (i32 i = 0; i < 5; ++i) {
    const vec2 at = pos + from_angle(sim_rng.range(0.0f, 360.0f)) * sim_rng.range(0.0f, radius * 0.5f);
    add_particle(fx_kind::smoke, at, {sim_rng.range(-6.0f, 6.0f), sim_rng.range(-22.0f, -12.0f)},
                 sim_rng.range(7.0f, 11.0f), sim_rng.range(1.0f, 1.6f), rgb(128, 120, 112), sim_rng.range(0.15f, 0.4f));
  }
  if (state.scorches.size() < 300)
    state.scorches.push_back({pos, radius * 0.55f});
  // A light jolt, and not one per shell: a whole battery firing would add up
  // to the shake's ceiling.
  static f32 last_shake = -1.0f;
  if (elapsed(ctx) - last_shake > 0.3f) {
    last_shake = elapsed(ctx);
    camera_shake(ctx, 0.04f);
  }
}

void fx_muzzle(vec2 pos, vec2 dir) {
  add_particle(fx_kind::fire, pos, dir * 30.0f, 5.0f, 0.12f);
  // The blast of the charge, and the ground dust blown round the gun.
  fx_shockwave(pos, 40.0f, 2.0f, 0.3f);
  for (i32 i = 0; i < 6; ++i) {
    const vec2 out = from_angle(static_cast<f32>(i) * 60.0f + sim_rng.range(-15.0f, 15.0f));
    add_particle(fx_kind::dust, pos + out * 4.0f, out * sim_rng.range(40.0f, 70.0f), 3.0f, 0.35f,
                 rgb(214, 190, 142));
  }
  for (i32 i = 0; i < 3; ++i)
    add_particle(fx_kind::smoke, pos + dir * sim_rng.range(0.0f, 6.0f),
                 dir * sim_rng.range(10.0f, 25.0f) + vec2{0.0f, -8.0f}, sim_rng.range(5.0f, 8.0f),
                 sim_rng.range(0.8f, 1.3f), rgb(200, 196, 188), 0.05f);
}

void fx_shockwave(vec2 pos, f32 radius, f32 strength, f32 duration) {
  // The shader bends the picture for 16 at most: the oldest give way.
  if (state.shockwaves.size() >= 16)
    state.shockwaves.erase(state.shockwaves.begin());
  state.shockwaves.push_back({pos, radius, strength, 0.0f, duration});
}

void fx_dust(vec2 pos, i32 puffs, f32 spread) {
  for (i32 i = 0; i < puffs; ++i)
    add_particle(fx_kind::dust, pos + vec2{sim_rng.range(-spread, spread), sim_rng.range(-spread, spread)},
                 {sim_rng.range(-8.0f, 8.0f), sim_rng.range(-8.0f, 2.0f)}, sim_rng.range(3.0f, 5.0f),
                 sim_rng.range(0.4f, 0.8f), rgb(214, 190, 142));
}

// --- Formation ---

f32 figure_radius(arm a, f32 weight) {
  return spec(a).body * (1.0f + 0.3f * std::log(std::max(1.0f, weight)) / std::log(3.0f));
}

std::vector<vec2> formation_slots(arm a, i32 tier) {
  const i32 n = figure_count(a, tier);
  const f32 weight = static_cast<f32>(tiers[tier].men) / static_cast<f32>(n);
  const f32 gap = figure_radius(a, weight) * (a == arm::artillery ? 4.0f : a == arm::elephant ? 3.0f : 2.5f);
  // Twice as wide as deep, like a line of battle.
  const i32 cols = std::max(1, static_cast<i32>(std::ceil(std::sqrt(static_cast<f32>(n) * 2.0f))));
  const i32 rows = (n + cols - 1) / cols;
  std::vector<vec2> out;
  out.reserve(static_cast<usize>(n));
  for (i32 i = 0; i < n; ++i) {
    const i32 row = i / cols;
    const i32 in_row = row == rows - 1 ? n - row * cols : cols;
    const i32 col = i % cols;
    out.push_back({(static_cast<f32>(col) - static_cast<f32>(in_row - 1) * 0.5f) * gap,
                   (static_cast<f32>(row) - static_cast<f32>(rows - 1) * 0.5f) * gap});
  }
  return out;
}

// --- Deployment ---

void load_level(context &ctx) {
  state.screen = phase::deploy;
  state.won = false;
  state.board.clear();
  state.saved_board.clear();
  state.menu = {};
  state.cmd = command::none;
  state.selected = -1;
  clear_battle();
  state.hour = current_level().hour;
  build_terrain();
  time_set_scale(ctx, 1.0f);
  time_set_paused(ctx, false);
  view_reset();
  // The briefing, where the eye already is when the table opens.
  char brief[256];
  std::snprintf(brief, sizeof(brief), "%s: %s", current_level().name, current_level().brief);
  ui_toast_clear(ctx);
  ui_toast(ctx, brief, {.seconds = 6.0f});
}

i32 troop_count(side owner) {
  if (owner == side::enemy)
    return static_cast<i32>(current_level().enemy.size());
  return static_cast<i32>(state.board.size());
}

const char *troop_error(arm a, vec2 pos, i32 ignore) {
  if (pos.x < table_margin || pos.y < table_margin || pos.x > world_width - table_margin ||
      pos.y > world_height - table_margin)
    return "Ngoài sa bàn";
  if (!walkable(pos))
    return "Không đặt quân lên đồi, núi hay sông";
  if (sails(a)) {
    f32 water = 0.0f;
    nearest_water(pos, &water);
    if (water > 6.0f * 32.0f)
      return "Thuyền phải đặt gần sông suối";
  }
  for (i32 i = 0; i < static_cast<i32>(state.board.size()); ++i) {
    const troop &c = state.board[static_cast<usize>(i)];
    // One flag to a spot: the formations sort themselves out in the battle.
    if (i != ignore && distance(c.pos, pos) < min_flag_gap)
      return "Quá sát một đơn vị khác";
  }
  return nullptr;
}

bool add_troop(context &ctx, arm a, i32 tier, vec2 pos) {
  if (troop_error(a, pos) != nullptr)
    return false;
  state.board.push_back({a, tier, side::player, pos});
  audio_play(ctx, sfx_type::chip, 1.0f);
  return true;
}

bool move_troop(context &ctx, i32 index, vec2 pos) {
  if (index < 0 || index >= static_cast<i32>(state.board.size()))
    return false;
  troop &c = state.board[static_cast<usize>(index)];
  if (troop_error(c.type, pos, index) != nullptr)
    return false;
  c.pos = pos;
  audio_play(ctx, sfx_type::chip, 1.0f);
  return true;
}

bool set_troop_tier(context &ctx, i32 index, i32 tier) {
  if (index < 0 || index >= static_cast<i32>(state.board.size()) || tier < 0 || tier >= tier_count)
    return false;
  state.board[static_cast<usize>(index)].tier = tier;
  state.new_tier = tier;
  audio_play(ctx, sfx_type::chip, 1.0f);
  return true;
}

void remove_troop(context &ctx, i32 index) {
  if (index < 0 || index >= static_cast<i32>(state.board.size()))
    return;
  state.board.erase(state.board.begin() + index);
  if (state.selected == index) {
    state.selected = -1;
    state.cmd = command::none;
  } else if (state.selected > index) {
    --state.selected;
  }
  audio_play(ctx, sfx_type::chip, 0.7f);
}

vec2 troop_home(const troop &t) {
  const rect z = player_zone;
  vec2 start = t.pos;
  if (!point_in_rect(t.pos, z))
    start = {clamp(t.pos.x, z.pos.x + tile_world, z.pos.x + z.size.x - tile_world), z.pos.y + z.size.y * 0.5f};
  if (sails(t.type))
    return nearest_water(start);
  // The nearest open ground in rings round it, if it is not open itself.
  for (i32 ring = 0; ring <= 12; ++ring) {
    const i32 steps = ring == 0 ? 1 : 16;
    for (i32 k = 0; k < steps; ++k) {
      const vec2 p = start + from_angle(360.0f * static_cast<f32>(k) / static_cast<f32>(steps)) *
                                 (tile_world * static_cast<f32>(ring));
      if (walkable(p))
        return p;
    }
  }
  return start;
}

void clear_board(context &ctx) {
  state.board.clear();
  state.menu = {};
  state.cmd = command::none;
  state.selected = -1;
  audio_play(ctx, sfx_type::chip, 0.8f);
}

// --- Battle ---

bool start_battle(context &ctx) {
  if (state.board.empty())
    return false;
  state.menu = {};
  state.cmd = command::none;
  state.saved_board = state.board;

  clear_battle();
  for (const troop &c : current_level().enemy)
    spawn_troop(c);
  for (const troop &c : state.board)
    spawn_troop(c);
  count_men();
  state.men_start[0] = std::max(1.0f, state.men_now[0]);
  state.men_start[1] = std::max(1.0f, state.men_now[1]);
  state.screen = phase::battle;
  set_speed(ctx, state.speed_index);
  audio_play(ctx, sfx_type::horn, 1.0f);
  audio_play(ctx, sfx_type::drum, 0.9f);
  add_popup({world_width * 0.5f, world_height * 0.5f}, col_gold_light, "XUẤT QUÂN!", 1.6f);
  return true;
}

void redeploy(context &ctx) {
  const std::vector<troop> board = state.saved_board;
  load_level(ctx);
  state.board = board;
  state.saved_board = board;
}

void set_speed(context &ctx, i32 index) {
  state.speed_index = clamp(index, 0, 2);
  time_set_scale(ctx, state.screen == phase::battle ? speed_steps[state.speed_index] : 1.0f);
}

void sim_init(context &ctx) {
  audio_init(ctx);
  load_level(ctx);
}

void sim_update(context &ctx) {
  const f32 dt = delta(ctx);
  if (dt <= 0.0f)
    return;

  if (state.screen == phase::battle) {
    // Fast-forward in small steps so 4x does not tunnel through battle lines.
    f32 left = dt;
    while (left > 0.0f && state.screen == phase::battle) {
      const f32 step = std::min(left, step_max);
      battle_step(ctx, step);
      left -= step;
    }
  }

  for (popup_text &p : state.popups) {
    p.timer -= dt;
    p.pos.y -= 18.0f * dt;
  }
  std::erase_if(state.popups, [](const popup_text &p) { return p.timer <= 0.0f; });
  for (fx_particle &p : state.particles) {
    if (p.delay > 0.0f) {
      p.delay -= dt;
      continue;
    }
    p.life -= dt;
    p.pos += p.vel * dt;
    switch (p.kind) {
    case fx_kind::fire:
      p.vel = p.vel * (1.0f - clamp(dt * 4.0f, 0.0f, 1.0f)) + vec2{0.0f, -30.0f * dt};
      break;
    case fx_kind::smoke:
      p.vel.x *= 1.0f - clamp(dt * 0.5f, 0.0f, 1.0f);
      p.size += 6.0f * dt;
      break;
    case fx_kind::dust:
      p.vel = p.vel * (1.0f - clamp(dt * 2.0f, 0.0f, 1.0f));
      p.size += 3.0f * dt;
      break;
    default:
      p.vel = p.vel * (1.0f - clamp(dt * 3.0f, 0.0f, 1.0f));
      break;
    }
  }
  std::erase_if(state.particles, [](const fx_particle &p) { return p.life <= 0.0f; });
  for (shockwave &w : state.shockwaves)
    w.time += dt;
  std::erase_if(state.shockwaves, [](const shockwave &w) { return w.time >= w.duration; });
}

} // namespace sandtable
