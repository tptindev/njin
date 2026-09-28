#include "crowd.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// How thousands of different, animated people stay cheap:
//   bake      the SDF figure is drawn once, at startup, into render textures:
//             a body cell per (build, pose, base direction, frame) and a head
//             cell per (hair style, direction). Cells hold colour slot weights,
//             not colours, so the sheets serve every DNA (sheet.cpp).
//   mirror    W, NW, SW are E, NE, SE flipped, so only 5 directions are baked.
//   DNA       48 bits in two floats. The vertex shader decodes it into colours,
//             a body block and a hair style; the CPU never builds colours.
//   draw      visible people are culled, sorted by feet y and uploaded as 8
//             floats each, then drawn with one instanced draw call.
//   simulate  one entity per person and per group, a grid for neighbours (sim.cpp).

namespace crowd {
namespace {
using namespace njin;

constexpr u32 max_people = 100000;

struct game_state {
  bool gpu_ok = true;
  bool start_in_gallery = false;
  bool save_sheets = false; // save the sheets as soon as they are baked
  bool in_gallery = false;
  bool frozen = false;
  u32 population = 5000;
  entt::entity selected = entt::null;
  std::vector<instance> visible;
  entt::entity camera = entt::null;
  f32 frame_ms = 16.0f;
  std::string status;
};
game_state g;

// Moves the camera to the gallery or to the middle of the crowd.
void place_camera(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.camera))
    return;
  camera_2d &cam = reg.get<camera_2d>(g.camera);
  transform &tr = reg.get<transform>(g.camera);
  if (g.in_gallery) {
    tr.pos = gallery_origin + vec2{260.0f, 440.0f};
    cam.zoom = 0.8f;
  } else {
    tr.pos = world_size * 0.5f;
    cam.zoom = 0.6f;
  }
}

void populate(njin_ctx &ctx, u32 count) {
  sim_populate(ctx, count);
  g.population = count;
  g.selected = entt::null;
}

void startup(njin_ctx &ctx) {
  g.gpu_ok = sheet_load(ctx);
  sim_debug_components(ctx);
  g.camera = camera_spawn(ctx, 0.6f, world_size * 0.5f);
  g.in_gallery = g.start_in_gallery;
  place_camera(ctx);
  populate(ctx, g.population);
}

void camera_input(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.camera))
    return;
  camera_2d &cam = reg.get<camera_2d>(g.camera);
  transform &tr = reg.get<transform>(g.camera);
  const f32 dt = delta_real(ctx);
  cam.offset = screen_size(ctx) * 0.5f;

  vec2 pan{};
  if (key_held(ctx, key_a) || key_held(ctx, key_left)) pan.x -= 1.0f;
  if (key_held(ctx, key_d) || key_held(ctx, key_right)) pan.x += 1.0f;
  if (key_held(ctx, key_w) || key_held(ctx, key_up)) pan.y -= 1.0f;
  if (key_held(ctx, key_s) || key_held(ctx, key_down)) pan.y += 1.0f;
  tr.pos += pan * (900.0f / cam.zoom) * dt;
  if (mouse_held(ctx, mouse_right))
    tr.pos -= mouse_delta(ctx) / cam.zoom;

  // Zoom about the point under the mouse, no closer than the baked resolution allows.
  if (const f32 wheel = mouse_wheel(ctx); wheel != 0.0f) {
    const vec2 m = mouse_pos(ctx);
    const vec2 before = tr.pos + (m - cam.offset) / cam.zoom;
    cam.zoom = clamp(cam.zoom * std::pow(1.15f, wheel), 0.12f, 3.0f);
    tr.pos = before - (m - cam.offset) / cam.zoom;
  }
  tr.pos = clamp(tr.pos, {gallery_origin.x, -100.0f}, world_size);

  if (mouse_pressed(ctx, mouse_left)) {
    const vec2 at = tr.pos + (mouse_pos(ctx) - cam.offset) / cam.zoom;
    f32 best = 18.0f * 18.0f;
    g.selected = entt::null;
    for (auto [e, t, p] : reg.view<const transform, const person>().each()) {
      const f32 d = length_sq(t.pos - vec2{0, 14} - at);
      if (d < best) {
        best = d;
        g.selected = e;
      }
    }
  }
}

void game_input(njin_ctx &ctx) {
  if (key_pressed(ctx, key_1)) populate(ctx, 1000);
  if (key_pressed(ctx, key_2)) populate(ctx, 5000);
  if (key_pressed(ctx, key_3)) populate(ctx, 20000);
  if (key_pressed(ctx, key_4)) populate(ctx, 50000);
  if (key_pressed(ctx, key_r)) populate(ctx, g.population);
  if (key_pressed(ctx, key_f) && sim_people(world(ctx)) + 24 <= max_people)
    sim_spawn_family(ctx, g.selected);
  if (key_pressed(ctx, key_space)) g.frozen = !g.frozen;
  if (key_pressed(ctx, key_g)) {
    g.in_gallery = !g.in_gallery;
    place_camera(ctx);
  }
  if (key_pressed(ctx, key_left_bracket)) sim.max_chain = std::max(2u, sim.max_chain - 1);
  if (key_pressed(ctx, key_right_bracket)) sim.max_chain = std::min(16u, sim.max_chain + 1);
  if (key_pressed(ctx, key_f12)) screenshot(ctx);
  if (key_pressed(ctx, key_f9)) {
    // The raw sheets: red, green, blue are colour slot weights.
    std::string where;
    g.status = sheet_save(ctx, where) ? "Đã lưu " + where : "Không lưu được sprite sheet";
  }
}

void simulate(njin_ctx &ctx) {
  if (!g.frozen)
    sim_update(ctx, delta(ctx));
}

void bake(njin_ctx &ctx) {
  if (!g.gpu_ok || sheet_ready())
    return;
  sheet_bake(ctx);
  std::string where;
  if (g.save_sheets)
    g.status = sheet_save(ctx, where) ? "Đã lưu " + where : "Không lưu được sprite sheet";
}

// Drawing is three systems so the inspector times each step.
void draw_background(njin_ctx &ctx) {
  draw_rect_lines(ctx, rect{{-8, -8}, world_size + vec2{16, 16}}, 4.0f, rgba{0.6f, 0.6f, 0.58f, 1.0f});
  for (const label &l : gallery_labels())
    draw_text(ctx, l.text.c_str(), l.pos, 12.0f, rgba{0.3f, 0.28f, 0.3f, 1.0f});
}

// Cull to the camera, with room for a lying or jumping figure at the edge.
void crowd_cull(njin_ctx &ctx) {
  g.visible.clear();
  if (!sheet_ready())
    return;
  const rect view = camera_bounds(ctx);
  const f32 m = 40.0f;
  const f32 x0 = view.pos.x - m, x1 = view.pos.x + view.size.x + m;
  const f32 y0 = view.pos.y - m, y1 = view.pos.y + view.size.y + m * 1.5f;
  const entt::registry &reg = world(ctx);
  for (auto [e, t, p] : reg.view<const transform, const person>().each()) {
    if (t.pos.x < x0 || t.pos.x > x1 || t.pos.y < y0 || t.pos.y > y1)
      continue;
    g.visible.emplace_back();
    sim_instance(reg, e, e == g.selected, g.visible.back());
  }
}

// Whoever stands lower on screen is in front.
void crowd_sort(njin_ctx &) {
  std::sort(g.visible.begin(), g.visible.end(), [](const instance &a, const instance &b) { return a.y < b.y; });
}

void crowd_draw(njin_ctx &ctx) { sheet_draw(ctx, g.visible); }

// Totals for the inspector's Watches panel; each person and group is also an
// entity in its Entities panel. Skipped when no inspector is connected.
void debug_watches(njin_ctx &ctx) {
  if (!debug_server_connected(ctx))
    return;
  const entt::registry &reg = world(ctx);
  debug_watch(ctx, "crowd.people", sim_people(reg));
  debug_watch(ctx, "crowd.gallery", (u32)reg.view<const gallery_pin>().size());
  debug_watch(ctx, "crowd.visible (instances)", (u32)g.visible.size());
  debug_watch(ctx, "crowd.upload KB/frame", (f32)(g.visible.size() * sizeof(instance)) / 1024.0f);
  debug_watch(ctx, "crowd.frozen", g.frozen);
  debug_watch(ctx, "crowd.max_chain", sim.max_chain);

  // How many people do what.
  u32 acts[act_pinned + 1] = {};
  for (auto [e, p] : reg.view<const person>(entt::exclude<gallery_pin>).each())
    acts[p.act]++;
  json_value activities = json_value::make_object();
  for (u32 a = 0; a < act_pinned; a++)
    activities.set(activity_name((u8)a), acts[a]);
  debug_watch(ctx, "crowd.activities", activities);

  // Groups: how many gather or act, and the chain lengths.
  u32 gathering = 0, acting[group_kinds] = {}, chain_people = 0, longest = 0;
  json_value lengths = json_value::make_object();
  u32 by_len[17] = {};
  for (auto [e, gr] : reg.view<const group>().each()) {
    if (!gr.acting) {
      gathering++;
      continue;
    }
    acting[gr.kind]++;
    if (gr.kind == grp_chain) {
      const u32 n = (u32)gr.members.size();
      chain_people += n;
      longest = std::max(longest, n);
      by_len[std::min(n, 16u)]++;
    }
  }
  for (u32 n = 2; n <= 16; n++)
    if (by_len[n] > 0)
      lengths.set(std::to_string(n) + " người", by_len[n]);
  debug_watch(ctx, "groups.gathering", gathering);
  debug_watch(ctx, "groups.greet", acting[grp_greet]);
  debug_watch(ctx, "groups.shake", acting[grp_shake]);
  debug_watch(ctx, "groups.chain", acting[grp_chain]);
  debug_watch(ctx, "groups.spar", acting[grp_spar]);
  debug_watch(ctx, "groups.chain people", chain_people);
  debug_watch(ctx, "groups.chain longest", longest);
  debug_watch(ctx, "groups.chain lengths", lengths);

  const sheet_stats st = sheet_get_stats();
  debug_watch(ctx, "sheet.body cells", st.body_cells);
  debug_watch(ctx, "sheet.body size", std::to_string(st.body_w) + " x " + std::to_string(st.body_h) + " x2");
  debug_watch(ctx, "sheet.head size", std::to_string(st.head_w) + " x " + std::to_string(st.head_h));
  debug_watch(ctx, "sheet.VRAM MB", (f32)st.bytes / (1024.0f * 1024.0f));

  // The person clicked in the game: find this id in the Entities panel.
  debug_watch(ctx, "selected entity", reg.valid(g.selected) ? json_value((u32)entt::to_integral(g.selected))
                                                            : json_value("(click a person)"));
}

void draw_hud(njin_ctx &ctx) {
  g.frame_ms = lerp(g.frame_ms, delta_real(ctx) * 1000.0f, 0.05f);
  const rgba ink{0.15f, 0.13f, 0.14f, 1.0f}, soft{0.15f, 0.13f, 0.14f, 0.7f};
  if (!g.gpu_ok) {
    draw_text(ctx, "Máy này không vẽ instanced được (cần OpenGL 3.3).", {16, 16}, 22.0f, ink);
    return;
  }
  const entt::registry &reg = world(ctx);
  const u32 crowd = sim_people(reg);
  const std::string line1 = "Người: " + std::to_string(crowd) + "   đang vẽ: " + std::to_string(g.visible.size()) +
                            " (1 lệnh vẽ)   " + std::to_string((i32)std::lround(1000.0f / g.frame_ms)) + " FPS";
  const std::string line2 = "Nhóm: chào " + std::to_string(sim_acting_groups(reg, grp_greet)) + "  bắt tay " +
                            std::to_string(sim_acting_groups(reg, grp_shake)) + "  nắm tay " +
                            std::to_string(sim_acting_groups(reg, grp_chain)) + "  đấu võ " +
                            std::to_string(sim_acting_groups(reg, grp_spar)) + "  (tối đa " +
                            std::to_string(sim.max_chain) + " người/chuỗi)";
  draw_rect(ctx, rect{{8, 8}, {700, 98}}, rgba{1, 1, 1, 0.75f});
  draw_text(ctx, line1.c_str(), {16, 14}, 20.0f, ink);
  draw_text(ctx, line2.c_str(), {16, 38}, 15.0f, ink);
  draw_text(ctx, "WASD/chuột phải: di chuyển  Lăn: zoom  Click: chọn  G: phòng trưng bày  F: sinh 24 con\n"
                 "1/2/3/4: 1k/5k/20k/50k người  R: DNA mới  [ ]: độ dài chuỗi  Space: dừng  F9: lưu sheet",
            {16, 60}, 14.0f, soft);

  if (reg.valid(g.selected)) {
    const person &p = reg.get<person>(g.selected);
    const dna &genes = reg.get<dna>(g.selected);
    std::string text = "entity " + std::to_string(entt::to_integral(g.selected)) + "  DNA " + genes.hex() + "\n" +
                       activity_name(p.act) + "\n";
    for (u32 i = 0; i < gene_count; i++) {
      const gene_info &info = gene_desc((gene)i);
      text += std::string(info.name) + ": " + std::to_string(genes.get((gene)i)) + "/" +
              std::to_string(info.count - 1) + "\n";
    }
    draw_rect(ctx, rect{{8, 114}, {220, 224}}, rgba{1, 1, 1, 0.8f});
    draw_text(ctx, text.c_str(), {16, 120}, 16.0f, ink);
  }
  if (!g.status.empty())
    draw_text(ctx, g.status.c_str(), {16, screen_size(ctx).y - 30}, 16.0f, soft);
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_pre_update, camera_input, "camera_input");
  ecs_register(ctx, phase_pre_update, game_input, "game_input");
  ecs_register(ctx, phase_update, simulate, "simulate");
  ecs_register(ctx, phase_post_update, bake, "bake");
  ecs_register(ctx, phase_render, draw_background, "draw_background");
  ecs_register(ctx, phase_render, crowd_cull, "crowd_cull");
  ecs_register(ctx, phase_render, crowd_sort, "crowd_sort");
  ecs_register(ctx, phase_render, crowd_draw, "crowd_draw");
  ecs_register(ctx, phase_post_render, draw_hud, "draw_hud");
  ecs_register(ctx, phase_post_render, debug_watches, "debug_watches");
}
} // namespace

njin::mod_desc crowd_module(const options &opts) {
  g.start_in_gallery = opts.gallery;
  g.save_sheets = opts.save_sheets;
  g.population = std::min(opts.people, max_people);
  return {.name = "crowd", .setup = setup};
}
} // namespace crowd
