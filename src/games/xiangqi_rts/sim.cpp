#include "sim.h"
#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace xiangqi {

game_state state;

namespace {
rng sim_rng{98765};

f32 point_segment_dist(vec2 p, vec2 a, vec2 b) {
  const vec2 ab = b - a;
  const f32 den = length_sq(ab);
  if (den <= 0.0001f)
    return distance(p, a);
  const f32 t = clamp(dot(p - a, ab) / den, 0.0f, 1.0f);
  return distance(p, a + ab * t);
}
} // namespace

bool is_point_on_bridge(vec2 pos) {
  for (const auto &b : bridges) {
    if (pos.x >= b.area.pos.x && pos.x <= b.area.pos.x + b.area.size.x &&
        pos.y >= b.area.pos.y && pos.y <= b.area.pos.y + b.area.size.y) {
      return true;
    }
  }
  return false;
}

bool is_point_in_river(vec2 pos) {
  return pos.y >= river_top && pos.y <= river_bottom && !is_point_on_bridge(pos);
}

void add_damage_popup(vec2 pos, f32 amount, rgba col, bool crit, const char *txt) {
  damage_popup p{};
  p.pos = pos + vec2{sim_rng.range(-12.0f, 12.0f), sim_rng.range(-10.0f, -5.0f)};
  p.amount = amount;
  p.timer = 0.9f;
  p.max_time = 0.9f;
  p.color = col;
  p.is_crit = crit;
  if (txt) {
    p.label = txt;
  } else {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "-%.0f", amount);
    p.label = buf;
  }
  state.popups.push_back(p);
}

void add_particle(vec2 pos, vec2 vel, rgba col, f32 size, f32 life, bool ring) {
  fx_particle p{};
  p.pos = pos;
  p.vel = vel;
  p.color = col;
  p.size = size;
  p.life = life;
  p.max_life = life;
  p.is_ring = ring;
  state.particles.push_back(p);
}

entt::entity spawn_unit(njin_ctx &ctx, piece_type type, faction side, vec2 pos) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();

  const piece_spec &sp = specs[static_cast<i32>(type)];
  unit_component u{};
  u.type = type;
  u.side = side;
  u.hp = sp.max_hp;
  u.max_hp = sp.max_hp;
  u.radius = sp.radius;
  u.facing = (side == faction::red) ? vec2{0.0f, -1.0f} : vec2{0.0f, 1.0f};

  transform tr{};
  tr.pos = pos;

  reg.emplace<transform>(e, tr);
  reg.emplace<unit_component>(e, u);

  if (side == faction::red) {
    state.red_pop++;
    if (type == piece_type::general)
      state.red_general = e;
  } else {
    state.black_pop++;
    if (type == piece_type::general)
      state.black_general = e;
  }

  // Spawn smoke/spark ring on summon
  const rgba glow = (side == faction::red) ? col_red_glow : col_black_glow;
  for (i32 i = 0; i < 8; ++i) {
    const f32 angle = i * 45.0f;
    const vec2 dir = from_angle(angle);
    add_particle(pos, dir * sim_rng.range(30.0f, 70.0f), glow, 4.0f, 0.35f);
  }

  return e;
}

bool recruit_unit(njin_ctx &ctx, piece_type type) {
  if (type == piece_type::general)
    return false;
  const i32 idx = static_cast<i32>(type);
  const piece_spec &sp = specs[idx];
  if (state.red_gold < sp.cost || state.red_pop >= game_state::max_pop) {
    return false;
  }

  state.red_gold -= sp.cost;
  const vec2 spawn_pos{1200.0f + sim_rng.range(-60.0f, 60.0f), 1320.0f + sim_rng.range(-30.0f, 30.0f)};
  const entt::entity e = spawn_unit(ctx, type, faction::red, spawn_pos);

  // Auto move forward a bit
  auto &u = world(ctx).get<unit_component>(e);
  u.move_target = spawn_pos + vec2{sim_rng.range(-40.0f, 40.0f), -80.0f};
  u.has_move_target = true;
  u.state = unit_state::moving;

  audio_play(ctx, sfx_type::command, 0.7f);
  return true;
}

void trigger_rally(njin_ctx &ctx) {
  if (state.rally_cooldown > 0.0f)
    return;
  state.rally_cooldown = 24.0f;
  state.rally_active_timer = 7.0f;
  audio_play(ctx, sfx_type::rally, 1.0f);

  if (state.red_general != entt::null && world(ctx).valid(state.red_general)) {
    const vec2 gpos = world(ctx).get<transform>(state.red_general).pos;
    add_damage_popup(gpos + vec2{0.0f, -40.0f}, 0.0f, col_gold, true, "HIỆU LỆNH TỔNG CÔNG!");
    for (i32 i = 0; i < 20; ++i) {
      const vec2 dir = from_angle(i * 18.0f);
      add_particle(gpos, dir * 140.0f, col_gold, 6.0f, 0.6f, true);
    }
  }
}

void issue_move_order(njin_ctx &ctx, const std::vector<entt::entity> &units, vec2 target_pos, bool attack_move) {
  (void)attack_move;
  if (units.empty())
    return;
  entt::registry &reg = world(ctx);

  const usize count = units.size();
  const f32 spacing = 46.0f;
  const i32 cols = static_cast<i32>(std::ceil(std::sqrt(static_cast<f32>(count))));

  for (usize i = 0; i < count; ++i) {
    if (!reg.valid(units[i]))
      continue;
    auto &u = reg.get<unit_component>(units[i]);
    const i32 row = static_cast<i32>(i) / cols;
    const i32 col = static_cast<i32>(i) % cols;
    const vec2 offset{(col - cols * 0.5f + 0.5f) * spacing, (row - cols * 0.5f + 0.5f) * spacing};
    u.move_target = target_pos + offset;
    u.has_move_target = true;
    u.attack_target = entt::null;
    u.state = unit_state::moving;
    u.charge_time = 0.0f;
  }

  // Ping ring on ground
  click_ping p{};
  p.pos = target_pos;
  p.timer = 0.0f;
  p.color = col_health_green;
  state.pings.push_back(p);

  audio_play(ctx, sfx_type::command, 0.65f);
}

void issue_attack_order(njin_ctx &ctx, const std::vector<entt::entity> &units, entt::entity target) {
  if (units.empty() || !world(ctx).valid(target))
    return;
  entt::registry &reg = world(ctx);
  const vec2 tpos = reg.get<transform>(target).pos;

  for (entt::entity e : units) {
    if (!reg.valid(e))
      continue;
    auto &u = reg.get<unit_component>(e);
    u.attack_target = target;
    u.move_target = tpos;
    u.has_move_target = true;
    u.state = unit_state::moving;
    u.charge_time = 0.0f;
  }

  click_ping p{};
  p.pos = tpos;
  p.timer = 0.0f;
  p.color = col_red_rim;
  state.pings.push_back(p);

  audio_play(ctx, sfx_type::slash, 0.8f);
}

void stop_units(njin_ctx &ctx, const std::vector<entt::entity> &units) {
  entt::registry &reg = world(ctx);
  for (entt::entity e : units) {
    if (!reg.valid(e))
      continue;
    auto &u = reg.get<unit_component>(e);
    u.has_move_target = false;
    u.attack_target = entt::null;
    u.state = unit_state::idle;
    u.charge_time = 0.0f;
  }
}

void sim_reset(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  reg.clear();

  state = game_state{};
  state.screen = game_screen::playing;
  state.red_gold = 280;
  state.black_gold = 280;
  state.red_pop = 0;
  state.black_pop = 0;
  state.match_time = 0.0f;
  state.rally_cooldown = 0.0f;
  state.rally_active_timer = 0.0f;

  // Initialize Camera
  state.camera_pos = {1200.0f, 1250.0f};
  state.camera_target = {1200.0f, 1250.0f};
  state.camera_zoom = 0.92f;
  state.camera_zoom_target = 0.92f;
  state.camera_entity = camera_spawn(ctx, state.camera_zoom, state.camera_pos);

  // Setup 3 Outposts along the river
  const vec2 outpost_positions[3] = {
      {460.0f, river_center_y},
      {1200.0f, river_center_y},
      {1940.0f, river_center_y}};
  const char *outpost_names[3] = {"Tháp Canh Tây", "Kỳ Đài Trung Tâm", "Tháp Canh Đông"};

  for (i32 i = 0; i < 3; ++i) {
    const entt::entity op = reg.create();
    outpost_component o{};
    o.pos = outpost_positions[i];
    o.radius = 85.0f;
    o.owner = -1;
    o.control = 0.0f;
    o.name = outpost_names[i];
    reg.emplace<outpost_component>(op, o);
  }

  // --- RED FACTION (South) ---
  // General in Palace
  spawn_unit(ctx, piece_type::general, faction::red, {1200.0f, 1480.0f});
  // 2 Sĩ
  spawn_unit(ctx, piece_type::advisor, faction::red, {1120.0f, 1480.0f});
  spawn_unit(ctx, piece_type::advisor, faction::red, {1280.0f, 1480.0f});
  // 2 Tượng
  spawn_unit(ctx, piece_type::elephant, faction::red, {980.0f, 1460.0f});
  spawn_unit(ctx, piece_type::elephant, faction::red, {1420.0f, 1460.0f});
  // 2 Mã
  spawn_unit(ctx, piece_type::horse, faction::red, {840.0f, 1440.0f});
  spawn_unit(ctx, piece_type::horse, faction::red, {1560.0f, 1440.0f});
  // 2 Xe
  spawn_unit(ctx, piece_type::chariot, faction::red, {660.0f, 1440.0f});
  spawn_unit(ctx, piece_type::chariot, faction::red, {1740.0f, 1440.0f});
  // 2 Pháo
  spawn_unit(ctx, piece_type::cannon, faction::red, {880.0f, 1280.0f});
  spawn_unit(ctx, piece_type::cannon, faction::red, {1520.0f, 1280.0f});
  // 5 Tốt
  const f32 red_pawn_x[5] = {580.0f, 890.0f, 1200.0f, 1510.0f, 1820.0f};
  for (f32 x : red_pawn_x) {
    spawn_unit(ctx, piece_type::pawn, faction::red, {x, 1140.0f});
  }

  // --- BLACK FACTION (North) ---
  // Marshal in Palace
  spawn_unit(ctx, piece_type::general, faction::black, {1200.0f, 120.0f});
  // 2 Sĩ
  spawn_unit(ctx, piece_type::advisor, faction::black, {1120.0f, 120.0f});
  spawn_unit(ctx, piece_type::advisor, faction::black, {1280.0f, 120.0f});
  // 2 Tượng
  spawn_unit(ctx, piece_type::elephant, faction::black, {980.0f, 140.0f});
  spawn_unit(ctx, piece_type::elephant, faction::black, {1420.0f, 140.0f});
  // 2 Mã
  spawn_unit(ctx, piece_type::horse, faction::black, {840.0f, 160.0f});
  spawn_unit(ctx, piece_type::horse, faction::black, {1560.0f, 160.0f});
  // 2 Xa
  spawn_unit(ctx, piece_type::chariot, faction::black, {660.0f, 160.0f});
  spawn_unit(ctx, piece_type::chariot, faction::black, {1740.0f, 160.0f});
  // 2 Pháo
  spawn_unit(ctx, piece_type::cannon, faction::black, {880.0f, 320.0f});
  spawn_unit(ctx, piece_type::cannon, faction::black, {1520.0f, 320.0f});
  // 5 Binh
  const f32 black_pawn_x[5] = {580.0f, 890.0f, 1200.0f, 1510.0f, 1820.0f};
  for (f32 x : black_pawn_x) {
    spawn_unit(ctx, piece_type::pawn, faction::black, {x, 460.0f});
  }
}

void sim_init(njin_ctx &ctx) {
  audio_init(ctx);
  sim_reset(ctx);
}

void update_economy_and_outposts(njin_ctx &ctx, f32 dt) {
  entt::registry &reg = world(ctx);

  // Passive gold generation: 7 gold/sec
  static f32 gold_timer = 0.0f;
  gold_timer += dt;
  if (gold_timer >= 1.0f) {
    gold_timer -= 1.0f;
    state.red_gold += 7;
    state.black_gold += 7;
  }

  // Update Outposts
  const auto unit_view = reg.view<const transform, const unit_component>();
  for (const auto [op_entity, op] : reg.view<outpost_component>().each()) {
    i32 red_count = 0;
    i32 black_count = 0;

    for (const auto [u_entity, tr, u] : unit_view.each()) {
      if (distance(tr.pos, op.pos) <= op.radius) {
        if (u.side == faction::red)
          red_count++;
        else
          black_count++;
      }
    }

    if (red_count > black_count) {
      op.control = std::min(100.0f, op.control + 28.0f * dt);
      if (op.control >= 100.0f && op.owner != 0) {
        op.owner = 0;
        add_damage_popup(op.pos, 0.0f, col_red_rim, true, "ĐỎ CHIẾM TIỀN TIÊU!");
        audio_play(ctx, sfx_type::rally, 0.8f);
      }
    } else if (black_count > red_count) {
      op.control = std::max(-100.0f, op.control - 28.0f * dt);
      if (op.control <= -100.0f && op.owner != 1) {
        op.owner = 1;
        add_damage_popup(op.pos, 0.0f, col_black_rim, true, "ĐEN CHIẾM TIỀN TIÊU!");
        audio_play(ctx, sfx_type::rally, 0.8f);
      }
    }

    // Gold reward per tick from controlled outpost
    if (gold_timer <= 0.05f) {
      if (op.owner == 0)
        state.red_gold += 3;
      else if (op.owner == 1)
        state.black_gold += 3;
    }
  }
}

void update_projectiles(njin_ctx &ctx, f32 dt) {
  entt::registry &reg = world(ctx);
  const auto view = reg.view<projectile_component>();
  std::vector<entt::entity> dead_projectiles;

  for (const auto [p_entity, p] : view.each()) {
    const f32 total_dist = distance(p.start_pos, p.target_pos);
    p.progress += (p.speed * dt) / (total_dist > 0.0f ? total_dist : 1.0f);

    if (p.progress >= 1.0f) {
      dead_projectiles.push_back(p_entity);

      // Impact explosion
      audio_play(ctx, sfx_type::cannon, 0.9f);
      const rgba exp_col = p.is_screened ? col_gold : col_red_rim;

      // Explosion ring & debris
      for (i32 i = 0; i < 16; ++i) {
        const vec2 dir = from_angle(i * 22.5f);
        add_particle(p.target_pos, dir * sim_rng.range(50.0f, 150.0f), exp_col, 5.0f, 0.45f);
      }
      add_particle(p.target_pos, {}, exp_col, p.splash_radius, 0.35f, true);

      // Splash damage
      for (const auto [u_entity, tr, u] : reg.view<transform, unit_component>().each()) {
        if (u.side != p.side) {
          const f32 d = distance(tr.pos, p.target_pos);
          if (d <= p.splash_radius + u.radius) {
            const f32 falloff = 1.0f - (d / (p.splash_radius + u.radius)) * 0.4f;
            const f32 dealt = p.damage * falloff;
            u.hp -= dealt;
            add_damage_popup(tr.pos, dealt, p.is_screened ? col_gold : col_red_rim, p.is_screened);
          }
        }
      }

      if (p.is_screened) {
        add_damage_popup(p.target_pos + vec2{0.0f, -25.0f}, 0.0f, col_gold, true, "BẠO KÍCH PHÁO GIÁ!");
      }
    } else {
      const vec2 base_pos = lerp(p.start_pos, p.target_pos, p.progress);
      const f32 arc = std::sin(p.progress * pi) * p.arc_height;
      p.pos = base_pos - vec2{0.0f, arc};

      // Trail particle
      if (sim_rng.unit() < 0.6f) {
        add_particle(p.pos, vec2{sim_rng.range(-15.0f, 15.0f), sim_rng.range(5.0f, 25.0f)},
                     p.is_screened ? col_gold_light : rgb(240, 120, 60), 3.5f, 0.3f);
      }
    }
  }

  reg.destroy(dead_projectiles.begin(), dead_projectiles.end());
}

void apply_damage(njin_ctx &ctx, entt::entity target_entity, f32 raw_damage, piece_type source_type, bool is_crit) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(target_entity))
    return;

  auto &target_u = reg.get<unit_component>(target_entity);
  const auto &target_tr = reg.get<transform>(target_entity);
  f32 final_damage = raw_damage;

  // 1. Advisor (Sĩ) Protective Aura: Check nearby friendly Sĩ
  bool has_advisor_shield = false;
  entt::entity protector_advisor = entt::null;
  for (const auto [other_e, other_tr, other_u] : reg.view<const transform, const unit_component>().each()) {
    if (other_u.side == target_u.side && other_u.type == piece_type::advisor && other_e != target_entity) {
      if (distance(other_tr.pos, target_tr.pos) <= 150.0f) {
        has_advisor_shield = true;
        protector_advisor = other_e;
        break;
      }
    }
  }

  if (has_advisor_shield) {
    final_damage *= 0.65f; // 35% damage reduction
    // If target is General, Sĩ intercepts 50% of the damage!
    if (target_u.type == piece_type::general && protector_advisor != entt::null && reg.valid(protector_advisor)) {
      auto &adv_u = reg.get<unit_component>(protector_advisor);
      const f32 intercepted = final_damage * 0.5f;
      adv_u.hp -= intercepted;
      final_damage -= intercepted;
      add_damage_popup(reg.get<transform>(protector_advisor).pos, intercepted, rgb(180, 140, 240), false, "HỘ VỆ!");
    }
  }

  // 2. Elephant (Tượng) Homeland Bulwark
  if (target_u.type == piece_type::elephant) {
    const bool on_home_soil = (target_u.side == faction::red && target_tr.pos.y > river_center_y) ||
                              (target_u.side == faction::black && target_tr.pos.y < river_center_y);
    if (on_home_soil) {
      final_damage *= 0.65f; // 35% damage reduction
    }
  }

  target_u.hp -= final_damage;
  add_damage_popup(target_tr.pos, final_damage, is_crit ? col_gold : col_white, is_crit);

  // Spark debris
  for (i32 i = 0; i < 5; ++i) {
    const vec2 vel = from_angle(sim_rng.range(0.0f, 360.0f)) * sim_rng.range(40.0f, 100.0f);
    add_particle(target_tr.pos, vel, is_crit ? col_gold : col_white, 3.0f, 0.25f);
  }

  if (source_type == piece_type::chariot && is_crit) {
    // Knockback
    const vec2 knock_dir = target_u.facing * -1.0f;
    reg.get<transform>(target_entity).pos += knock_dir * 30.0f;
  }
}

void ai_decision_tick(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);

  // 1. Production AI: Train units matching need
  if (state.black_gold >= 50 && state.black_pop < game_state::max_pop) {
    piece_type pick = piece_type::pawn;
    const f32 roll = sim_rng.unit();

    if (state.black_gold >= 200 && roll < 0.22f)
      pick = piece_type::elephant;
    else if (state.black_gold >= 180 && roll < 0.42f)
      pick = piece_type::chariot;
    else if (state.black_gold >= 150 && roll < 0.62f)
      pick = piece_type::cannon;
    else if (state.black_gold >= 110 && roll < 0.82f)
      pick = piece_type::horse;
    else if (state.black_gold >= 120 && roll < 0.90f)
      pick = piece_type::advisor;
    else
      pick = piece_type::pawn;

    const piece_spec &sp = specs[static_cast<i32>(pick)];
    if (state.black_gold >= sp.cost) {
      state.black_gold -= sp.cost;
      const vec2 bspawn{1200.0f + sim_rng.range(-60.0f, 60.0f), 260.0f + sim_rng.range(-30.0f, 30.0f)};
      const entt::entity ne = spawn_unit(ctx, pick, faction::black, bspawn);
      auto &nu = reg.get<unit_component>(ne);
      nu.move_target = bspawn + vec2{sim_rng.range(-40.0f, 40.0f), 80.0f};
      nu.has_move_target = true;
      nu.state = unit_state::moving;
    }
  }

  // 2. Tactical Behavior
  // Check if Black Marshal is under attack
  bool marshal_in_danger = false;
  vec2 marshal_pos{1200.0f, 120.0f};
  if (state.black_general != entt::null && reg.valid(state.black_general)) {
    const auto &mu = reg.get<unit_component>(state.black_general);
    marshal_pos = reg.get<transform>(state.black_general).pos;
    if (mu.hp < mu.max_hp * 0.75f) {
      marshal_in_danger = true;
    }
  }

  // Find nearest Red unit
  vec2 nearest_red_pos{1200.0f, 1300.0f};
  f32 closest_dist = 999999.0f;
  for (const auto [re, rtr, ru] : reg.view<const transform, const unit_component>().each()) {
    if (ru.side == faction::red) {
      const f32 d = distance(rtr.pos, marshal_pos);
      if (d < closest_dist) {
        closest_dist = d;
        nearest_red_pos = rtr.pos;
      }
    }
  }

  // Issue orders to Black units
  for (const auto [be, btr, bu] : reg.view<transform, unit_component>().each()) {
    if (bu.side != faction::black)
      continue;
    if (bu.type == piece_type::general)
      continue; // Marshal stays in Palace

    if (marshal_in_danger) {
      // Rush back to defend Marshal!
      bu.move_target = marshal_pos + vec2{sim_rng.range(-70.0f, 70.0f), sim_rng.range(20.0f, 80.0f)};
      bu.has_move_target = true;
      bu.state = unit_state::moving;
      continue;
    }

    // Advisors guard near Marshal
    if (bu.type == piece_type::advisor) {
      if (distance(btr.pos, marshal_pos) > 130.0f) {
        bu.move_target = marshal_pos + vec2{sim_rng.range(-60.0f, 60.0f), sim_rng.range(-30.0f, 50.0f)};
        bu.has_move_target = true;
        bu.state = unit_state::moving;
      }
      continue;
    }

    // Other military units: push toward outposts or attack player units
    if (!bu.has_move_target || bu.state == unit_state::idle) {
      // Choose target: outpost or advance across river
      if (sim_rng.unit() < 0.45f) {
        // Attack outpost
        const i32 op_idx = sim_rng.range(0, 2);
        const auto op_view = reg.view<outpost_component>();
        auto it = op_view.begin();
        std::advance(it, op_idx);
        if (it != op_view.end()) {
          const auto &op = op_view.get<outpost_component>(*it);
          bu.move_target = op.pos + vec2{sim_rng.range(-30.0f, 30.0f), sim_rng.range(-30.0f, 30.0f)};
          bu.has_move_target = true;
          bu.state = unit_state::moving;
        }
      } else {
        // Advance toward player lines
        bu.move_target = nearest_red_pos + vec2{sim_rng.range(-50.0f, 50.0f), sim_rng.range(-50.0f, 50.0f)};
        bu.has_move_target = true;
        bu.state = unit_state::moving;
      }
    }
  }
}

void sim_update(njin_ctx &ctx) {
  const f32 dt = delta(ctx);
  if (dt <= 0.0f || state.screen != game_screen::playing)
    return;

  state.match_time += dt;
  if (state.rally_cooldown > 0.0f)
    state.rally_cooldown = std::max(0.0f, state.rally_cooldown - dt);
  if (state.rally_active_timer > 0.0f)
    state.rally_active_timer = std::max(0.0f, state.rally_active_timer - dt);

  entt::registry &reg = world(ctx);

  // 1. Economy & Outposts
  update_economy_and_outposts(ctx, dt);

  // 2. Projectiles
  update_projectiles(ctx, dt);

  // 3. AI Director
  state.ai_decision_timer += dt;
  if (state.ai_decision_timer >= 2.2f) {
    state.ai_decision_timer = 0.0f;
    ai_decision_tick(ctx);
  }

  // 4. Units Simulation
  std::vector<entt::entity> dead_units;
  const auto unit_view = reg.view<transform, unit_component>();

  for (const auto [e, tr, u] : unit_view.each()) {
    if (u.hp <= 0.0f) {
      dead_units.push_back(e);
      continue;
    }

    u.anim_timer += dt;
    if (u.attack_timer > 0.0f)
      u.attack_timer = std::max(0.0f, u.attack_timer - dt);

    const piece_spec &sp = specs[static_cast<i32>(u.type)];

    // Check Elephant homeland regeneration
    if (u.type == piece_type::elephant) {
      const bool on_home = (u.side == faction::red && tr.pos.y > river_center_y) ||
                           (u.side == faction::black && tr.pos.y < river_center_y);
      if (on_home && u.hp < u.max_hp) {
        u.hp = std::min(u.max_hp, u.hp + 18.0f * dt);
      }
    }

    // Check Pawn River Crossing Awakening ("Qua Sông Thành Xe")
    if (u.type == piece_type::pawn && !u.crossed_river) {
      const bool crossed = (u.side == faction::red && tr.pos.y < river_top) ||
                           (u.side == faction::black && tr.pos.y > river_bottom);
      if (crossed) {
        u.crossed_river = true;
        u.max_hp += 80.0f;
        u.hp = std::min(u.max_hp, u.hp + 80.0f);
        add_damage_popup(tr.pos + vec2{0.0f, -25.0f}, 0.0f, col_gold, true, "QUA SÔNG!");
        if (u.side == faction::red) {
          audio_play(ctx, sfx_type::river_cross, 0.9f);
        }
        for (i32 p = 0; p < 12; ++p) {
          const vec2 vel = from_angle(p * 30.0f) * sim_rng.range(30.0f, 90.0f);
          add_particle(tr.pos, vel, col_gold, 5.0f, 0.5f);
        }
      }
    }

    // Speed Calculation
    f32 speed = sp.move_speed;
    if (is_point_in_river(tr.pos)) {
      speed *= 0.55f; // Water penalty
    }
    if (u.type == piece_type::pawn && u.crossed_river) {
      speed *= 1.35f; // River buff
    }
    if (u.side == faction::red && state.rally_active_timer > 0.0f) {
      speed *= 1.40f; // Rally cry
    }

    // General Long Uy Aura: Nearby friendly General boosts speed by 15%
    for (const auto [ge, gtr, gu] : unit_view.each()) {
      if (gu.side == u.side && gu.type == piece_type::general && ge != e) {
        if (distance(gtr.pos, tr.pos) <= 220.0f) {
          speed *= 1.15f;
          break;
        }
      }
    }

    // Movement Execution
    bool moving = false;
    vec2 move_dir{0.0f, 0.0f};

    if (u.has_move_target) {
      const vec2 to_target = u.move_target - tr.pos;
      const f32 dist = length(to_target);
      if (dist > 8.0f) {
        move_dir = normalize(to_target);
        moving = true;
        u.facing = move_dir;
        tr.pos += move_dir * speed * dt;

        // Chariot charge acceleration
        if (u.type == piece_type::chariot) {
          u.charge_time += dt;
          if (u.charge_time >= 1.2f) {
            u.state = unit_state::charging;
            speed *= 1.7f;
            if (sim_rng.unit() < 0.4f) {
              add_particle(tr.pos, move_dir * -40.0f, rgb(180, 160, 140), 4.0f, 0.25f);
            }
          }
        }
      } else {
        u.has_move_target = false;
        u.charge_time = 0.0f;
        u.state = unit_state::idle;
      }
    }

    // Soft collision repulsion with other units (avoid overlapping)
    for (const auto [oe, otr, ou] : unit_view.each()) {
      if (oe == e)
        continue;
      const vec2 diff = tr.pos - otr.pos;
      const f32 d = length(diff);
      const f32 min_d = u.radius + ou.radius;
      if (d < min_d && d > 0.001f) {
        const vec2 push = (diff / d) * (min_d - d) * 0.5f;
        tr.pos += push;
      }
    }

    // Clamp inside world borders
    tr.pos.x = clamp(tr.pos.x, u.radius + 15.0f, world_width - u.radius - 15.0f);
    tr.pos.y = clamp(tr.pos.y, u.radius + 15.0f, world_height - u.radius - 15.0f);

    // --- COMBAT SCAN & ATTACK ---
    entt::entity target_foe = entt::null;
    f32 target_dist = 999999.0f;

    // First check explicit attack target
    if (u.attack_target != entt::null && reg.valid(u.attack_target)) {
      target_foe = u.attack_target;
      target_dist = distance(tr.pos, reg.get<transform>(target_foe).pos);
    } else {
      // Scan for nearest enemy
      const f32 aggro_range = (u.type == piece_type::cannon) ? sp.attack_range : 260.0f;
      for (const auto [fe, ftr, fu] : unit_view.each()) {
        if (fu.side != u.side && fu.hp > 0.0f) {
          const f32 d = distance(tr.pos, ftr.pos);
          if (d <= aggro_range && d < target_dist) {
            target_dist = d;
            target_foe = fe;
          }
        }
      }
    }

    // Engage target
    if (target_foe != entt::null && reg.valid(target_foe)) {
      const auto &foe_tr = reg.get<transform>(target_foe);
      const f32 range = sp.attack_range + u.radius;

      if (target_dist <= range) {
        // In attack range
        u.facing = normalize(foe_tr.pos - tr.pos);
        if (u.attack_timer <= 0.0f) {
          // Attack ready!
          f32 base_dmg = sp.attack_damage;
          f32 cooldown = sp.attack_interval;

          // Pawn river buff
          if (u.type == piece_type::pawn && u.crossed_river) {
            base_dmg *= 1.50f;
            cooldown *= 0.65f;
          }

          // General Long Uy Aura: +20% damage
          for (const auto [ge, gtr, gu] : unit_view.each()) {
            if (gu.side == u.side && gu.type == piece_type::general && ge != e) {
              if (distance(gtr.pos, tr.pos) <= 220.0f) {
                base_dmg *= 1.20f;
                break;
              }
            }
          }

          // Rally active: +35% attack speed
          if (u.side == faction::red && state.rally_active_timer > 0.0f) {
            cooldown *= 0.65f;
          }

          u.attack_timer = cooldown;

          // Attack Logic by Unit Type
          if (u.type == piece_type::cannon) {
            // Check Pháo Giá (Screen mount): Is there any piece between Cannon and Target?
            bool is_screened = false;
            for (const auto [se, str, su] : unit_view.each()) {
              if (se != e && se != target_foe) {
                if (point_segment_dist(str.pos, tr.pos, foe_tr.pos) <= su.radius + 18.0f) {
                  is_screened = true;
                  break;
                }
              }
            }

            // Spawn ballistic projectile
            const entt::entity pe = reg.create();
            projectile_component proj{};
            proj.pos = tr.pos;
            proj.start_pos = tr.pos;
            proj.target_pos = foe_tr.pos;
            proj.target_entity = target_foe;
            proj.side = u.side;
            proj.source_type = piece_type::cannon;
            proj.speed = 460.0f;
            proj.damage = base_dmg * (is_screened ? 1.6f : 1.0f);
            proj.splash_radius = is_screened ? 85.0f : 60.0f;
            proj.arc_height = 90.0f;
            proj.is_screened = is_screened;
            proj.color = (u.side == faction::red) ? col_gold : col_black_glow;
            reg.emplace<projectile_component>(pe, proj);

            audio_play(ctx, sfx_type::cannon, 0.75f);
          } else {
            // Melee / Direct Strike
            bool is_crit = false;
            if (u.type == piece_type::chariot && u.state == unit_state::charging) {
              base_dmg += 60.0f;
              is_crit = true;
              u.charge_time = 0.0f;
              u.state = unit_state::idle;
              audio_play(ctx, sfx_type::charge, 0.9f);
            } else if (u.type == piece_type::horse) {
              // Horse flank critical against ranged/support
              const auto &foe_u = reg.get<unit_component>(target_foe);
              if (foe_u.type == piece_type::cannon || foe_u.type == piece_type::advisor || foe_u.type == piece_type::pawn) {
                base_dmg *= 1.45f;
                is_crit = true;
              }
            }

            apply_damage(ctx, target_foe, base_dmg, u.type, is_crit);
            audio_play(ctx, sfx_type::slash, 0.6f);

            // Elephant (Tượng) cleave stomp
            if (u.type == piece_type::elephant) {
              for (const auto [ce, ctr, cu] : unit_view.each()) {
                if (cu.side != u.side && ce != target_foe) {
                  if (distance(ctr.pos, foe_tr.pos) <= 80.0f) {
                    apply_damage(ctx, ce, base_dmg * 0.5f, u.type, false);
                  }
                }
              }
              add_particle(foe_tr.pos, {}, rgb(90, 160, 110), 65.0f, 0.3f, true);
            }
          }
        }
      } else if (!moving && !u.has_move_target) {
        // Move into attack range
        u.move_target = foe_tr.pos;
        u.has_move_target = true;
        u.state = unit_state::moving;
      }
    }
  }

  // 5. Cleanup dead units
  for (entt::entity de : dead_units) {
    if (!reg.valid(de))
      continue;
    const auto &du = reg.get<unit_component>(de);
    const auto &dtr = reg.get<transform>(de);

    if (du.side == faction::red) {
      state.red_pop = std::max(0, state.red_pop - 1);
      state.black_kills++;
      if (du.type == piece_type::general) {
        state.screen = game_screen::defeat;
        audio_play(ctx, sfx_type::defeat, 1.0f);
      }
    } else {
      state.black_pop = std::max(0, state.black_pop - 1);
      state.red_kills++;
      if (du.type == piece_type::general) {
        state.screen = game_screen::victory;
        audio_play(ctx, sfx_type::victory, 1.0f);
      }
    }

    // Death explosion of particles
    const rgba dcol = (du.side == faction::red) ? col_red_rim : col_black_rim;
    for (i32 i = 0; i < 14; ++i) {
      const vec2 vel = from_angle(i * 25.7f) * sim_rng.range(30.0f, 100.0f);
      add_particle(dtr.pos, vel, dcol, 4.5f, 0.5f);
    }

    reg.destroy(de);
  }

  // 6. Update Popups
  for (auto &pop : state.popups) {
    pop.timer -= dt;
    pop.pos.y -= 25.0f * dt;
  }
  std::erase_if(state.popups, [](const damage_popup &p) { return p.timer <= 0.0f; });

  // 7. Update Particles
  for (auto &pt : state.particles) {
    pt.life -= dt;
    pt.pos += pt.vel * dt;
  }
  std::erase_if(state.particles, [](const fx_particle &p) { return p.life <= 0.0f; });

  // 8. Update Pings
  for (auto &pg : state.pings) {
    pg.timer += dt;
  }
  std::erase_if(state.pings, [](const click_ping &p) { return p.timer >= p.max_time; });
}

} // namespace xiangqi
