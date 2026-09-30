#include "sim.h"
#include "audio.h"
#include "levels.h"
#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable {

game_state state;

namespace {

rng sim_rng{98765};

// Rebuilt every fight step: one index per side for target searches, one over
// everybody for the push that keeps men from standing in each other.
spatial_index side_index[2];
spatial_index all_index;
std::vector<spatial_item> side_items[2];
std::vector<u32> side_ids[2]; // index item -> soldier
std::vector<spatial_item> all_items;
std::vector<u32> all_ids;
std::vector<vec2> push;

constexpr f32 melee_aggro = 150.0f;   // a man goes for anyone of the other gang this close
constexpr f32 hunt_range = 420.0f;    // an enemy group goes for the player's men this close
constexpr f32 min_flag_gap = 48.0f;   // world units between two troops' flags
constexpr f32 claim_time = 12.0f;     // seconds for one man to claim a free turf
constexpr f32 step_max = 1.0f / 30.0f;
constexpr rect table_bounds{{0.0f, 0.0f}, {world_width, world_height}};

i32 side_idx(side s) { return static_cast<i32>(s); }

f32 speed_of(const soldier &s) { return fighter.speed * terrain_speed(terrain_at(s.pos)); }

// Moves `pos` by `step` without leaving the ground it can be on, sliding
// along the edge when the step runs into it at an angle.
vec2 move_on_ground(vec2 pos, vec2 step) {
  if (!walkable(pos))
    return pos + step; // already stuck outside it (pushed in a crowd): let it get back
  if (walkable(pos + step))
    return pos + step;
  if (walkable(pos + vec2{step.x, 0.0f}))
    return pos + vec2{step.x, 0.0f};
  if (walkable(pos + vec2{0.0f, step.y}))
    return pos + vec2{0.0f, step.y};
  return pos;
}

// Paths of their own that men may still look for this step: the rest head
// for their group's anchor and look again later.
i32 own_paths_left = 0;

// Where to head for `goal`: straight there if nothing is in the way; else the
// group's anchor, a point of the group's path, or a path of its own. Looked
// at every half second or so, not every step.
vec2 route_to(soldier &s, const group &g, vec2 goal, f32 dt) {
  s.route -= dt;
  if (s.use_via && distance(s.pos, s.via) < 6.0f)
    s.route = 0.0f; // reached the way point: look again
  if (s.route > 0.0f)
    return s.use_via ? s.via : goal;
  s.route = sim_rng.range(0.4f, 0.7f);
  const nav_grid &nav = terrain_nav();
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
  state.corpses.push_back({s.pos, s.facing, s.radius * 0.85f, s.owner});
  fx_dust(s.pos, 2, 3.0f);
}

void hurt(soldier &target, f32 damage) {
  if (!target.alive)
    return;
  target.hp -= damage;
  target.flash = 0.12f;
  target.hurt = flinch_time;
  if (target.hp <= 0.0f)
    kill(target);
}

// A punch (left or right) or a kick: it lands, knocks the other man back a
// step, and throws a few sparks of the hitter's colour. A kick lands harder
// and may put him on the ground for a while.
void strike(context &ctx, soldier &s, soldier &target) {
  s.act_kind = sim_rng.chance(0.3f) ? 2 : (sim_rng.chance(0.5f) ? 0 : 1);
  s.act = blow_time;
  const bool kick = s.act_kind == 2;
  hurt(target, fighter.damage * (kick ? 1.3f : 1.0f) * sim_rng.range(0.8f, 1.25f));
  if (kick && target.alive && target.down <= 0.0f && target.rise <= 0.0f && sim_rng.chance(0.35f)) {
    target.down = down_time;
    fx_dust(target.pos, 3, 4.0f);
  }
  const vec2 away = target.pos - s.pos;
  if (length_sq(away) > 0.01f)
    target.pos = move_on_ground(target.pos, normalize(away) * 3.0f);
  const rgba c = s.owner == side::player ? col_player_light : col_enemy_light;
  for (i32 i = 0; i < 2; ++i)
    add_particle(fx_kind::spark, target.pos, from_angle(sim_rng.range(0.0f, 360.0f)) * 40.0f, 1.5f, 0.15f, c);
  audio_play_gated(ctx, sfx_type::slash, 0.5f, 0.12f);
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

// The nearest living man of the other gang within `radius` of `s`, or -1.
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
// the group is attacked, nobody further out is chased.
f32 guard_radius(const group &g) { return melee_aggro + g.span; }

// Walks a group's anchor toward `goal` round what cannot be crossed, facing
// the way it goes, and stops `stop` short of it. True once there.
bool walk_to(group &g, vec2 goal, f32 stop, f32 dt) {
  const vec2 to_goal = goal - g.anchor;
  const f32 dist = length(to_goal);
  if (dist <= stop)
    return true;
  vec2 steer = to_goal / dist;
  if (dist > tile_world) {
    g.repath -= dt;
    if (g.repath <= 0.0f || g.path.done() || distance(goal, g.goal) > tile_world) {
      g.repath = sim_rng.range(1.0f, 1.8f);
      g.goal = goal;
      std::vector<vec2> way;
      nav_find_path(terrain_nav(), g.anchor, goal, way);
      g.path.set(std::move(way));
      g.path.reach = tile_world * 0.5f;
    }
    if (!g.path.done())
      steer = nav_steer(g.path, g.anchor);
  }
  if (length_sq(steer) > 0.0001f)
    g.dir = turn_to(g.dir, normalize(steer), dt * 1.5f);
  // Wait for stragglers, and for men caught in a fight on the way.
  const bool strung_out = distance(g.anchor, g.centroid) > 50.0f + 6.0f * std::sqrt(static_cast<f32>(g.figures));
  if (!strung_out) {
    const f32 step = std::min(dist - stop, fighter.speed * 0.85f * terrain_speed(terrain_at(g.anchor)) * dt);
    g.anchor = move_on_ground(g.anchor, steer * step);
  }
  return false;
}

// Where an enemy group goes: for the player's men if any are near, else to
// the nearest turf the enemy does not fully hold (to take it, or to save one
// of its own being taken), else for the player's men wherever they are.
vec2 enemy_goal(const group &g, f32 *stop) {
  const group *prey = nullptr;
  f32 best = 1e12f;
  for (const group &o : state.groups) {
    if (o.owner == g.owner || o.alive == 0)
      continue;
    const f32 d = distance(g.anchor, o.centroid);
    if (d < best) {
      best = d;
      prey = &o;
    }
  }
  *stop = 20.0f;
  if (prey != nullptr && best < hunt_range)
    return prey->centroid;
  const turf *aim = nullptr;
  f32 near = 1e12f;
  for (const turf &t : state.turfs) {
    if (t.claim <= -1.0f && t.men[side_idx(side::player)] == 0)
      continue; // its own, and safe
    const f32 d = distance(g.anchor, t.pos);
    if (d < near) {
      near = d;
      aim = &t;
    }
  }
  if (aim != nullptr) {
    *stop = aim->radius * 0.3f;
    return aim->pos;
  }
  return prey != nullptr ? prey->centroid : g.anchor;
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
      // The player's men walk to their flag and hold it, facing the way they
      // were told.
      if (walk_to(g, g.post, 2.0f, dt))
        g.dir = turn_to(g.dir, g.face, dt * 1.5f);
      continue;
    }
    f32 stop = 20.0f;
    const vec2 goal = enemy_goal(g, &stop);
    if (walk_to(g, goal, stop, dt) && distance(goal, g.anchor) > 1.0f)
      g.dir = turn_to(g.dir, normalize(goal - g.anchor), dt * 1.5f);
  }
}

void update_soldiers(context &ctx, f32 dt) {
  own_paths_left = 16;
  for (u32 i = 0; i < state.soldiers.size(); ++i) {
    soldier &s = state.soldiers[i];
    if (!s.alive)
      continue;
    s.cooldown = std::max(0.0f, s.cooldown - dt);
    s.flash = std::max(0.0f, s.flash - dt);
    s.act = std::max(0.0f, s.act - dt);
    s.hurt = std::max(0.0f, s.hurt - dt);
    s.think -= dt;
    s.anim += dt;
    s.pace = 0.0f;
    // On the ground, then getting up: no blows, no steps.
    if (s.down > 0.0f) {
      s.down -= dt;
      if (s.down <= 0.0f)
        s.rise = rise_time;
      s.fighting = s.moving = false;
      continue;
    }
    if (s.rise > 0.0f) {
      s.rise = std::max(0.0f, s.rise - dt);
      s.fighting = s.moving = false;
      continue;
    }

    if (s.target >= 0 && !state.soldiers[static_cast<usize>(s.target)].alive)
      s.target = -1;
    const group &g = state.groups[static_cast<usize>(s.group)];
    if (s.think <= 0.0f) {
      s.think = sim_rng.range(0.2f, 0.35f);
      s.target = find_enemy(s, melee_aggro);
      // A garrison's men go for no one outside its guard.
      if (s.target >= 0 && g.garrison &&
          distance(state.soldiers[static_cast<usize>(s.target)].pos, g.anchor) > guard_radius(g))
        s.target = -1;
    }
    vec2 goal = slot_world(g, s.slot);
    bool chase = false;
    s.fighting = false;
    if (s.target >= 0) {
      soldier &t = state.soldiers[static_cast<usize>(s.target)];
      const f32 d = distance(s.pos, t.pos);
      const f32 touch = s.radius + t.radius + fighter.reach;
      if (d <= touch) {
        goal = s.pos;
        s.fighting = true;
        if (s.cooldown <= 0.0f) {
          s.cooldown = fighter.interval * sim_rng.range(0.85f, 1.2f);
          strike(ctx, s, t);
        }
      } else if (!g.garrison || distance(t.pos, g.anchor) <= guard_radius(g)) {
        goal = t.pos;
        chase = true;
      }
      if (s.fighting && d > 0.01f)
        s.facing = (t.pos - s.pos) / d;
    }

    if (distance(goal, s.pos) > 2.0f)
      goal = route_to(s, g, goal, dt);
    const vec2 to_goal = goal - s.pos;
    const f32 gd = length(to_goal);
    s.moving = gd > 2.0f;
    if (gd > 2.0f) {
      // Run at a man, or to catch up when far behind; else walk.
      const f32 speed = speed_of(s) * (chase || gd > 60.0f ? run_factor : 1.0f);
      const f32 step = std::min(gd, speed * dt);
      const vec2 dir = to_goal / gd;
      s.pos = move_on_ground(s.pos, dir * step);
      s.facing = dir;
      s.stride += step;
      s.pace = step / dt;
      if (sim_rng.chance(0.25f * dt))
        fx_dust(s.pos + vec2{0.0f, s.radius}, 1, 2.0f);
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
    // The crowd does not shove anyone up a cliff or into the river.
    s.pos = move_on_ground(s.pos, push[k]);
    s.pos.x = clamp(s.pos.x, table_margin + s.radius, world_width - table_margin - s.radius);
    s.pos.y = clamp(s.pos.y, table_margin + s.radius, world_height - table_margin - s.radius);
  }
}

// Men on each turf, and each turf's claim moving toward the only gang on it:
// the more men, the faster. It turns at either end, and falls to nobody on
// the way across.
void update_turfs(context &ctx, f32 dt) {
  for (turf &t : state.turfs)
    t.men[0] = t.men[1] = 0;
  for (const soldier &s : state.soldiers) {
    if (!s.alive)
      continue;
    const i32 k = turf_at(s.pos);
    if (k >= 0)
      state.turfs[static_cast<usize>(k)].men[side_idx(s.owner)]++;
  }
  for (turf &t : state.turfs) {
    const i32 mine = t.men[0], theirs = t.men[1];
    if ((mine > 0) == (theirs > 0))
      continue; // empty, or fought over
    const i32 n = std::max(mine, theirs);
    const f32 rate = (0.7f + 0.03f * static_cast<f32>(std::min(n, 10))) / claim_time;
    t.claim = clamp(t.claim + (mine > 0 ? rate : -rate) * dt, -1.0f, 1.0f);
    const i32 was = t.held_by;
    if (t.claim >= 1.0f)
      t.held_by = side_idx(side::player);
    else if (t.claim <= -1.0f)
      t.held_by = side_idx(side::enemy);
    else if ((t.held_by == side_idx(side::player) && t.claim < 0.0f) ||
             (t.held_by == side_idx(side::enemy) && t.claim > 0.0f))
      t.held_by = nobody;
    if (t.held_by != was && t.held_by != nobody) {
      const bool ours = t.held_by == side_idx(side::player);
      char line[96];
      std::snprintf(line, sizeof(line), "%s %s", ours ? "CHIẾM" : "MẤT", t.name);
      add_popup(t.pos, ours ? col_gold_light : col_bad, line, 2.0f);
      audio_play(ctx, ours ? sfx_type::horn : sfx_type::drum, 0.8f);
    }
  }
}

void count_men() {
  state.men_now[0] = state.men_now[1] = 0.0f;
  for (const soldier &s : state.soldiers) {
    if (s.alive)
      state.men_now[side_idx(s.owner)] += s.hp / s.max_hp;
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
  update_turfs(ctx, dt);
  count_men();

  i32 alive[2]{};
  for (const soldier &s : state.soldiers)
    alive[side_idx(s.owner)] += s.alive ? 1 : 0;
  if (alive[1] == 0) {
    end_battle(ctx, true);
  } else if (alive[0] == 0) {
    end_battle(ctx, false);
  } else if (state.battle_time >= battle_time_limit) {
    // Time: more turfs wins; as many, more men left.
    i32 ours = 0, theirs = 0, free = 0;
    turf_counts(ours, theirs, free);
    end_battle(ctx, ours != theirs ? ours > theirs
                                   : state.men_now[0] / state.men_start[0] > state.men_now[1] / state.men_start[1]);
  }
}

void spawn_troop(const troop &c) {
  const i32 gi = static_cast<i32>(state.groups.size());
  group g{};
  g.tier = c.tier;
  g.owner = c.owner;
  // The player's men start at home and walk to their flags; the enemy's
  // stand at theirs.
  const vec2 start = c.owner == side::player ? troop_home(c) : c.pos;
  g.anchor = start;
  g.centroid = start;
  g.goal = start;
  g.dir = c.owner == side::player ? vec2{0.0f, -1.0f} : vec2{0.0f, 1.0f};
  if (c.owner == side::player) {
    g.garrison = true;
    g.post = c.pos;
    g.face = c.face;
  }
  const std::vector<vec2> slots = formation_slots(c.tier);
  for (const vec2 &slot : slots)
    g.span = std::max(g.span, length(slot));
  g.figures = static_cast<i32>(slots.size());
  g.alive = g.figures;
  state.groups.push_back(g);

  for (const vec2 &slot : slots) {
    soldier s{};
    s.slot = slot;
    // Out of a huddle: every man walks to his place.
    s.pos = slot_world(g, slot * 0.3f) + vec2{sim_rng.range(-2.0f, 2.0f), sim_rng.range(-2.0f, 2.0f)};
    if (!walkable(s.pos))
      s.pos = start;
    s.facing = g.dir;
    s.max_hp = s.hp = fighter.hp;
    s.radius = fighter.body;
    s.cooldown = sim_rng.range(0.0f, fighter.interval);
    s.think = sim_rng.range(0.0f, 0.3f);
    s.anim = sim_rng.range(0.0f, 1.0f);
    s.group = gi;
    s.owner = c.owner;
    state.soldiers.push_back(s);
  }
}

// The level's turfs, each moved onto open ground if its middle is not.
void reset_turfs() {
  state.turfs.clear();
  for (const turf_def &d : current_level().turfs) {
    turf t{};
    t.name = d.name;
    t.pos = d.pos;
    t.radius = d.radius;
    for (i32 ring = 0; ring <= 8 && !walkable(t.pos); ++ring)
      for (i32 k = 0; k < 16; ++k) {
        const vec2 p = d.pos + from_angle(22.5f * static_cast<f32>(k)) * (tile_world * static_cast<f32>(ring));
        if (walkable(p)) {
          t.pos = p;
          break;
        }
      }
    t.held_by = d.held_by;
    t.claim = d.held_by == side_idx(side::player) ? 1.0f : d.held_by == side_idx(side::enemy) ? -1.0f : 0.0f;
    state.turfs.push_back(t);
  }
}

void clear_battle() {
  state.soldiers.clear();
  state.groups.clear();
  state.corpses.clear();
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
  if (state.particles.size() > 2000)
    return;
  state.particles.push_back({pos, vel, life, life, size, col, kind, delay});
}

void fx_dust(vec2 pos, i32 puffs, f32 spread) {
  for (i32 i = 0; i < puffs; ++i)
    add_particle(fx_kind::dust, pos + vec2{sim_rng.range(-spread, spread), sim_rng.range(-spread, spread)},
                 {sim_rng.range(-8.0f, 8.0f), sim_rng.range(-8.0f, 2.0f)}, sim_rng.range(3.0f, 5.0f),
                 sim_rng.range(0.4f, 0.8f), rgb(214, 190, 142));
}

// --- Formation ---

// A gang stands in a loose crowd, not in ranks: a sunflower spiral round the
// middle, every man about the same room from his neighbours.
std::vector<vec2> formation_slots(i32 tier) {
  const i32 n = tiers[static_cast<usize>(tier)].men;
  const f32 gap = fighter.body * 3.2f;
  std::vector<vec2> out;
  out.reserve(static_cast<usize>(n));
  for (i32 k = 0; k < n; ++k) {
    const f32 r = gap * 0.55f * std::sqrt(static_cast<f32>(k) + 0.5f);
    out.push_back(from_angle(137.508f * static_cast<f32>(k)) * r);
  }
  return out;
}

// --- Turfs ---

i32 turf_at(vec2 pos) {
  for (usize i = 0; i < state.turfs.size(); ++i)
    if (distance(pos, state.turfs[i].pos) <= state.turfs[i].radius)
      return static_cast<i32>(i);
  return -1;
}

void turf_counts(i32 &player, i32 &enemy, i32 &free) {
  player = enemy = free = 0;
  for (const turf &t : state.turfs) {
    if (t.held_by == side_idx(side::player))
      ++player;
    else if (t.held_by == side_idx(side::enemy))
      ++enemy;
    else
      ++free;
  }
}

// --- Setting up ---

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
  reset_turfs();
  time_set_scale(ctx, 1.0f);
  time_set_paused(ctx, false);
  view_reset();
  // The men stationed at the home turf.
  const turf_def &home = current_level().turfs[static_cast<usize>(current_level().home_turf)];
  state.board.push_back({2, side::player, home.pos});
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

const char *troop_error(vec2 pos, i32 ignore) {
  if (pos.x < table_margin || pos.y < table_margin || pos.x > world_width - table_margin ||
      pos.y > world_height - table_margin)
    return "Ngoài sa bàn";
  if (!walkable(pos))
    return "Không cắm cờ trên đồi, núi hay sông";
  for (i32 i = 0; i < static_cast<i32>(state.board.size()); ++i) {
    const troop &c = state.board[static_cast<usize>(i)];
    // One flag to a spot: the crowds sort themselves out in the fight.
    if (i != ignore && distance(c.pos, pos) < min_flag_gap)
      return "Quá sát một nhóm khác";
  }
  return nullptr;
}

bool add_troop(context &ctx, i32 tier, vec2 pos) {
  if (troop_error(pos) != nullptr)
    return false;
  state.board.push_back({tier, side::player, pos});
  audio_play(ctx, sfx_type::chip, 1.0f);
  return true;
}

bool move_troop(context &ctx, i32 index, vec2 pos) {
  if (index < 0 || index >= static_cast<i32>(state.board.size()))
    return false;
  troop &c = state.board[static_cast<usize>(index)];
  if (troop_error(pos, index) != nullptr)
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
  // The home turf, on its side toward the flag, so groups set out spread
  // round it rather than all from one spot.
  const turf_def &h = current_level().turfs[static_cast<usize>(current_level().home_turf)];
  vec2 start = h.pos;
  const f32 d = distance(t.pos, h.pos);
  if (d > 1.0f)
    start = h.pos + (t.pos - h.pos) / d * std::min(d, h.radius * 0.5f);
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
  add_popup({world_width * 0.5f, world_height * 0.5f}, col_gold_light, "LÊN ĐƯỜNG!", 1.6f);
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
  for (corpse &c : state.corpses)
    c.age += dt;
}

} // namespace sandtable
