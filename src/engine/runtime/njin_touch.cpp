#include "njin_touch.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_input_impl.h"
#include "njin_view.h"
#include <algorithm>
#include <cmath>

namespace njin {
namespace {
// A finger that slides farther than this (window pixels) is no longer a tap
// or a long press.
constexpr f32 tap_slop = 12.0f;
constexpr f64 tap_time = 0.3;
constexpr f64 long_press_time = 0.5;

// Client pixels to screen units, as the mouse.
vec2 mapped(const view_state &view, vec2 raw) {
  vec2 motion{};
  view_map_mouse(view, raw, motion);
  return raw;
}

touch_track *find(touch_state &t, i64 os_id) {
  for (touch_track &p : t.points)
    if (p.down && p.os_id == os_id)
      return &p;
  return nullptr;
}
} // namespace

void touch_poll(touch_state &t, const view_state &view, void *window, f64 now) {
  if (!t.tried) {
    t.tried = true;
    touch_native_install(window);
  }
  // Fingers lifted last frame are gone; the rest start the frame unflagged.
  std::erase_if(t.points, [](const touch_track &p) { return !p.down; });
  for (touch_track &p : t.points) {
    p.prev = p.pub.pos;
    p.pub.pressed = false;
  }

  touch_native_take(t.events);
  for (const touch_raw &e : t.events) {
    const vec2 raw{e.x, e.y};
    touch_track *p = find(t, e.os_id);
    if (e.what == touch_raw::down || p == nullptr) {
      // A move or lift of a finger never seen down (it touched before the
      // window listened) starts it here.
      if (e.what == touch_raw::up && p == nullptr)
        continue;
      if ((i32)std::count_if(t.points.begin(), t.points.end(), [](const touch_track &q) { return q.down; }) >=
          touch_max)
        continue;
      touch_track n;
      n.os_id = e.os_id;
      n.pub.id = t.next_id++;
      n.pub.pos = n.pub.start = n.prev = mapped(view, raw);
      n.pub.pressed = true;
      n.raw = n.raw_start = raw;
      n.since = now;
      t.points.push_back(n);
      continue;
    }
    p->raw = raw;
    p->pub.pos = mapped(view, raw);
    if (length(raw - p->raw_start) > tap_slop)
      p->moved = true;
    if (e.what == touch_raw::up)
      p->down = false;
  }

  gesture_state g{};
  vec2 pan{};
  i32 steady = 0; // fingers down now; a new one counts from where it landed
  const touch_track *two[2] = {nullptr, nullptr};
  vec2 sum{};
  for (touch_track &p : t.points) {
    p.pub.released = !p.down;
    // A finger that landed this frame may already have slid (its first move
    // came with the touch): from where it landed, so no motion is lost.
    p.pub.delta = p.pub.pos - p.prev;
    p.pub.duration = (f32)(now - p.since);
    const f64 held = now - p.since;
    if (!p.down) {
      if (!p.moved && !p.long_sent && held < tap_time) {
        g.tap = true;
        g.tap_pos = p.pub.pos;
      }
      continue;
    }
    g.fingers++;
    sum += p.pub.pos;
    if (!p.moved && !p.long_sent && held >= long_press_time) {
      p.long_sent = true;
      g.long_press = true;
      g.long_press_pos = p.pub.pos;
    }
    pan += p.pub.delta;
    if (steady < 2)
      two[steady] = &p;
    steady++;
  }
  if (steady > 0)
    g.pan = pan * (1.0f / (f32)steady);
  if (g.fingers > 0)
    g.center = sum * (1.0f / (f32)g.fingers);
  if (two[1] != nullptr) {
    const vec2 now_d = two[1]->pub.pos - two[0]->pub.pos;
    const vec2 was_d = two[1]->prev - two[0]->prev;
    const f32 was = length(was_d);
    if (was > 1e-3f)
      g.pinch = length(now_d) / was;
    f32 turn = angle_of(now_d) - angle_of(was_d);
    turn -= 360.0f * std::floor((turn + 180.0f) / 360.0f);
    g.rotate = turn;
    g.center = (two[0]->pub.pos + two[1]->pub.pos) * 0.5f;
  }
  t.gestures = g;
}

namespace {
// In the UI canvas (ui_canvas_begin()), in its units, as mouse_pos().
f32 canvas_factor(const context &ctx) {
  return view_in_canvas(ctx_ref<view_state>(ctx)) ? 1.0f / ctx_ref<view_state>(ctx)->canvas_scale : 1.0f;
}
} // namespace

i32 touch_count(const context &ctx) { return (i32)ctx_ref<input_store>(ctx)->touch.points.size(); }

touch_point touch_get(const context &ctx, i32 index) {
  const touch_state &t = ctx_ref<input_store>(ctx)->touch;
  if (index < 0 || index >= (i32)t.points.size())
    return {};
  touch_point p = t.points[(usize)index].pub;
  const f32 k = canvas_factor(ctx);
  p.pos = p.pos * k;
  p.delta = p.delta * k;
  p.start = p.start * k;
  return p;
}

gesture_state gestures(const context &ctx) {
  gesture_state g = ctx_ref<input_store>(ctx)->touch.gestures;
  const f32 k = canvas_factor(ctx);
  g.tap_pos = g.tap_pos * k;
  g.long_press_pos = g.long_press_pos * k;
  g.pan = g.pan * k;
  g.center = g.center * k;
  return g;
}
} // namespace njin
