#include "game.h"
#include <algorithm>
#include <cmath>
#include <utility>

// Every person is an entity (transform, person, dna). Each one runs a small
// activity (idle, walk, run, jump, sit, lie) and, now and then, pulls nearby
// free people into a group entity: two to greet, shake hands or spar, or a
// hand-in-hand chain of 2 .. max_chain. A group first gathers (everyone walks to
// a slot), then acts together, then lets everyone go. Neighbours are found
// through a njin::spatial_index rebuilt at the end of every frame, which also
// pushes overlapping people apart (collide()).
//
// Deciding can create a group and add `member` to other people, so the update
// loops walk a copied list of entities and fetch components afresh instead of
// holding references across a decision.

namespace crowd {
using namespace njin;
sim_state sim;

namespace {
constexpr f32 walk_stride = 18.0f; // world units per walk cycle
constexpr f32 run_stride = 30.0f;
constexpr f32 jump_time = 0.9f;
constexpr f32 jump_height = 12.0f;
constexpr f32 gather_speed = 38.0f;
constexpr f32 chain_speed = 22.0f;
constexpr f32 edge = 40.0f; // groups keep this far inside the world
constexpr f32 leap_back = 16.0f;   // a leaping attack first steps this far back
constexpr f32 leap_height = 10.0f;

std::vector<label> labels;

f32 fract(f32 v) { return v - std::floor(v); }

u32 frame_of(f32 phase, u32 frames) { return std::min((u32)(phase * (f32)frames), frames - 1); }

// Screen angle to one of 8 directions (y down, so +90 degrees is south).
u8 dir_of(vec2 v) {
  static constexpr u8 by_octant[8] = {dir_e, dir_se, dir_s, dir_sw, dir_w, dir_nw, dir_n, dir_ne};
  return by_octant[(i32)std::lround(angle_of(v) / 45.0f) & 7];
}

bool is_free(const entt::registry &reg, entt::entity e) {
  if (reg.all_of<member>(e))
    return false;
  const u8 act = reg.get<person>(e).act;
  return act == act_idle || act == act_walk || act == act_run;
}

// ---- neighbours and collision ------------------------------------------------
// Everyone is a disc at the feet, sized by build, in a njin::spatial_index
// rebuilt at the end of every frame (grid or quadtree, sim.index). The next
// frame finds free people for a group in it (nearest_free), and the same frame
// pushes overlapping discs apart with spatial_separate(): each person by its k
// nearest overlaps only (sim.neighbours). Someone who holds still for a reason
// (sitting, lying, jumping, at a slot or acting in a group) is `fixed`, so the
// others go round them and a chain walks through the crowd. Members of one
// group share a spatial group and do not push each other: their slots are
// closer than two discs.

vec2 slot_pos(const group &g, u32 slot);

constexpr f32 build_scale[builds] = {0.85f, 1.0f, 1.2f};

std::vector<entt::entity> crowd_list; // everyone but the gallery, item i of the index
std::vector<spatial_item> items;
std::vector<vec2 *> item_pos; // the transform of item i, written when pushed
spatial_index index;

bool holds_still(u8 act) { return act != act_idle && act != act_walk && act != act_run && act != act_gather; }

// Walks a view rather than looking components up by entity: a lookup apiece
// costs several times the rest of the step.
void index_build(entt::registry &reg) {
  crowd_list.clear();
  items.clear();
  item_pos.clear();
  const f32 near_slot = 2.0f * sim.radius * build_scale[builds - 1];
  for (auto [e, p, tr, genes] : reg.view<const person, transform, const dna>(entt::exclude<gallery_pin>).each()) {
    spatial_item it{.pos = tr.pos, .fixed = holds_still(p.act)};
    const member *m = p.act == act_gather || it.fixed ? reg.try_get<member>(e) : nullptr;
    if (m) {
      it.group = (u32)entt::to_integral(m->group) + 1;
      // The last steps to the slot are not pushed, or whoever stands there
      // would keep a gatherer out until its group gives up.
      if (p.act == act_gather && length_sq(slot_pos(reg.get<group>(m->group), m->slot) - tr.pos) < near_slot * near_slot)
        it.fixed = true;
    }
    it.radius = sim.radius * build_scale[std::min(genes.get(g_build), builds - 1)];
    crowd_list.push_back(e);
    items.push_back(it);
    item_pos.push_back(&tr.pos);
  }
  spatial_build(index, {.kind = sim.index, .bounds = {{0, 0}, world_size}}, items);
}

void collide() {
  static std::vector<vec2> push;
  sim.contacts = spatial_separate(index, push, sim.neighbours);
  for (u32 i = 0; i < push.size(); i++)
    if (push[i].x != 0.0f || push[i].y != 0.0f)
      *item_pos[i] = clamp(items[i].pos + push[i], {0, 0}, world_size);
}

bool free_only(u32 item, void *user) {
  const entt::registry &reg = *static_cast<const entt::registry *>(user);
  const entt::entity e = crowd_list[item];
  return reg.valid(e) && is_free(reg, e);
}

// Up to `want` free people nearest to `at` within `radius`, not `self`, where
// they stood at the end of the last frame.
void nearest_free(const entt::registry &reg, vec2 at, f32 radius, entt::entity self, u32 want,
                  std::vector<entt::entity> &out) {
  static std::vector<spatial_hit> hits;
  out.clear();
  if (spatial_size(index) != crowd_list.size()) // not built for this population yet
    return;
  spatial_query q{.at = at, .radius = radius, .filter = free_only, .user = const_cast<entt::registry *>(&reg)};
  spatial_nearest(index, q, hits, want + 1); // self may be among them
  for (const spatial_hit &h : hits)
    if (crowd_list[h.item] != self && out.size() < want)
      out.push_back(crowd_list[h.item]);
}

vec2 slot_pos(const group &g, u32 slot) {
  static constexpr f32 gaps[group_kinds] = {greet_distance, shake_distance, hold_spacing, spar_distance};
  const f32 gap = gaps[g.kind];
  const f32 n = (f32)g.members.size();
  return g.anchor + vec2{((f32)slot - (n - 1.0f) * 0.5f) * gap, 0.0f};
}

// Pairs face each other; a chain faces where it walks.
u8 slot_facing(const group &g, u32 slot) {
  if (g.kind == grp_chain)
    return g.dir;
  return slot == 0 ? dir_e : dir_w;
}

void walk(person &p, rng &r, f32 lo, f32 hi, u8 act) {
  p.act = act;
  p.vel = from_angle(r.range(0.0f, 360.0f)) * r.range(lo, hi);
  p.timer = act == act_run ? r.range(1.5f, 3.5f) : r.range(2.0f, 6.0f);
}

// Members sorted left to right take the slots, so nobody crosses over.
void start_group(entt::registry &reg, group_kind kind, std::vector<entt::entity> &members, rng &r) {
  std::sort(members.begin(), members.end(), [&](entt::entity a, entt::entity b) {
    return reg.get<transform>(a).pos.x < reg.get<transform>(b).pos.x;
  });
  vec2 centre{};
  for (const entt::entity m : members)
    centre += reg.get<transform>(m).pos;
  centre /= (f32)members.size();

  const entt::entity ge = reg.create();
  group &g = reg.emplace<group>(ge);
  g.kind = kind;
  g.dir = (r.next_u32() & 1u) ? dir_s : dir_n;
  g.members = members;
  const f32 half = (f32)members.size() * hold_spacing * 0.5f + greet_distance;
  g.anchor = clamp(centre, {edge + half, edge}, world_size - vec2{edge + half, edge});
  for (u32 s = 0; s < members.size(); s++) {
    reg.emplace<member>(members[s], member{ge, s});
    person &p = reg.get<person>(members[s]);
    p.act = act_gather;
    p.vel = {};
  }
}

bool try_pair(entt::registry &reg, entt::entity e, group_kind kind, rng &r) {
  static std::vector<entt::entity> near;
  nearest_free(reg, reg.get<transform>(e).pos, 140.0f, e, 1, near);
  if (near.empty())
    return false;
  std::vector<entt::entity> members{e, near[0]};
  start_group(reg, kind, members, r);
  return true;
}

bool try_chain(entt::registry &reg, entt::entity e, rng &r) {
  static std::vector<entt::entity> near;
  const u32 size = 2 + r.next_u32() % (std::max(sim.max_chain, 2u) - 1); // 2 .. max_chain
  nearest_free(reg, reg.get<transform>(e).pos, 180.0f, e, size - 1, near);
  if (near.empty())
    return false;
  std::vector<entt::entity> members{e};
  members.insert(members.end(), near.begin(), near.end());
  start_group(reg, grp_chain, members, r);
  return true;
}

void decide(entt::registry &reg, entt::entity e, rng &r) {
  const f32 x = r.unit();
  {
    person &p = reg.get<person>(e);
    p.vel = {};
    p.lie_rot = 0.0f;
    if (x < 0.12f) {
      p.act = act_idle;
      p.timer = r.range(1.0f, 3.0f);
      return;
    }
    if (x < 0.50f) {
      walk(p, r, 18.0f, 40.0f, act_walk);
      return;
    }
    if (x < 0.58f) {
      walk(p, r, 60.0f, 95.0f, act_run);
      return;
    }
    if (x < 0.62f) {
      p.act = act_jump;
      p.timer = jump_time;
      return;
    }
    if (x < 0.69f) {
      p.act = act_sit;
      p.timer = r.range(4.0f, 10.0f);
      return;
    }
    if (x < 0.72f) {
      p.act = act_lie;
      p.timer = r.range(5.0f, 12.0f);
      p.lie_rot = (r.next_u32() & 1u) ? pi * 0.5f : -pi * 0.5f;
      return;
    }
  }
  // Groups may add components, so no reference is held from here on.
  if (x < 0.78f) {
    const f32 k = r.unit();
    if (try_pair(reg, e, k < 0.4f ? grp_greet : (k < 0.75f ? grp_shake : grp_spar), r))
      return;
  }
  if (x >= 0.78f && x < 0.79f && try_chain(reg, e, r))
    return; // rare: a chain pulls in up to max_chain people for many seconds
  walk(reg.get<person>(e), r, 18.0f, 40.0f, act_walk);
}

void disband(entt::registry &reg, entt::entity ge, rng &r) {
  const std::vector<entt::entity> members = reg.get<group>(ge).members;
  reg.destroy(ge);
  for (const entt::entity m : members) {
    if (!reg.valid(m))
      continue;
    reg.remove<member>(m);
    decide(reg, m, r);
  }
}

void update_person(entt::registry &reg, entt::entity e, f32 dt, rng &r) {
  person &p = reg.get<person>(e);
  vec2 &pos = reg.get<transform>(e).pos;
  switch (p.act) {
  case act_idle:
    p.phase = fract(p.phase + dt * 0.5f);
    break;
  case act_walk:
  case act_run:
    pos += p.vel * dt;
    if (pos.x < 0.0f || pos.x > world_size.x) p.vel.x = -p.vel.x;
    if (pos.y < 0.0f || pos.y > world_size.y) p.vel.y = -p.vel.y;
    pos = clamp(pos, {0, 0}, world_size);
    p.dir = dir_of(p.vel);
    p.phase = fract(p.phase + length(p.vel) * dt / (p.act == act_run ? run_stride : walk_stride));
    break;
  case act_jump:
    break;
  case act_sit:
  case act_lie:
    p.phase = fract(p.phase + dt * 0.35f);
    break;
  case act_gather: {
    const member &m = reg.get<member>(e);
    const group &g = reg.get<group>(m.group);
    const vec2 target = slot_pos(g, m.slot);
    const vec2 to = target - pos;
    const f32 d = length(to);
    if (d < 1.0f) {
      pos = target;
      p.act = act_wait;
      p.dir = slot_facing(g, m.slot);
    } else {
      pos += to * (std::min(d, gather_speed * dt) / d);
      p.dir = dir_of(to);
      p.phase = fract(p.phase + gather_speed * dt / walk_stride);
    }
    return;
  }
  default: // waiting and acting people are moved by their group
    p.phase = fract(p.phase + dt * 0.5f);
    return;
  }
  p.timer -= dt;
  if (p.timer <= 0.0f)
    decide(reg, e, r);
}

bool is_leap(pose_id pose) { return pose == pose_leap_punch || pose == pose_leap_kick; }

// Leaping attack, over one cycle t (0..1): crouch while stepping back, fly in
// (striking near the top), land back at the slot. Returns the frame, sets the
// lift and how far behind the slot the attacker is.
u32 leap_frame(f32 t, f32 &lift, f32 &back) {
  lift = 0.0f;
  if (t < 0.25f) {
    back = leap_back * t / 0.25f;
    return 0;
  }
  if (t > 0.9f) {
    back = 0.0f;
    return 3;
  }
  const f32 u = (t - 0.25f) / 0.65f;
  back = leap_back * (1.0f - u);
  lift = 4.0f * leap_height * u * (1.0f - u);
  return u < 0.3f ? 1 : (u < 0.85f ? 2 : 3);
}

// Sparring: after each move the other one usually answers, most often with a
// punch combo, sometimes a kick, now and then a leaping attack.
void next_move(group &g, rng &r) {
  if (r.unit() < 0.7f)
    g.turn ^= 1u;
  const f32 k = r.unit();
  g.move = k < 0.45f ? pose_punch : (k < 0.75f ? pose_kick : (k < 0.88f ? pose_leap_punch : pose_leap_kick));
}

void update_group(entt::registry &reg, entt::entity ge, f32 dt, rng &r) {
  group &g = reg.get<group>(ge);
  if (!g.acting) {
    g.timer += dt;
    bool ready = true;
    for (const entt::entity m : g.members)
      ready = ready && reg.valid(m) && reg.get<person>(m).act == act_wait;
    if (!ready) {
      if (g.timer > 10.0f)
        disband(reg, ge, r); // someone could not get there
      return;
    }
    g.acting = true;
    g.phase = 0.0f;
    static constexpr u8 acts[group_kinds] = {act_greet, act_shake, act_chain, act_spar};
    switch (g.kind) {
    case grp_greet: g.timer = r.range(2.5f, 3.5f); break;
    case grp_shake: g.timer = r.range(2.0f, 3.0f); break;
    case grp_chain:
      g.timer = r.range(6.0f, 14.0f);
      g.vel = {0.0f, g.dir == dir_s ? chain_speed : -chain_speed};
      break;
    default:
      g.timer = r.range(3.0f, 6.0f);
      g.turn = (u8)(r.next_u32() & 1u);
      next_move(g, r);
      break;
    }
    for (const entt::entity m : g.members)
      reg.get<person>(m).act = acts[g.kind];
  }

  static constexpr f32 rates[group_kinds] = {1.6f, 2.8f, chain_speed / walk_stride, 1.5f};
  const f32 before = g.phase;
  const f32 rate = g.kind == grp_spar && is_leap(g.move) ? 1.0f : rates[g.kind]; // a leap takes longer
  g.phase = fract(g.phase + dt * rate);
  if (g.kind == grp_spar && g.phase < before)
    next_move(g, r);
  if (g.kind == grp_chain) {
    g.anchor += g.vel * dt;
    if (g.anchor.y < edge || g.anchor.y > world_size.y - edge) {
      g.vel.y = -g.vel.y; // turn round at the edge
      g.dir = g.dir == dir_s ? dir_n : dir_s;
      g.anchor.y = std::clamp(g.anchor.y, edge, world_size.y - edge);
    }
  }
  for (u32 s = 0; s < g.members.size(); s++) {
    const entt::entity m = g.members[s];
    reg.get<transform>(m).pos = slot_pos(g, s);
    person &p = reg.get<person>(m);
    p.dir = slot_facing(g, s);
    if (g.kind == grp_spar) {
      // The one not attacking keeps guard: the first frame of a punch.
      const bool attacking = s == g.turn;
      p.pose = attacking ? g.move : pose_punch;
      p.phase = attacking ? g.phase : 0.0f;
      if (attacking && is_leap(g.move)) {
        f32 lift, back;
        leap_frame(g.phase, lift, back);
        reg.get<transform>(m).pos.x += s == 0 ? -back : back; // slot 0 faces east
      }
      continue;
    }
    // Greeters wave out of step; hands that touch move together.
    p.phase = g.kind == grp_greet ? fract(g.phase + 0.3f * (f32)s) : g.phase;
  }
  g.timer -= dt;
  if (g.timer <= 0.0f)
    disband(reg, ge, r);
}

// Jump: crouch, take off, in the air, land. Returns the frame, sets the lift.
u32 jump_frame(f32 t, f32 &lift) {
  lift = 0.0f;
  if (t < 0.15f)
    return 0;
  if (t > 0.85f)
    return 3;
  const f32 u = (t - 0.15f) / 0.7f;
  lift = 4.0f * jump_height * u * (1.0f - u);
  return u < 0.2f ? 1 : 2;
}

entt::entity spawn(entt::registry &reg, vec2 pos, const dna &genes, const person &p) {
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  reg.emplace<person>(e, p);
  reg.emplace<dna>(e, genes);
  return e;
}

void pinned(entt::registry &reg, vec2 pos, pose_id pose, u8 dir, const dna &genes, f32 phase) {
  person p;
  p.act = act_pinned;
  p.pose = pose;
  p.dir = dir;
  p.phase = phase;
  if (pose == pose_lie)
    p.lie_rot = pi * 0.5f;
  reg.emplace<gallery_pin>(spawn(reg, pos, genes, p));
}

// Every pose from every baked direction, then the group poses side by side.
void build_gallery(entt::registry &reg, rng &r) {
  labels.clear();
  dna model = dna::random(r);
  model.set(g_build, 1);
  const vec2 o = gallery_origin;
  labels.push_back({o + vec2{0, -34}, "PHÒNG TRƯNG BÀY  (tư thế x 8 hướng)"});
  static constexpr u8 base_of[8] = {0, 1, 2, 3, 4, 3, 2, 1};
  static constexpr const char *dir_names[8] = {"S", "SE", "E", "NE", "N", "NW", "W", "SW"};
  for (u32 d = 0; d < 8; d++)
    labels.push_back({o + vec2{128.0f + (f32)d * 44.0f - 6.0f, -12.0f}, dir_names[d]});
  for (u32 pose = 0; pose < pose_count; pose++) {
    const f32 y = o.y + 30.0f + (f32)pose * 48.0f;
    labels.push_back({{o.x, y - 20.0f}, pose_desc((pose_id)pose).name});
    for (u32 d = 0; d < 8; d++) {
      if (!(pose_desc((pose_id)pose).dirs >> base_of[d] & 1u))
        continue;
      const vec2 at{o.x + 128.0f + (f32)d * 44.0f, y + (pose == pose_lie ? -8.0f : 0.0f)};
      pinned(reg, at, (pose_id)pose, (u8)d, model, (f32)d * 0.11f);
    }
  }

  // Together: a greeting, a handshake, and hand-in-hand chains.
  f32 y = o.y + 30.0f + (f32)pose_count * 48.0f + 50.0f;
  const f32 x = o.x + 128.0f;
  labels.push_back({{o.x, y - 20.0f}, "chào nhau"});
  pinned(reg, {x, y}, pose_wave, dir_e, dna::random(r), 0.0f);
  pinned(reg, {x + greet_distance, y}, pose_wave, dir_w, dna::random(r), 0.3f);
  labels.push_back({{o.x + 230.0f, y - 20.0f}, "bắt tay"});
  pinned(reg, {x + 200.0f, y}, pose_shake, dir_e, dna::random(r), 0.0f);
  pinned(reg, {x + 200.0f + shake_distance, y}, pose_shake, dir_w, dna::random(r), 0.0f);
  y += 50.0f;
  labels.push_back({{o.x, y - 20.0f}, "đấu võ"});
  pinned(reg, {x, y}, pose_punch, dir_e, dna::random(r), 0.0f);
  pinned(reg, {x + spar_distance, y}, pose_punch, dir_w, dna::random(r), 0.5f);
  pinned(reg, {x + 200.0f, y}, pose_kick, dir_e, dna::random(r), 0.0f);
  pinned(reg, {x + 200.0f + spar_distance, y}, pose_kick, dir_w, dna::random(r), 0.5f);
  // Leaping in the gallery stays on the spot (no step back).
  y += 50.0f;
  labels.push_back({{o.x, y - 20.0f}, "nhảy đấm / nhảy đá"});
  pinned(reg, {x, y}, pose_leap_punch, dir_e, dna::random(r), 0.0f);
  pinned(reg, {x + spar_distance, y}, pose_punch, dir_w, dna::random(r), 0.0f);
  pinned(reg, {x + 200.0f, y}, pose_leap_kick, dir_e, dna::random(r), 0.0f);
  pinned(reg, {x + 200.0f + spar_distance, y}, pose_punch, dir_w, dna::random(r), 0.0f);
  const u32 chains[] = {2, 3, 5, 8};
  for (u32 dirn = 0; dirn < 2; dirn++)
    for (u32 n : chains) {
      y += 50.0f;
      labels.push_back({{o.x, y - 20.0f}, "nắm tay " + std::to_string(n) + (dirn ? " (N)" : " (S)")});
      for (u32 s = 0; s < n; s++) {
        const pose_id pose = s == 0 ? pose_hold_r : (s + 1 == n ? pose_hold_l : pose_hold_b);
        pinned(reg, {x + (f32)s * hold_spacing, y}, pose, dirn ? dir_n : dir_s, dna::random(r), 0.0f);
      }
    }
}

entt::entity spawn_random(entt::registry &reg, rng &r, vec2 pos, const dna &genes) {
  person p;
  p.phase = r.unit();
  p.dir = (u8)(r.next_u32() % 8);
  const entt::entity e = spawn(reg, pos, genes, p);
  decide(reg, e, r);
  return e;
}

template <class... T> void destroy_all(entt::registry &reg) {
  std::vector<entt::entity> doomed;
  for (const entt::entity e : reg.view<T...>())
    doomed.push_back(e);
  reg.destroy(doomed.begin(), doomed.end());
}
} // namespace

const char *activity_name(u8 act) {
  static constexpr const char *names[] = {"đứng", "đi bộ", "chạy", "nhảy", "ngồi", "nằm", "đến chỗ hẹn",
                                          "chờ nhóm", "chào nhau", "bắt tay", "nắm tay", "đấu võ", "trưng bày"};
  return act <= act_pinned ? names[act] : "?";
}

const char *group_kind_name(group_kind kind) {
  static constexpr const char *names[] = {"chào nhau", "bắt tay", "nắm tay", "đấu võ"};
  return kind < group_kinds ? names[kind] : "?";
}

const std::vector<label> &gallery_labels() { return labels; }

void sim_debug_components(context &ctx) {
  debug_component<person>(ctx, "person", [](const person &p) {
    return json_value::make_object()
        .set("activity", activity_name(p.act))
        .set("dir", (u32)p.dir)
        .set("speed", length(p.vel))
        .set("timer", p.timer)
        .set("phase", p.phase)
        .set("pose", p.act == act_pinned || p.act == act_spar ? pose_desc(p.pose).name : "-");
  });
  debug_component<dna>(ctx, "dna", [](const dna &d) {
    json_value v = json_value::make_object().set("hex", d.hex());
    for (u32 i = 0; i < gene_count; i++)
      v.set(gene_desc((gene)i).name, d.get((gene)i));
    return v;
  });
  debug_component<member>(ctx, "member", [](const member &m) {
    return json_value::make_object().set("group", (u32)entt::to_integral(m.group)).set("slot", m.slot);
  });
  debug_component<group>(ctx, "group", [](const group &g) {
    json_value ids = json_value::make_array();
    for (const entt::entity m : g.members)
      ids.push((u32)entt::to_integral(m));
    return json_value::make_object()
        .set("kind", group_kind_name(g.kind))
        .set("acting", g.acting)
        .set("size", (u32)g.members.size())
        .set("timer", g.timer)
        .set("anchor", json_value::make_array().push(g.anchor.x).push(g.anchor.y))
        .set("members", ids);
  });
  debug_component<gallery_pin>(ctx, "gallery_pin", [](const gallery_pin &) { return json_value::make_object(); });
}

void sim_populate(context &ctx, u32 crowd) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  destroy_all<group>(reg);
  destroy_all<person>(reg);
  crowd_list.clear(); // the index holds the old population until rebuilt
  build_gallery(reg, r);
  for (u32 i = 0; i < crowd; i++) {
    const entt::entity e =
        spawn_random(reg, r, {r.range(0.0f, world_size.x), r.range(0.0f, world_size.y)}, dna::random(r));
    reg.get<person>(e).timer *= r.unit(); // not everyone decides on the same frame
  }
  index_build(reg);
}

void sim_spawn_family(context &ctx, entt::entity parent) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(parent) || !reg.all_of<person, dna>(parent) || reg.all_of<gallery_pin>(parent) ||
      crowd_list.empty())
    return;
  rng &r = random(ctx);
  const dna mom = reg.get<dna>(parent);
  const vec2 at = reg.get<transform>(parent).pos;
  const entt::entity mate_e = crowd_list[r.next_u32() % crowd_list.size()];
  const dna mate = reg.valid(mate_e) ? reg.get<dna>(mate_e) : dna::random(r);
  for (i32 i = 0; i < 24; i++) {
    dna child = dna::cross(mom, mate, r);
    child.mutate(r, 0.15f);
    const vec2 pos = clamp(at + from_angle(r.range(0.0f, 360.0f)) * r.range(20.0f, 90.0f), {0, 0}, world_size);
    spawn_random(reg, r, pos, child);
  }
}

void sim_update(context &ctx, f32 dt) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  // Gallery: every pose loops at its own pace.
  static constexpr f32 rate[pose_count] = {0.5f, 1.3f, 2.2f, 0.8f, 0.35f, 0.35f, 1.6f, 2.8f, 1.3f, 1.3f, 1.3f, 1.5f, 1.5f, 1.0f, 1.0f};
  for (auto [e, p] : reg.view<person, gallery_pin>().each())
    p.phase = fract(p.phase + dt * rate[p.pose]);

  // crowd_list is from the end of the last frame: someone born since then
  // starts next frame. Deciding may add components, so no references are kept.
  for (const entt::entity e : crowd_list)
    if (reg.valid(e))
      update_person(reg, e, dt, r);

  static std::vector<entt::entity> groups;
  groups.clear();
  for (const entt::entity ge : reg.view<group>())
    groups.push_back(ge);
  for (const entt::entity ge : groups)
    if (reg.valid(ge))
      update_group(reg, ge, dt, r);
  index_build(reg); // also for the next frame's nearest_free
  if (sim.collide)
    collide();
  else
    sim.contacts = 0;
}

void sim_instance(const entt::registry &reg, entt::entity e, bool selected, instance &out) {
  const person &p = reg.get<person>(e);
  const vec2 pos = reg.get<transform>(e).pos;
  pose_id pose = pose_idle;
  u32 frame = 0;
  f32 lift = 0.0f;
  switch (p.act) {
  case act_walk:
  case act_gather:
    pose = pose_walk;
    frame = frame_of(p.phase, 6);
    break;
  case act_run:
    pose = pose_run;
    frame = frame_of(p.phase, 6);
    break;
  case act_jump:
    pose = pose_jump;
    frame = jump_frame(1.0f - p.timer / jump_time, lift);
    break;
  case act_sit:
    pose = pose_sit;
    frame = frame_of(p.phase, 2);
    break;
  case act_lie:
    pose = pose_lie;
    frame = frame_of(p.phase, 2);
    break;
  case act_greet:
    pose = pose_wave;
    frame = frame_of(p.phase, 4);
    break;
  case act_shake:
    pose = pose_shake;
    frame = frame_of(p.phase, 4);
    break;
  case act_chain: {
    const member &m = reg.get<member>(e);
    const u32 n = (u32)reg.get<group>(m.group).members.size();
    pose = m.slot == 0 ? pose_hold_r : (m.slot + 1 == n ? pose_hold_l : pose_hold_b);
    frame = 1 + frame_of(p.phase, 6);
    break;
  }
  case act_spar:
  case act_pinned:
    pose = p.pose;
    if (is_leap(pose)) {
      f32 back;
      frame = leap_frame(p.phase, lift, back);
    } else if (pose == pose_jump)
      frame = jump_frame(p.phase, lift);
    else if (pose >= pose_hold_l && pose <= pose_hold_b)
      frame = 1 + frame_of(p.phase, 6);
    else
      frame = frame_of(p.phase, pose_desc(pose).frames);
    break;
  default: // idle, waiting for the group
    frame = frame_of(p.phase, 4);
    break;
  }
  const dna &genes = reg.get<dna>(e);
  const cell_ref c = cell_for(pose, p.dir, frame);
  out = {pos.x, pos.y, c.cell, (f32)c.head_dir + (selected ? 8.0f : 0.0f),
         (f32)genes.word[0], (f32)genes.word[1], lift, pose == pose_lie ? p.lie_rot : 0.0f};
}

u32 sim_people(const entt::registry &reg) {
  u32 n = 0;
  for ([[maybe_unused]] const entt::entity e : reg.view<const person>(entt::exclude<gallery_pin>))
    n++;
  return n;
}

u32 sim_acting_groups(const entt::registry &reg, group_kind kind) {
  u32 n = 0;
  for (const auto [e, g] : reg.view<const group>().each())
    n += g.acting && g.kind == kind;
  return n;
}
} // namespace crowd
