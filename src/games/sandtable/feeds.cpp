#include "feeds.h"
#include "crowd.h"
#include "gang.h"
#include "view.h"
#include "weather.h"
#include "world.h"

#include <algorithm>
#include <cmath>

namespace sandtable {

namespace {

bool on = false;
std::vector<feed> list;
vec2 cell{320.0f, 180.0f};
size_t next_view = 0;

constexpr i32 per_frame = 2;    // views redrawn each frame, in turn
constexpr f32 reach = 260.0f;   // world units of the town drawn round him

void drop(context &ctx, feed &f) {
  if (f.view.id != 0)
    render_texture_unload(ctx, f.view);
  f.view = {};
  f.fresh = false;
}

// The list follows the men on a job: out of the headquarters and not idle.
void sync(context &ctx) {
  const std::vector<lackey> &men = gang().men;
  const auto busy = [&](i32 i) {
    if (i < 0 || i >= static_cast<i32>(men.size()))
      return false;
    const lackey &m = men[static_cast<size_t>(i)];
    return m.task != job::idle && !m.inside;
  };
  for (auto it = list.begin(); it != list.end();)
    if (!busy(it->man)) {
      drop(ctx, *it);
      it = list.erase(it);
    } else {
      ++it;
    }
  for (i32 i = 0; i < static_cast<i32>(men.size()); ++i)
    if (busy(i) && std::none_of(list.begin(), list.end(), [&](const feed &f) { return f.man == i; }))
      list.push_back({.man = i});
  std::sort(list.begin(), list.end(), [](const feed &a, const feed &b) { return a.man < b.man; });
}

// Over his shoulder: a little behind and above his head, looking the way he
// faces, down a touch to the street ahead.
camera3d eye_of(const lackey &m) {
  const f32 metre = city::units_per_metre;
  const vec2 ahead = from_angle(m.facing);
  camera3d c;
  c.position = to3d(m.pos - ahead * (1.4f * metre), 2.2f * metre * unit3d);
  c.target = to3d(m.pos + ahead * (8.0f * metre), 1.0f * metre * unit3d);
  c.fovy = 62.0f;
  c.near_plane = 0.02f;
  c.far_plane = reach * 1.5f * unit3d;
  c.entities = false;
  return c;
}

} // namespace

bool &feeds_on() { return on; }
const std::vector<feed> &feeds() { return list; }

void feeds_set_size(vec2 size) { cell = {std::floor(size.x), std::floor(size.y)}; }

void feeds_render(context &ctx) {
  if (!on) {
    if (!list.empty())
      feeds_cleanup(ctx);
    return;
  }
  sync(ctx);
  if (list.empty() || cell.x < 8.0f || cell.y < 8.0f)
    return;
  // The town as the main view has it, less what is only for the table seen
  // from above: nothing cut open, lit up or labelled.
  city::view_options eye = world_view();
  eye.cut.clear();
  eye.selected = -1;
  eye.around = false;
  eye.focused = false;
  eye.hover_district = eye.hover_building = -1;
  eye.person_focused = false;
  eye.show_turf = false;
  eye.layer = city::overlay::none;
  eye.labels = eye.graph = eye.markers = false;
  // No sun shadows in the small views: a shadow pass each would cost more
  // than the view itself. The table's fog is the dark round it; seen from the
  // street, the far end fades into the sky instead.
  const rgba sky = lerp(rgba{0.62f, 0.74f, 0.86f, 1.0f}, rgba{0.04f, 0.05f, 0.09f, 1.0f}, darkness());
  const light3d sun = light3d_get(ctx);
  light3d flat = sun;
  flat.shadows = false;
  flat.fog_color = sky;
  light3d_set(ctx, flat);
  const std::vector<lackey> &men = gang().men;
  const i32 n = std::min(per_frame, static_cast<i32>(list.size()));
  for (i32 k = 0; k < n; ++k) {
    feed &f = list[next_view++ % list.size()];
    if (f.view.id == 0 || f.size.x != cell.x || f.size.y != cell.y) {
      drop(ctx, f);
      f.view = render_texture_load(ctx, static_cast<u32>(cell.x), static_cast<u32>(cell.y));
      f.size = cell;
      if (f.view.id == 0)
        continue;
    }
    const lackey &m = men[static_cast<size_t>(f.man)];
    begin_3d(ctx, eye_of(m), f.view, sky);
    city::view_draw_eye(ctx, world(), eye, m.pos, reach);
    crowd_draw_around(ctx, m.pos, reach);
    gang_draw_around(ctx, m.pos, reach);
    end_3d(ctx);
    f.fresh = true;
  }
  light3d_set(ctx, sun);
}

void feeds_cleanup(context &ctx) {
  for (feed &f : list)
    drop(ctx, f);
  list.clear();
  next_view = 0;
}

} // namespace sandtable
