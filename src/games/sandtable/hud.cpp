#include "hud.h"
#include "audio.h"
#include "gang.h"
#include "view.h"
#include "weather.h"
#include "world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sandtable {

namespace {

// --- Look ---------------------------------------------------------------------------
// Dark glass over the table: rounded, a little see-through, one warm accent.
// Sizes are in pixels of a 720-line window; `k` (view.h ui_scale) scales them
// to the window: the styles through ui_style::scale, the rest by hand.

constexpr rgba ink = rgb(236, 230, 214);
constexpr rgba ink_dim = rgb(160, 164, 156);
constexpr rgba accent = rgb(232, 176, 72);
constexpr rgba gang_red = rgb(214, 70, 58);
constexpr rgba glass = rgb(18, 21, 22, 214);
constexpr rgba glass_edge = rgb(255, 255, 255, 22);

font_handle font{};
f32 k = 1.0f; // ui_scale this frame
ui_style pill_style{}, dock_style{}, card_style{}, popup_style{};

ui_skin skin(rgba color, f32 round, rgba edge = glass_edge) {
  return ui_skin{.color = color, .roundness = round, .outline = edge, .outline_width = 1.0f};
}

ui_style base_style() {
  ui_style s = ui_default_style();
  s.font = font;
  s.font_size = 17.0f;
  s.padding = 10.0f;
  s.spacing = 6.0f;
  s.widget_height = 32.0f;
  s.width = 360.0f;
  s.panel.normal = skin(glass, 0.35f);
  s.panel.text = ink;
  s.label.text = s.label.text_focused = ink;
  s.button.normal = skin(rgb(255, 255, 255, 14), 0.4f, rgb(0, 0, 0, 0));
  s.button.focused = skin(rgb(255, 255, 255, 34), 0.4f, rgb(255, 255, 255, 40));
  s.button.pressed = skin(rgb(255, 255, 255, 56), 0.4f, accent);
  s.button.disabled = skin(rgb(255, 255, 255, 6), 0.4f, rgb(0, 0, 0, 0));
  s.button.text = ink;
  s.button.text_focused = rgb(255, 250, 238);
  s.button.text_disabled = rgb(110, 112, 108);
  s.track.normal = s.track.focused = s.track.pressed = s.track.disabled = skin(rgb(0, 0, 0, 90), 0.5f);
  s.fill.normal = s.fill.focused = s.fill.pressed = s.fill.disabled = skin(accent, 0.5f, rgb(0, 0, 0, 0));
  s.toast.normal = skin(glass, 0.3f);
  s.toast.text = ink;
  s.toast_anchor = {1.0f, 0.0f};
  // Under the clock, top right.
  s.toast_margin = {16.0f, 116.0f};
  s.toast_width = 360.0f;
  s.toast_accent[0] = ink_dim;
  s.toast_accent[1] = rgb(96, 190, 120);
  s.toast_accent[2] = accent;
  s.toast_accent[3] = gang_red;
  s.dim = rgb(0, 0, 0, 110);
  s.sound_move = {};
  s.sound_accept = audio_sound(sfx_type::click);
  return s;
}

void make_styles() {
  pill_style = base_style();
  pill_style.widget_height = 36.0f;
  pill_style.padding = 8.0f;
  dock_style = base_style();
  dock_style.widget_height = 52.0f;
  dock_style.padding = 8.0f;
  dock_style.spacing = 8.0f;
  card_style = base_style();
  card_style.width = 336.0f;
  card_style.widget_height = 32.0f;
  card_style.padding = 14.0f;
  card_style.spacing = 6.0f;
  popup_style = base_style();
  popup_style.font_size = 18.0f;
  popup_style.padding = 20.0f;
  popup_style.spacing = 8.0f;
  popup_style.widget_height = 36.0f;
  popup_style.width = 680.0f;
}

// Sets `st`, scaled to the window.
void use(context &ctx, ui_style st) {
  st.scale = k;
  ui_style_set(ctx, st);
}

// A button standing for the current choice (the speed, the popup open).
ui_style with_active(ui_style s) {
  s.button.normal = s.button.focused = skin(rgb(232, 176, 72, 70), 0.4f, accent);
  return s;
}

// --- Icons ------------------------------------------------------------------------------

enum icon { i_people, i_flag, i_coins, i_bug, i_pause, i_play, i_fast, i_up, i_down, i_close, i_house, icon_count };
texture_handle icons[icon_count];

// Icons and the tip under the mouse go on top of a panel's widgets, which the
// engine draws only at ui_end (its background first): they wait here, and
// drawn() puts them on after each panel.
struct pending_icon {
  icon i;
  vec2 centre;
  f32 size;
  rgba tint;
};
std::vector<pending_icon> waiting;
std::string tip_text;
rect tip_over{};

void draw_icon(context &, icon i, vec2 centre, f32 size, rgba tint = ink) { waiting.push_back({i, centre, size, tint}); }

void drawn(context &ctx) {
  for (const pending_icon &p : waiting)
    if (icons[p.i].id != 0)
      texture_draw_ex(ctx, icons[p.i],
                      {.pos = p.centre, .scale = {p.size / 64.0f, p.size / 64.0f}, .origin = {0.5f, 0.5f}, .tint = p.tint});
  waiting.clear();
}

// The name of the icon button under the mouse, over it: drawn last of all.
void draw_tip(context &ctx) {
  if (tip_text.empty())
    return;
  const f32 fs = 15.0f * k;
  const vec2 w = text_measure(ctx, tip_text.c_str(), fs, font);
  const rect r = tip_over;
  const vec2 at{std::floor(r.pos.x + r.size.x * 0.5f - w.x * 0.5f), std::floor(r.pos.y - w.y - 16.0f * k)};
  draw_rect(ctx, {at - vec2{8.0f, 4.0f} * k, w + vec2{16.0f, 8.0f} * k}, glass);
  draw_text(ctx, tip_text.c_str(), at, fs, ink, font);
  tip_text.clear();
}

// A button with an icon in the middle, and its name shown over it on hover.
bool icon_button(context &ctx, const char *id, icon i, const char *tip, bool enabled = true, rgba tint = ink) {
  const bool pressed = ui_button(ctx, id, enabled);
  const rect r = ui_last_rect(ctx);
  const f32 size = std::min(r.size.x, r.size.y) * 0.56f;
  draw_icon(ctx, i, r.pos + r.size * 0.5f, size, enabled ? tint : rgb(110, 112, 108));
  if (tip != nullptr && point_in_rect(mouse_pos(ctx), r)) {
    tip_text = tip;
    tip_over = r;
  }
  return pressed;
}

// A label with an icon before it (the text starts after the icon).
void icon_label(context &ctx, icon i, const char *text, rgba tint) {
  char line[128];
  std::snprintf(line, sizeof(line), "          %s", text);
  ui_label(ctx, line);
  const rect r = ui_last_rect(ctx);
  draw_icon(ctx, i, {r.pos.x + 12.0f * k, r.pos.y + r.size.y * 0.5f}, 22.0f * k, tint);
}

void label(context &ctx, const char *text, rgba col) {
  ui_style s = ui_style_get(ctx);
  const rgba old = s.label.text;
  s.label.text = col;
  ui_style_set(ctx, s);
  ui_label(ctx, text);
  s.label.text = old;
  ui_style_set(ctx, s);
}

// Money in thousands of đồng: "1.500k".
std::string money(i32 k) {
  const bool neg = k < 0;
  std::string digits = std::to_string(neg ? -k : k);
  for (i32 at = static_cast<i32>(digits.size()) - 3; at > 0; at -= 3)
    digits.insert(static_cast<size_t>(at), ".");
  return (neg ? "-" : "") + digits + "k";
}

// --- What is open --------------------------------------------------------------------

enum class popup { none, men, turf, books, debug };
popup open_popup = popup::none;

void open(popup p) { open_popup = open_popup == p ? popup::none : p; }

// --- The pieces ----------------------------------------------------------------------

void resources(context &ctx) {
  use(ctx, pill_style);
  ui_begin(ctx, {.id = "res", .anchor = {0.0f, 0.0f}, .pivot = {0.0f, 0.0f}, .offset = vec2{16.0f, 16.0f} * k,
                 .width = 330.0f, .navigable = false});
  ui_row(ctx, 3);
  const gang_state &g = gang();
  icon_label(ctx, i_coins, money(g.money).c_str(), g.money < 0 ? gang_red : accent);
  char men[32], turf[32];
  std::snprintf(men, sizeof(men), "%d", static_cast<i32>(g.men.size()));
  icon_label(ctx, i_people, men, ink);
  std::snprintf(turf, sizeof(turf), "%d khối", g.turf);
  icon_label(ctx, i_flag, turf, gang_red);
  ui_end(ctx);
  drawn(ctx);
}

// Top right, level with the resources on the left: the day and hour over
// the three speeds, each centred in the pill.
void clock(context &ctx) {
  use(ctx, pill_style);
  ui_begin(ctx, {.id = "clock", .anchor = {1.0f, 0.0f}, .pivot = {1.0f, 0.0f}, .offset = vec2{-16.0f, 16.0f} * k,
                 .width = 200.0f, .navigable = false});
  ui_space(ctx, 24.0f);
  const rect line = ui_last_rect(ctx);
  ui_row(ctx, 3);
  const struct {
    const char *id;
    icon i;
    f32 speed;
    const char *tip;
  } speeds[] = {{"##pause", i_pause, 0.0f, "Dừng (Space)"},
                {"##play", i_play, 1.0f, "Chạy"},
                {"##fast", i_fast, 3.0f, "Nhanh x3"}};
  for (const auto &sp : speeds) {
    const bool on = state.speed == sp.speed;
    use(ctx, on ? with_active(pill_style) : pill_style);
    if (icon_button(ctx, sp.id, sp.i, sp.tip, true, on ? accent : ink))
      state.speed = sp.speed;
  }
  use(ctx, pill_style);
  ui_end(ctx);
  drawn(ctx);
  // "Ngày 3 · 21:40", the day dim and the hour bright, centred over the buttons.
  char day[24], time[24];
  std::snprintf(day, sizeof(day), "Ngày %d  ·  ", state.day);
  const i32 minutes = static_cast<i32>(state.hour * 60.0f) % (24 * 60);
  std::snprintf(time, sizeof(time), "%02d:%02d", minutes / 60, minutes % 60);
  const f32 fs = 18.0f * k;
  const vec2 wd = text_measure(ctx, day, fs, font), wt = text_measure(ctx, time, fs, font);
  const vec2 at{std::floor(line.pos.x + (line.size.x - wd.x - wt.x) * 0.5f), std::floor(line.pos.y + (line.size.y - wd.y) * 0.5f)};
  draw_text(ctx, day, at, fs, ink_dim, font);
  draw_text(ctx, time, at + vec2{wd.x, 0.0f}, fs, ink, font);
}

void dock(context &ctx) {
  use(ctx, dock_style);
  ui_begin(ctx, {.id = "dock", .anchor = {0.5f, 1.0f}, .pivot = {0.5f, 1.0f}, .offset = vec2{0.0f, -16.0f} * k,
                 .width = 4 * 60.0f + 3 * 8.0f + 16.0f, .navigable = false});
  ui_row(ctx, 4);
  const struct {
    const char *id;
    icon i;
    popup p;
    const char *tip;
    rgba tint;
  } items[] = {{"##men", i_people, popup::men, "Đàn em", ink},
               {"##turf", i_flag, popup::turf, "Địa bàn", gang_red},
               {"##books", i_coins, popup::books, "Sổ sách", accent},
               {"##debug", i_bug, popup::debug, "Gỡ lỗi", ink_dim}};
  for (const auto &it : items) {
    use(ctx, open_popup == it.p ? with_active(dock_style) : dock_style);
    if (icon_button(ctx, it.id, it.i, it.tip, true, it.tint))
      open(it.p);
  }
  use(ctx, dock_style);
  ui_end(ctx);
  drawn(ctx);
}

// The building picked on the map: what it is, its floors, whom to send.
void card(context &ctx) {
  city::view_options &v = world_view();
  if (v.selected < 0)
    return;
  const city::city_map &m = world();
  const city::building &b = m.buildings[static_cast<size_t>(v.selected)];
  const city::business *bz = b.business >= 0 ? &m.businesses[static_cast<size_t>(b.business)] : nullptr;
  gang_state &g = gang();
  const i32 hq_of = gang_of_hq(v.selected);
  const bool hq = hq_of == 0;
  char title[160];
  if (hq_of >= 0)
    std::snprintf(title, sizeof(title), "Trụ sở · %s", gangs()[static_cast<size_t>(hq_of)].name.c_str());
  else
    std::snprintf(title, sizeof(title), "%s", bz ? bz->name.c_str() : city::building_name(b.kind));
  use(ctx, card_style);
  ui_begin(ctx, {.id = "card", .title = title, .anchor = {1.0f, 0.5f}, .pivot = {1.0f, 0.5f},
                 .offset = vec2{-16.0f, 0.0f} * k, .navigable = false});
  char line[200];
  if (bz) {
    std::snprintf(line, sizeof(line), "%s · hạng %d", city::business_name(bz->kind), bz->tier);
    label(ctx, line, ink_dim);
  }
  if (b.road >= 0 && b.number > 0)
    std::snprintf(line, sizeof(line), "%s %d tầng · số %d %s", city::building_name(b.kind), b.floors, b.number,
                  m.roads[static_cast<size_t>(b.road)].name.c_str());
  else
    std::snprintf(line, sizeof(line), "%s %d tầng", city::building_name(b.kind), b.floors);
  label(ctx, line, ink_dim);
  if (bz) {
    std::snprintf(line, sizeof(line), "Doanh thu %s/ngày", money(bz->income).c_str());
    label(ctx, line, ink);
    std::snprintf(line, sizeof(line), "Bảo kê %s/tuần", money(bz->protection).c_str());
    label(ctx, line, ink);
    const shop_state &s = shops()[static_cast<size_t>(b.business)];
    if (s.owner == 0)
      std::snprintf(line, sizeof(line), "Đang nộp cho băng mình · nợ %s", money(s.owed).c_str());
    else if (s.owner > 0)
      std::snprintf(line, sizeof(line), "Đang nộp cho %s", gangs()[static_cast<size_t>(s.owner)].name.c_str());
    else
      std::snprintf(line, sizeof(line), "Chưa nộp cho ai");
    label(ctx, line, s.owner == 0 ? rgb(120, 206, 140) : s.owner > 0 ? gangs()[static_cast<size_t>(s.owner)].colour : accent);
  }
  if (hq_of > 0) {
    // A rival's headquarters: what is known of it.
    const gang_state &r = gangs()[static_cast<size_t>(hq_of)];
    std::snprintf(line, sizeof(line), "Băng đối thủ · %d người · %d khối", static_cast<i32>(r.men.size()), r.turf);
    label(ctx, line, r.colour);
    std::snprintf(line, sizeof(line), "Cầm đầu: %s", r.men.empty() ? "?" : r.men[0].name.c_str());
    label(ctx, line, ink_dim);
  }
  // The floors, when there are more than one.
  if (b.floors > 1) {
    ui_row(ctx, 3);
    if (icon_button(ctx, "##floor_down", i_down, "Tầng dưới (PgDn)", v.floor > 0))
      v.floor = std::max(0, v.floor - 1);
    std::snprintf(line, sizeof(line), "Tầng %d/%d", v.floor + 1, b.floors);
    label(ctx, line, ink);
    if (icon_button(ctx, "##floor_up", i_up, "Tầng trên (PgUp)", v.floor + 1 < b.floors))
      v.floor = std::min(b.floors - 1, v.floor + 1);
  }
  if (hq) {
    std::snprintf(line, sizeof(line), "Tuyển thêm đàn em (%s)##recruit", money(recruit_cost).c_str());
    if (ui_button(ctx, line, g.money >= recruit_cost))
      gang_recruit(ctx);
    if (ui_button(ctx, g.mustered ? "Giải tán, vào trong##muster" : "Tập hợp trước trụ sở##muster"))
      gang_muster(ctx, !g.mustered);
  }
  if (bz) {
    ui_space(ctx, 4.0f);
    const i32 owner = shops()[static_cast<size_t>(b.business)].owner;
    label(ctx, owner == 0 ? "Cử người đi thu:" : owner > 0 ? "Cử người đi giành mối:" : "Cử người đi ép nộp:", ink_dim);
    bool any = false;
    for (i32 i = 0; i < static_cast<i32>(g.men.size()); ++i) {
      const lackey &man = g.men[static_cast<size_t>(i)];
      if (man.task != job::idle || man.rk == rank::boss)
        continue;
      any = true;
      std::snprintf(line, sizeof(line), "%s · %s · sức %d##send%d", man.name.c_str(), rank_name(man.rk), man.strength, i);
      if (ui_button(ctx, line) && !gang_send(ctx, i, b.business))
        ui_toast(ctx, "Không tìm được đường tới đó", {.kind = ui_toast_warning});
    }
    if (!any)
      label(ctx, "Không ai rảnh lúc này", ink_dim);
  }
  ui_space(ctx, 4.0f);
  if (ui_button(ctx, "Đóng##card"))
    world_unfocus();
  ui_end(ctx);
  drawn(ctx);
}

// --- Popups ---------------------------------------------------------------------------

bool popup_begin(context &ctx, const char *id, const char *title) {
  use(ctx, popup_style);
  ui_popup_begin(ctx, {.id = id, .title = title});
  return true;
}

// The last row of a popup: close it.
void popup_end(context &ctx) {
  ui_space(ctx, 8.0f);
  if (ui_button(ctx, "Đóng##popup") || ui_back(ctx))
    open_popup = popup::none;
  ui_popup_end(ctx);
  drawn(ctx);
}

void men_popup(context &ctx) {
  gang_state &g = gang();
  char title[64], line[200];
  std::snprintf(title, sizeof(title), "Đàn em · %d người", static_cast<i32>(g.men.size()));
  popup_begin(ctx, "men", title);
  std::snprintf(line, sizeof(line), "Lương cả băng: %s/ngày", money(gang_wages_per_day()).c_str());
  label(ctx, line, ink_dim);
  for (i32 i = 0; i < static_cast<i32>(g.men.size()); ++i) {
    const lackey &m = g.men[static_cast<size_t>(i)];
    ui_row(ctx, 4);
    label(ctx, m.name.c_str(), m.rk == rank::boss ? accent : ink);
    std::snprintf(line, sizeof(line), "%s · Sức %d · Lì %d · Lanh %d", rank_name(m.rk), m.strength, m.grit, m.wits);
    label(ctx, line, ink_dim);
    if (m.task == job::idle)
      std::snprintf(line, sizeof(line), "%s", job_name(m.task));
    else
      std::snprintf(line, sizeof(line), "%s · %s", job_name(m.task), gang_target_name(m));
    label(ctx, line, m.task == job::idle ? ink_dim : accent);
    std::snprintf(line, sizeof(line), "Xem##look%d", i);
    if (ui_button(ctx, line)) {
      open_popup = popup::none;
      view_focus(m.pos, 10.0f);
    }
  }
  ui_space(ctx, 8.0f);
  std::snprintf(line, sizeof(line), "Tuyển thêm đàn em (%s)##recruit_popup", money(recruit_cost).c_str());
  if (ui_button(ctx, line, g.money >= recruit_cost))
    gang_recruit(ctx);
  popup_end(ctx);
}

void turf_popup(context &ctx) {
  gang_state &g = gang();
  const city::city_map &m = world();
  popup_begin(ctx, "turf", "Địa bàn");
  char line[200];
  // Each gang: blocks held, shops paying.
  for (i32 gi = 0; gi < static_cast<i32>(gangs().size()); ++gi) {
    const gang_state &o = gangs()[static_cast<size_t>(gi)];
    i32 paying = 0;
    for (const shop_state &s : shops())
      paying += s.owner == gi ? 1 : 0;
    std::snprintf(line, sizeof(line), "%s%s: %d / %d khối · %d cơ sở nộp", o.name.c_str(), gi == 0 ? " (mình)" : "",
                  o.turf, static_cast<i32>(m.blocks.size()), paying);
    label(ctx, line, o.colour);
  }
  std::snprintf(line, sizeof(line), "Thu dự kiến: %s/ngày", money(gang_income_per_day()).c_str());
  label(ctx, line, ink_dim);
  label(ctx, "Một khối thuộc về băng khi từ nửa số cơ sở trong đó trở lên nộp tiền.", ink_dim);
  ui_toggle(ctx, "Hiện địa bàn trên bản đồ", world_view().show_turf);
  // The shops owing the most, to send someone to.
  std::vector<i32> owing;
  const std::vector<shop_state> &sh = shops();
  for (i32 i = 0; i < static_cast<i32>(sh.size()); ++i)
    if (sh[static_cast<size_t>(i)].owner == 0)
      owing.push_back(i);
  std::sort(owing.begin(), owing.end(),
            [&](i32 a, i32 b) { return sh[static_cast<size_t>(a)].owed > sh[static_cast<size_t>(b)].owed; });
  if (owing.size() > 6)
    owing.resize(6);
  if (!owing.empty()) {
    ui_space(ctx, 4.0f);
    label(ctx, "Nợ nhiều nhất:", ink_dim);
  }
  i32 idle = -1;
  for (i32 i = 0; i < static_cast<i32>(g.men.size()) && idle < 0; ++i)
    if (g.men[static_cast<size_t>(i)].task == job::idle && g.men[static_cast<size_t>(i)].rk != rank::boss)
      idle = i;
  for (const i32 s : owing) {
    ui_row(ctx, 3);
    label(ctx, m.businesses[static_cast<size_t>(s)].name.c_str(), ink);
    label(ctx, money(sh[static_cast<size_t>(s)].owed).c_str(), accent);
    std::snprintf(line, sizeof(line), "Cử người##owe%d", s);
    if (ui_button(ctx, line, idle >= 0))
      gang_send(ctx, idle, s);
  }
  popup_end(ctx);
}

void books_popup(context &ctx) {
  const gang_state &g = gang();
  popup_begin(ctx, "books", "Sổ sách");
  char line[200];
  const i32 in = gang_income_per_day(), out = gang_wages_per_day();
  ui_row(ctx, 2);
  label(ctx, "Tiền mặt", ink_dim);
  label(ctx, money(g.money).c_str(), g.money < 0 ? gang_red : accent);
  ui_row(ctx, 2);
  label(ctx, "Bảo kê dự kiến / ngày", ink_dim);
  label(ctx, ("+" + money(in)).c_str(), rgb(120, 206, 140));
  ui_row(ctx, 2);
  label(ctx, "Lương / ngày", ink_dim);
  label(ctx, money(-out).c_str(), gang_red);
  ui_row(ctx, 2);
  label(ctx, "Lãi / ngày", ink_dim);
  label(ctx, money(in - out).c_str(), in - out >= 0 ? ink : gang_red);
  ui_space(ctx, 8.0f);
  label(ctx, "Gần đây:", ink_dim);
  const i32 n = static_cast<i32>(g.ledger.size());
  for (i32 i = n - 1; i >= std::max(0, n - 8); --i) {
    const ledger_line &l = g.ledger[static_cast<size_t>(i)];
    const i32 minutes = static_cast<i32>(l.hour * 60.0f);
    ui_row(ctx, 3);
    std::snprintf(line, sizeof(line), "Ngày %d · %02d:%02d", l.day, minutes / 60, minutes % 60);
    label(ctx, line, ink_dim);
    label(ctx, (l.amount >= 0 ? "+" + money(l.amount) : money(l.amount)).c_str(),
          l.amount >= 0 ? rgb(120, 206, 140) : gang_red);
    label(ctx, l.what.c_str(), ink);
  }
  popup_end(ctx);
}

void debug_popup(context &ctx) {
  const city::city_map &m = world();
  city::view_options &v = world_view();
  popup_begin(ctx, "debug", "Gỡ lỗi");
  char line[240];
  std::snprintf(line, sizeof(line), "Thành phố #%u · %d quận · %d khối · %d nhà · %d cơ sở · %d chỗ · %.0f ms",
                m.desc.seed, static_cast<i32>(m.districts.size()), static_cast<i32>(m.blocks.size()),
                static_cast<i32>(m.buildings.size()), static_cast<i32>(m.businesses.size()),
                static_cast<i32>(m.spots.size()), static_cast<f64>(m.report.gen_ms));
  label(ctx, line, ink);
  if (m.report.ok())
    label(ctx, "Kiểm tra thành phố: đạt", rgb(120, 206, 140));
  else
    for (size_t i = 0; i < m.report.errors.size() && i < 4; ++i)
      label(ctx, m.report.errors[i].c_str(), gang_red);
  const city::view_stats &vs = city::view_last_stats();
  static f32 frame_time = 1.0f / 60.0f;
  frame_time += (delta_real(ctx) - frame_time) * 0.05f;
  std::snprintf(line, sizeof(line), "Vẽ %d/%d ô · chi tiết %d ô · %u khối · %.0f fps", vs.visible, vs.chunks,
                vs.detailed, vs.instances, static_cast<f64>(1.0f / std::max(frame_time, 1e-4f)));
  label(ctx, line, ink_dim);
  ui_space(ctx, 4.0f);
  ui_row(ctx, 3);
  if (ui_button(ctx, "Seed trước (B)", m.desc.seed > 1))
    world_generate(ctx, m.desc.seed - 1);
  if (ui_button(ctx, "Seed sau (N)"))
    world_generate(ctx, m.desc.seed + 1);
  if (ui_button(ctx, "Ngẫu nhiên (R)")) {
    const u64 t = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
    world_generate(ctx, static_cast<u32>((t ^ (t >> 32)) % 100000u) + 1u);
  }
  i32 layer = static_cast<i32>(v.layer);
  if (ui_choice(ctx, "Lớp phủ (F1)", layer, {"Không", "Quận", "Khối", "Đi bộ", "Xe chạy"}))
    v.layer = static_cast<city::overlay>(layer);
  ui_row(ctx, 2);
  ui_toggle(ctx, "Nhãn tên (F2)", v.labels);
  ui_toggle(ctx, "Đồ thị đường (F3)", v.graph);
  ui_row(ctx, 2);
  ui_toggle(ctx, "Ghim (F4)", v.markers);
  ui_toggle(ctx, "Thẻ khi rê chuột", v.debug_card);
  bool around = world_cut_around_on();
  ui_row(ctx, 2);
  if (ui_toggle(ctx, "Mở mọi nhà quanh (C)", around))
    world_cut_around(around);
  if (ui_button(ctx, darkness() < 0.5f ? "Nhảy tới đêm (T)" : "Nhảy tới ngày (T)"))
    state.hour = darkness() < 0.5f ? 21.0f : 12.0f;
  ui_space(ctx, 4.0f);
  label(ctx, "WASD / kéo chuột giữa: di chuyển · Q E: xoay · Lăn chuột: gần xa · Space: dừng", ink_dim);
  label(ctx, "Nhấp nhà: xem · Esc: thôi · PgUp PgDn: đổi tầng", ink_dim);
  popup_end(ctx);
}

} // namespace

void hud_init(context &ctx, font_handle f) {
  font = f;
  make_styles();
  const char *const names[icon_count] = {"people", "flag", "coins", "bug", "pause", "play",
                                         "fast", "up", "down", "close", "house"};
  char path[96];
  for (i32 i = 0; i < icon_count; ++i) {
    std::snprintf(path, sizeof(path), "assets/ui/%s.png", names[i]);
    icons[i] = texture_load(ctx, path);
  }
}

void hud_cleanup(context &ctx) {
  for (texture_handle &t : icons)
    if (t.id != 0) {
      texture_unload(ctx, t);
      t = {};
    }
}

void hud_draw(context &ctx) {
  k = ui_scale(ctx);
  resources(ctx);
  clock(ctx);
  dock(ctx);
  // The card hides while a popup is open.
  if (open_popup == popup::none)
    card(ctx);
  switch (open_popup) {
  case popup::men: men_popup(ctx); break;
  case popup::turf: turf_popup(ctx); break;
  case popup::books: books_popup(ctx); break;
  case popup::debug: debug_popup(ctx); break;
  case popup::none: break;
  }
  state.popup_open = open_popup != popup::none;
  draw_tip(ctx);
  // Space pauses and goes on, when no popup has the keys.
  if (!state.popup_open && key_pressed(ctx, key_space))
    state.speed = state.speed > 0.0f ? 0.0f : 1.0f;
  use(ctx, pill_style);
}

} // namespace sandtable
