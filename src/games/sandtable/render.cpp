#include "render.h"
#include "audio.h"
#include "crowd.h"
#include "person.h"
#include "view.h"
#include "weather.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable {

namespace {

// The world is a model sand table in 3D (view.h) with the city on it
// (world.h, drawn by city/render.*); the HUD is drawn over it in screen
// pixels. The gangs and their men are not on it yet (CONCEPT.md): only the
// townsfolk (crowd.h), and the pose sheet for the test run.

font_handle ui_font{};
constexpr f32 font_size = 16.0f;

// Instance data (the frame, built once). The engine's
// layout (draw_instanced3d): position and scale, colour, rotation in degrees,
// scale by axis.
struct batch {
  instance_buffer_handle buffer{};
  std::vector<f32> data;
  u32 floats = 16;
  u32 count() const { return static_cast<u32>(data.size() / floats); }
};

batch frame{}; // the wooden frame round the sand
void push(batch &b, vec3 pos, vec3 scale, rgba col, vec3 rot = {}) {
  b.data.insert(b.data.end(), {pos.x, pos.y, pos.z, 1.0f, col.r, col.g, col.b, col.a, rot.x, rot.y, rot.z, 0.0f,
                               scale.x, scale.y, scale.z, 0.0f});
}

void upload(context &ctx, batch &b) {
  if (b.buffer.id == 0)
    b.buffer = instance_buffer_create(ctx, b.floats);
  instance_buffer_upload(ctx, b.buffer, b.data.data(), b.count());
}

void draw(context &ctx, mesh3d_kind mesh, const batch &b) {
  if (b.buffer.id != 0 && b.count() > 0)
    draw_instanced3d(ctx, mesh, b.buffer, 0, b.count());
}

// --- The table ---

constexpr f32 table_bottom = -1.2f; // the underside of the sand

// The wooden frame round the sand, its top edge lighter: built once.
void build_frame(context &ctx) {
  frame.data.clear();
  const f32 w = world_width * unit3d, h = world_height * unit3d;
  const f32 rim = 0.8f, frame_top = 0.25f, frame_h = frame_top - table_bottom - 0.3f;
  const f32 fy = frame_top - frame_h * 0.5f;
  push(frame, {w * 0.5f, fy, -rim * 0.5f}, {w + rim * 2.0f, frame_h, rim}, col_frame);
  push(frame, {w * 0.5f, fy, h + rim * 0.5f}, {w + rim * 2.0f, frame_h, rim}, col_frame);
  push(frame, {-rim * 0.5f, fy, h * 0.5f}, {rim, frame_h, h}, col_frame);
  push(frame, {w + rim * 0.5f, fy, h * 0.5f}, {rim, frame_h, h}, col_frame);
  for (i32 i = 0; i < 4; ++i) {
    const bool along = i < 2;
    const vec3 c = along ? vec3{w * 0.5f, frame_top + 0.02f, i == 0 ? -rim * 0.5f : h + rim * 0.5f}
                         : vec3{i == 2 ? -rim * 0.5f : w + rim * 0.5f, frame_top + 0.02f, h * 0.5f};
    push(frame, c, along ? vec3{w + rim * 2.0f, 0.04f, rim} : vec3{rim, 0.04f, h}, col_frame_light);
  }
  upload(ctx, frame);
}

// The sand: one flat slab from the underside up to height 0, over the dark
// wooden table everything stands on.
void draw_table(context &ctx) {
  const f32 w = world_width * unit3d, h = world_height * unit3d;
  material3d_set(ctx, {.specular = 0.1f, .cast_shadows = false});
  draw_plane3d(ctx, {w * 0.5f, table_bottom - 0.02f, h * 0.5f}, {400.0f, 400.0f}, rgb(52, 36, 26));
  material3d_set(ctx, {.specular = 0.06f, .shininess = 10.0f});
  draw_cube3d(ctx, {w * 0.5f, table_bottom * 0.5f, h * 0.5f}, {w, -table_bottom, h}, col_sand);
  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  draw(ctx, mesh3d_cube, frame);
  material3d_set(ctx, {});
}

// --- The men ---

bool pose_row = false;
vec2 pose_row_at{};

// The pose sheet (show_pose_row): every motion a column, moments from its
// start to its end down the rows, so one picture shows how each moves.
void draw_pose_row(context &ctx) {
  constexpr i32 moments = 4;
  constexpr i32 acts = static_cast<i32>(act::count);
  material3d_set(ctx, {.specular = 0.15f, .shininess = 16.0f});
  for (i32 a = 0; a < acts; ++a)
    for (i32 k = 0; k < moments; ++k) {
      const act which = static_cast<act>(a);
      const f32 t = act_duration(which) * static_cast<f32>(k) / static_cast<f32>(moments);
      const vec2 at = pose_row_at + vec2{(static_cast<f32>(a) - (acts - 1) * 0.5f) * 12.0f,
                                         (static_cast<f32>(k) - (moments - 1) * 0.5f) * 16.0f};
      draw_person(ctx, {.at = at, .facing = 60.0f, .now = which, .time = t,
                        .tint = a % 2 == 0 ? rgb(214, 96, 80) : rgb(96, 140, 190)});
    }
  material3d_set(ctx, {});
}

// The men on the table: the townsfolk, and the pose sheet for the test run.
void draw_men(context &ctx) {
  crowd_draw(ctx);
  if (pose_row)
    draw_pose_row(ctx);
}

// --- Light ---

// The sun follows the hour across the sky; at night the moon stands in, low
// and blue.
void set_light(context &ctx) {
  const vec3 sky = daylight();
  const f32 h = state.hour;
  const bool day = h > 5.5f && h < 19.0f;
  const f32 a = day ? (h - 5.5f) / 13.5f * pi : 0.8f;
  const vec3 dir{-std::cos(a) * 0.8f, -std::max(0.35f, std::sin(a)), -0.45f};
  const f32 dark = darkness();
  light3d_set(ctx, {.direction = dir,
                    .color = {sky.x * sky.x * 0.95f, sky.y * sky.y * 0.9f, sky.z * sky.z * 0.85f, 1.0f},
                    // Dim at night, so the torches and fires carry the scene.
                    .ambient = {0.06f + sky.x * 0.34f, 0.06f + sky.y * 0.34f, 0.1f + sky.z * 0.36f, 1.0f},
                    .shadows = true,
                    .shadow_range = clamp(state.cam_distance * 0.75f, 12.0f, 48.0f),
                    .shadow_size = 2048,
                    .shadow_softness = 1.5f,
                    .fog_color = {0.06f + 0.1f * (1.0f - dark), 0.06f + 0.1f * (1.0f - dark),
                                  0.08f + 0.12f * (1.0f - dark), 1.0f},
                    .fog_density = 0.006f});
}

// --- Over the picture: text and marks in screen pixels ---

void text(context &ctx, const char *str, vec2 pos, rgba col, f32 size = font_size) {
  draw_text(ctx, str, {std::floor(pos.x), std::floor(pos.y)}, size, col, ui_font);
}
// Text with a one-pixel dark shadow, for text over the table.
void text_shadow(context &ctx, const char *str, vec2 pos, rgba col, f32 size = font_size) {
  text(ctx, str, pos + vec2{1.0f, 1.0f}, rgb(20, 14, 10, static_cast<i32>(200 * col.a)), size);
  text(ctx, str, pos, col, size);
}

// --- Field ledger: paper, ink and a single vermilion command ---

constexpr rgba col_panel = rgb(26, 31, 29, 235);
constexpr rgba col_panel_edge = rgb(78, 85, 73);
constexpr rgba col_button = rgb(42, 48, 43);
constexpr rgba col_button_hover = rgb(66, 75, 62);
constexpr rgba col_button_down = rgb(91, 103, 83);
constexpr rgba col_button_off = rgb(31, 36, 32);
constexpr rgba col_text_hint = rgb(164, 172, 151);
constexpr rgba col_paper = rgb(218, 210, 184);
constexpr rgba col_seal = rgb(148, 54, 41);
ui_style hud_style{};

ui_skin flat(rgba color, rgba edge = rgb(20, 14, 10)) {
  return ui_skin{.color = color, .roundness = 0.0f, .outline = edge, .outline_width = 1.0f};
}

ui_style make_hud_style() {
  ui_style s = ui_default_style();
  s.font = ui_font;
  s.font_size = font_size;
  s.padding = 8.0f;
  s.spacing = 2.0f;
  s.widget_height = 20.0f;
  s.width = 200.0f;
  s.toast_width = 240.0f;
  s.toast_margin = {8.0f, 72.0f};
  s.toast.normal = flat(col_panel, col_panel_edge);
  s.toast.text = col_paper;
  s.toast_accent[0] = col_text_hint;
  s.toast_accent[1] = rgb(111, 139, 94);
  s.toast_accent[2] = rgb(188, 153, 87);
  s.toast_accent[3] = col_seal;
  s.dim = rgb(0, 0, 0, 150);

  s.panel.normal = flat(col_panel, col_panel_edge);
  s.panel.text = col_paper;
  s.label.text = s.label.text_focused = col_white;

  s.button.normal = flat(col_button);
  s.button.focused = flat(col_button_hover, col_panel_edge);
  s.button.pressed = flat(col_button_down, col_paper);
  s.button.disabled = flat(col_button_off, rgb(30, 24, 18));
  s.button.text = col_white;
  s.button.text_focused = col_paper;
  s.button.text_disabled = rgb(110, 100, 86);

  const ui_skin slot = flat(rgb(20, 16, 12));
  s.track.normal = s.track.focused = s.track.pressed = s.track.disabled = slot;
  s.track.text = s.track.text_focused = col_white;
  const ui_skin fill{.color = col_good};
  s.fill.normal = s.fill.focused = s.fill.pressed = s.fill.disabled = fill;
  for (ui_look *look : {&s.panel, &s.button, &s.track, &s.fill, &s.knob, &s.toast})
    for (ui_skin *skin : {&look->normal, &look->focused, &look->pressed, &look->disabled})
      skin->roundness = 0.0f;

  s.sound_move = {};
  s.sound_accept = audio_sound(sfx_type::click);
  return s;
}

void hud_label(context &ctx, const char *str, rgba color) {
  ui_style s = ui_style_get(ctx);
  const rgba old = s.label.text;
  s.label.text = color;
  ui_style_set(ctx, s);
  ui_label(ctx, str);
  s.label.text = old;
  ui_style_set(ctx, s);
}

void top_bar(context &ctx) {
  const vec2 scr = screen_size(ctx);
  ui_begin(ctx, {.id = "top_bar", .anchor = {0.0f, 0.0f}, .pivot = {0.0f, 0.0f},
                 .width = scr.x, .navigable = false});
  char title[96];
  std::snprintf(title, sizeof(title), "Sa Bàn Chiến Trận   ·   %02d:00", static_cast<i32>(state.hour));
  hud_label(ctx, title, col_paper);
  ui_end(ctx);
}

// What the generator made, and whether it passed its checks.
void city_panel(context &ctx) {
  const city::city_map &m = world();
  char line[200];
  std::snprintf(line, sizeof(line), "Thành phố #%u  ·  %d quận  ·  %d khối  ·  %d nhà  ·  %d cơ sở  ·  %d chỗ  ·  %.0f ms",
                m.desc.seed, static_cast<i32>(m.districts.size()), static_cast<i32>(m.blocks.size()),
                static_cast<i32>(m.buildings.size()), static_cast<i32>(m.businesses.size()),
                static_cast<i32>(m.spots.size()), static_cast<f64>(m.report.gen_ms));
  text_shadow(ctx, line, {8.0f, 28.0f}, col_paper, 12.0f);
  const city::view_stats &vs = city::view_last_stats();
  char cull_line[128];
  std::snprintf(cull_line, sizeof(cull_line), "Vẽ %d/%d ô  ·  chi tiết %d ô  ·  %u khối", vs.visible, vs.chunks,
                vs.detailed, vs.instances);
  text_shadow(ctx, cull_line, {8.0f, 70.0f}, col_text_hint, 11.0f);
  const city::view_options &v = world_view();
  if (v.selected >= 0 || (v.around && !v.cut.empty())) {
    const i32 floors = v.selected >= 0 ? m.buildings[static_cast<size_t>(v.selected)].floors : 0;
    char floor_line[96];
    if (floors > 0)
      std::snprintf(floor_line, sizeof(floor_line), "Đang xem tầng %d/%d  (PgUp PgDn)", v.floor + 1, floors);
    else
      std::snprintf(floor_line, sizeof(floor_line), "Đang xem tầng %d  (PgUp PgDn)", v.floor + 1);
    text_shadow(ctx, floor_line, {8.0f, 56.0f}, col_gold, 12.0f);
  }
  if (m.report.ok())
    text_shadow(ctx, "Kiểm tra: đạt", {8.0f, 42.0f}, col_good, 12.0f);
  else
    for (size_t i = 0; i < m.report.errors.size() && i < 4; ++i)
      text_shadow(ctx, m.report.errors[i].c_str(), {8.0f, 42.0f + static_cast<f32>(i) * 13.0f}, col_bad, 12.0f);
}

} // namespace

void show_pose_row(bool on, vec2 at) {
  pose_row = on;
  pose_row_at = at;
}

void render_init(context &ctx) {
  ui_font = font_load(ctx, "assets/fonts/BeVietnamPro-Bold.ttf", 32);
  hud_style = make_hud_style();
  build_frame(ctx);
  post_fx_set(ctx, {.saturation = 1.08f, .vignette = 0.35f, .bloom = 0.55f, .bloom_threshold = 0.82f});
}

void render_cleanup(context &ctx) {
  city::view_cleanup(ctx);
  city::view_shutdown(ctx);
  person_cleanup(ctx);
  if (ui_font.id != 0) {
    font_unload(ctx, ui_font);
    ui_font = {};
  }
  for (batch *b : {&frame})
    if (b->buffer.id != 0) {
      instance_buffer_destroy(ctx, b->buffer);
      b->buffer = {};
    }
}

void render_world(context &ctx) {
  set_light(ctx);
  begin_3d(ctx, table_camera());
  draw_table(ctx);
  world_view().night = darkness();
  world_update_view();
  // In focus, the edges of the picture close in a little more, and what is
  // nearer or farther than the thing in focus goes out of focus, as through
  // a lens.
  post_fx fx{.saturation = 1.08f, .vignette = 0.35f, .bloom = 0.55f, .bloom_threshold = 0.82f};
  const city::view_options &v = world_view();
  if (v.focused) {
    const camera3d cam = table_camera();
    const vec3 look = normalize(cam.target - cam.position);
    fx.vignette = 0.62f;
    fx.dof = 7.0f;
    fx.dof_focus = dot(to3d(v.focus) - cam.position, look);
    fx.dof_range = v.focus_radius * unit3d * 0.35f;
    fx.dof_falloff = v.focus_radius * unit3d * 0.6f;
  }
  post_fx_set(ctx, fx);
  city::view_draw(ctx, world(), world_view());
  draw_men(ctx);
  end_3d(ctx);
}

void render_ui(context &ctx) {
  ui_style_set(ctx, hud_style);
  top_bar(ctx);
  city::view_draw_ui(ctx, world(), world_view(), ui_font);
  city_panel(ctx);
  const f32 bottom = screen_size(ctx).y;
  text_shadow(ctx, "WASD: di chuyển  /  Q E: xoay  /  Lăn chuột: gần xa", {8.0f, bottom - 40.0f}, col_paper, 12.0f);
  char keys[200];
  std::snprintf(keys, sizeof(keys), "N B R: seed  /  F1 lớp phủ (%s)  /  F2 nhãn  /  F3 đồ thị  /  F4 ghim  /  Nhấp: mở nhà  /  C: mở quanh",
                city::overlay_name(world_view().layer));
  text_shadow(ctx, keys, {8.0f, bottom - 24.0f}, col_text_hint, 12.0f);
}

} // namespace sandtable
