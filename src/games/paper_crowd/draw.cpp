// Drawing. Each person is one quad, drawn by
// draw_instanced() with assets/person.vs and person.fs: the CPU writes twelve
// floats a person into one buffer and the GPU does the rest. The CPU still
// decides the pose: it turns what a person is doing into a pose id, a phase
// and a couple of parameters, and moves, lifts, tips and flips the quad; the
// shader draws the figure inside it.
//
// Order: the paper, the figures and pets sorted by the y of their feet, then
// the paper grain multiplied over all of it so the paint sits in the paper's
// texture.
#include "figure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace paper_crowd {
namespace {
constexpr f32 pi = 3.14159265f;
constexpr f32 tau = 2.0f * pi;
constexpr rgba ink{0.12f, 0.11f, 0.13f, 1.0f};
constexpr rgba paper_color{0.945f, 0.94f, 0.925f, 1.0f};

// Everything one quad carries: see the table at the top of person.fs.
struct quad {
  u32 pose = pose_stand;
  f32 phase = 0.0f;  // 0..1, the pose's own cycle
  f32 extra = 0.0f;  // 0..1, pose-specific
  f32 extra2 = 0.0f; // 0..1, pose-specific
  f32 lift = 0.0f;   // world pixels off the ground
  f32 tilt = 0.0f;   // degrees around the pivot
  bool flip = false; // face left
};

f32 cycle01(f32 x) { return x - std::floor(x); }

f32 angle01(vec2 v) { return cycle01(std::atan2(v.y, v.x) / tau); }

rgba shade(rgba c, f32 k) { return {c.r * k, c.g * k, c.b * k, c.a}; }

vec2 clamp_len(vec2 v, f32 max_len) {
  const f32 len = length(v);
  return len > max_len ? v * (max_len / len) : v;
}

void capsule(njin_ctx &ctx, vec2 a, vec2 b, f32 thickness, rgba color) {
  draw_line(ctx, a, b, thickness, color);
  draw_circle(ctx, a, thickness * 0.5f, color);
  draw_circle(ctx, b, thickness * 0.5f, color);
}

void walk_quad(quad &q, const person &p, vec2 velocity, bool running) {
  // One full stride every ~14 px walked (~18 px running).
  const f32 cycle = p.walk * (running ? 0.35f : 0.45f);
  q.pose = running ? pose_run : pose_walk;
  q.phase = cycle01(cycle / tau);
  // The shader draws facing right: a left-facing person's heading is mirrored
  // along with the quad.
  const vec2 heading = length_sq(velocity) > 1.0f ? velocity : vec2{p.face, 0.0f};
  q.extra = angle01({heading.x * p.face, heading.y});
  q.lift = std::abs(std::cos(cycle)) * (running ? 1.8f : 0.55f);
}

quad person_quad(const entt::registry &reg, entt::entity e, const person &p, const topdown_body &b,
                 vec2 pos, f32 now) {
  quad q;
  q.flip = p.face < 0.0f;
  const f32 t = p.clock;
  const f32 f = p.face;
  const f32 speed = length(b.velocity);

  switch (p.act) {
  case act_idle:
    q.phase = cycle01((now * 1.3f + p.walk) / tau);
    break;
  case act_wave:
    q.pose = pose_wave;
    q.phase = cycle01(t * 10.0f / tau);
    break;
  case act_jump:
    if (t < 0.0f) // waiting for its turn in a cheer
      break;
    q.pose = pose_jump;
    q.phase = cycle01(t / 0.62f);
    q.lift = std::sin(pi * q.phase) * 9.0f;
    break;
  case act_jacks:
    q.pose = pose_jacks;
    q.phase = cycle01(t / 0.5f);
    q.lift = std::sin(q.phase * pi) * 2.0f;
    break;
  case act_dance:
    q.pose = pose_dance;
    q.phase = cycle01(t * 6.0f / tau);
    q.tilt = std::sin(t * 3.0f) * 11.0f * p.spin;
    q.lift = std::abs(std::sin(t * 6.0f)) * 0.8f;
    break;
  case act_cartwheel:
    q.pose = pose_cartwheel;
    q.tilt = p.spin * t / 0.8f * 360.0f;
    break;
  case act_handstand:
    q.pose = pose_handstand;
    q.tilt = 180.0f * std::min(1.0f, t * 3.0f) * f; // swing up into it
    q.phase = cycle01(t * 4.0f / tau);
    break;
  case act_lie:
    q.pose = pose_lie;
    q.tilt = 90.0f * f;
    q.lift = -7.0f;
    q.phase = cycle01(t * 1.4f / tau);
    break;
  case act_sit:
    q.pose = pose_sit;
    break;
  case act_ring: {
    const bool joined = p.ring >= 0 && p.ring < static_cast<i32>(g.rings.size());
    static const std::vector<entt::entity> none;
    const std::vector<entt::entity> &m = joined ? g.rings[static_cast<usize>(p.ring)].members : none;
    const auto it = std::find(m.begin(), m.end(), e);
    if (m.size() < 2 || it == m.end() || distance(pos, p.target) > 3.0f) {
      walk_quad(q, p, b.velocity, false);
      break;
    }
    // Reach for the neighbours on either side; their hands reach back and the
    // two meet halfway. The quad is not flipped, so these are world directions.
    const usize n = m.size();
    const usize k = static_cast<usize>(it - m.begin());
    const vec2 a = (reg.get<transform>(m[(k + n - 1) % n]).pos - pos) * (1.0f / p.size);
    const vec2 c = (reg.get<transform>(m[(k + 1) % n]).pos - pos) * (1.0f / p.size);
    const vec2 left = a.x < c.x ? a : c;
    const vec2 right = a.x < c.x ? c : a;
    q.pose = pose_ring;
    q.flip = false;
    q.extra = angle01(clamp_len(left * 0.5f + vec2{0.0f, 2.4f}, 6.4f));
    q.extra2 = angle01(clamp_len(right * 0.5f + vec2{0.0f, 2.4f}, 6.4f));
    q.lift = std::abs(std::sin(now * 4.0f + static_cast<f32>(k))) * 0.8f;
    break;
  }
  default:
    // stroll, run, follow, chase, flee, come: walking or running.
    if (speed > 6.0f)
      walk_quad(q, p, b.velocity, speed > 70.0f);
    break;
  }
  // Any pose still sliding to a stop walks it out.
  if (speed > 12.0f && (p.act == act_idle || p.act == act_wave || p.act == act_sit))
    walk_quad(q, p, b.velocity, false);
  return q;
}

// Adds one quad for the person shader. `base` is where the feet stand. A pose
// the sheet has (`baked`) is written as the two frames to blend; any other is
// written as the pose itself, for the shader to compute.
void emit(std::vector<instance> &out, entt::entity e, const person &p, vec2 base, const quad &q, bool baked) {
  const vec2 centre = base + vec2{0.0f, -q.lift} + pivot * p.size;
  const f32 side = quad_units * p.size;
  const f32 flip = q.flip ? -1.0f : 1.0f;
  const f32 cloth = static_cast<f32>(p.cloth);
  sheet_pick pick;
  if (baked && sheet_frames(q.pose, q.phase, q.extra, pick)) {
    out.push_back({centre.x, centre.y, side, q.tilt, static_cast<f32>(pick.f0), cloth, flip, 0.0f, pick.blend,
                   static_cast<f32>(pick.f1), 0.0f, 0.0f});
    return;
  }
  // A number that differs from one person to the next: it seeds their paper.
  const f32 seed = cycle01(static_cast<f32>(entt::to_entity(e)) * 0.618034f);
  out.push_back({centre.x, centre.y, side, q.tilt, static_cast<f32>(q.pose), cloth, flip, 1.0f, q.phase, q.extra,
                 q.extra2, seed});
}

void draw_pet(njin_ctx &ctx, const pet &a, const topdown_body &b, vec2 base) {
  const f32 dir = b.velocity.x < -1.0f ? -1.0f : (b.velocity.x > 1.0f ? 1.0f : (a.offset.x < 0 ? 1.0f : -1.0f));
  auto at = [&](f32 x, f32 y) { return base + vec2{x * dir, y}; };
  const f32 step = std::sin(a.walk * 0.9f) * (length(b.velocity) > 5.0f ? 1.2f : 0.0f);
  const rgba coat = a.coat;
  const rgba dark = shade(coat, coat.r > 0.5f ? 0.72f : 1.6f);

  // Legs, then body, head, ears and tail.
  draw_line(ctx, at(-2.6f, -3.0f), at(-2.6f - step, 0.0f), 0.9f, dark);
  draw_line(ctx, at(-1.6f, -3.0f), at(-1.6f + step, 0.0f), 0.9f, dark);
  draw_line(ctx, at(2.2f, -3.0f), at(2.2f + step, 0.0f), 0.9f, dark);
  draw_line(ctx, at(3.0f, -3.0f), at(3.0f - step, 0.0f), 0.9f, dark);
  capsule(ctx, at(-2.8f, -3.6f), at(2.8f, -3.6f), 3.2f, coat);
  const f32 nod = a.sniffing ? 2.2f : 0.0f;
  draw_circle(ctx, at(4.2f, -5.6f + nod), 1.9f, coat);
  if (a.cat) {
    draw_triangle(ctx, at(3.3f, -6.8f + nod), at(4.2f, -7.3f + nod), at(3.5f, -9.0f + nod), dark);
    draw_triangle(ctx, at(4.6f, -7.2f + nod), at(5.4f, -6.6f + nod), at(5.3f, -8.8f + nod), dark);
    const f32 flick = std::sin(a.walk * 0.3f + a.timer * 3.0f) * 1.2f;
    draw_line(ctx, at(-3.8f, -4.0f), at(-5.2f, -7.0f), 0.8f, coat);
    draw_line(ctx, at(-5.2f, -7.0f), at(-4.4f + flick, -9.4f), 0.8f, coat);
  } else {
    draw_circle(ctx, at(3.4f, -6.3f + nod), 0.9f, dark);
    draw_circle(ctx, at(5.9f, -5.2f + nod), 0.8f, dark); // nose
    const f32 wag = std::sin(a.timer * 18.0f) * 1.4f;
    draw_line(ctx, at(-3.8f, -4.2f), at(-6.0f, -6.4f + wag), 0.9f, coat);
  }
}

const char *activity_name(activity a) {
  switch (a) {
  case act_idle: return "đứng ngắm";
  case act_stroll: return "đi dạo";
  case act_run: return "chạy";
  case act_wave: return "vẫy tay";
  case act_jump: return "nhảy";
  case act_jacks: return "nhảy dang tay chân";
  case act_dance: return "nhảy múa";
  case act_cartwheel: return "lộn nhào";
  case act_handstand: return "trồng cây chuối";
  case act_lie: return "nằm dang tay";
  case act_sit: return "ngồi nghỉ";
  case act_follow: return "đi cùng bạn";
  case act_chase: return "đuổi bắt";
  case act_flee: return "chạy trốn";
  case act_ring: return "nắm tay thành vòng";
  case act_come: return "tới chỗ được gọi";
  default: return "";
  }
}

struct draw_item {
  f32 y;
  entt::entity e;
  usize quad; // index into the quads built this draw; people only
  bool is_pet;
  bool shown = true; // people only: inside the camera's view, so written as an instance
};

void draw_paper_grain(njin_ctx &ctx) {
  if (!g.paper.id)
    return;
  blend_begin(ctx, blend_multiply);
  shader_begin(ctx, g.paper);
  draw_rect(ctx, {{0.0f, 0.0f}, {world_w, world_h}}, colors::white);
  shader_end(ctx);
  blend_end(ctx);
}
} // namespace

void draw_paper(njin_ctx &ctx) { draw_rect(ctx, {{0.0f, 0.0f}, {world_w, world_h}}, paper_color); }

void draw_crowd(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  const f32 now = elapsed(ctx);

  std::vector<draw_item> items;
  std::vector<quad> quads;
  for (auto [e, p, b, tr] : reg.view<const person, const topdown_body, const transform>().each()) {
    items.push_back({tr.pos.y, e, quads.size(), false});
    quads.push_back(person_quad(reg, e, p, b, tr.pos, now));
  }
  for (auto [e, a, tr] : reg.view<const pet, const transform>().each())
    items.push_back({tr.pos.y, e, 0, true});
  std::sort(items.begin(), items.end(), [](const draw_item &x, const draw_item &y) { return x.y < y.y; });

  if (!g.person.id || !g.instances.id) {
    // Without the shader or instancing, at least show where everyone is.
    for (const draw_item &it : items)
      draw_circle(ctx, reg.get<transform>(it.e).pos + vec2{0.0f, -8.0f}, 3.0f, ink);
    return;
  }

  // Every figure in view, by the y of their feet, in one upload; the draws
  // below are ranges of it. Close up (past bake_zoom_max), or with B pressed,
  // people are computed live; otherwise from the sprite sheet, except the poses
  // it has no frames for.
  const bool baked = g.use_baked && g.sheet.id != 0 && camera_active(ctx).zoom <= bake_zoom_max;
  g.baked_now = baked;
  const rect seen = camera_bounds(ctx);
  constexpr f32 margin = quad_units * 1.5f; // a raised, tilted quad reaches this far past the feet
  static std::vector<instance> batch;
  batch.clear();
  for (draw_item &it : items) {
    if (it.is_pet)
      continue;
    const vec2 feet = reg.get<transform>(it.e).pos;
    it.shown = feet.x > seen.pos.x - margin && feet.x < seen.pos.x + seen.size.x + margin &&
               feet.y > seen.pos.y - margin && feet.y < seen.pos.y + seen.size.y + margin;
    if (it.shown)
      emit(batch, it.e, reg.get<person>(it.e), feet, quads[it.quad], baked);
  }
  instance_buffer_upload(ctx, g.instances, &batch.data()->cx, static_cast<u32>(batch.size()));
  auto draw_run = [&](u32 first, u32 count) {
    if (g.sheet.id != 0)
      draw_instanced(ctx, g.instances, g.person, first, count, g.sheet);
    else
      draw_instanced(ctx, g.instances, g.person, first, count);
  };

  // The person being followed stands in a faint pencil ellipse on the ground.
  // Drawn as a polyline so it stays round however far the camera zooms in.
  if (reg.valid(g.focus)) {
    const person &p = reg.get<person>(g.focus);
    const vec2 feet = reg.get<transform>(g.focus).pos;
    const vec2 radius = vec2{9.0f, 4.2f} * p.size;
    constexpr i32 steps = 48;
    for (i32 i = 0; i < steps; ++i) {
      const vec2 a = from_angle(360.0f * static_cast<f32>(i) / steps);
      const vec2 b = from_angle(360.0f * static_cast<f32>(i + 1) / steps);
      draw_line(ctx, feet + vec2{a.x * radius.x, a.y * radius.y}, feet + vec2{b.x * radius.x, b.y * radius.y},
                0.45f, rgba{0.35f, 0.42f, 0.62f, 0.6f});
    }
  }

  if (g.call_flash > 0.0f) {
    const f32 k = 1.0f - g.call_flash / 0.8f;
    draw_circle_lines(ctx, g.call_pos, 10.0f + k * 60.0f, 1.5f,
                      rgba{0.35f, 0.42f, 0.62f, (1.0f - k) * 0.6f});
    draw_circle_lines(ctx, g.call_pos, 4.0f + k * 30.0f, 1.0f,
                      rgba{0.35f, 0.42f, 0.62f, (1.0f - k) * 0.5f});
  }

  // Figures go out as one instanced draw per run of people between two pets:
  // a pet is drawn with plain shapes in its place in the y order, so the
  // crowd costs as many draw calls as there are pets, plus one.
  u32 next = 0;
  u32 run = 0;
  for (const draw_item &it : items) {
    if (!it.is_pet) {
      run += it.shown ? 1 : 0;
      continue;
    }
    draw_run(next, run);
    next += run;
    run = 0;
    draw_pet(ctx, reg.get<pet>(it.e), reg.get<topdown_body>(it.e), reg.get<transform>(it.e).pos);
  }
  draw_run(next, run);

  draw_paper_grain(ctx);
}

void draw_hint(njin_ctx &ctx) {
  const rgba soft_ink{0.22f, 0.22f, 0.28f, 0.75f};
  char count[64];
  std::snprintf(count, sizeof count, "%d người · %s", static_cast<i32>(world(ctx).view<person>().size()),
                g.baked_now ? "sprite sheet" : "SDF trực tiếp");
  draw_text(ctx, count, {world_w - 190.0f, world_h - 24.0f}, 13.0f, soft_ink);
  if (world(ctx).valid(g.focus)) {
    char line[96];
    std::snprintf(line, sizeof line, "Đang theo: %s", activity_name(world(ctx).get<person>(g.focus).act));
    draw_text(ctx, line, {18.0f, 16.0f}, 15.0f, soft_ink);
  }
  if (g.toast_timer > 0.0f)
    draw_text(ctx, g.toast.c_str(), {18.0f, 40.0f}, 12.0f, soft_ink);
  if (!g.show_hint)
    return;
  const char *hint =
      world(ctx).valid(g.focus)
          ? "Lăn chuột: phóng to / thu nhỏ   Esc hoặc lăn ra: thôi theo   Click người khác: đổi người"
          : "Click vào người: theo dõi   Lăn chuột lên: phóng vào người gần nhất   Chuột trái chỗ trống: thêm người   "
            "Chuột phải: gọi lại gần   Space: cổ vũ   B: đổi cách vẽ   E: xuất sprite sheet   R: làm lại   H: ẩn";
  draw_text(ctx, hint, {18.0f, world_h - 24.0f}, 13.0f, soft_ink);
}
} // namespace paper_crowd
