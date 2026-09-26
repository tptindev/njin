#include "anim.h"
#include "_comps.h"
#include "njin_anim_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include <algorithm>

namespace njin {
namespace {
// A frame shorter than this counts as this long, so a zero duration from a
// bad export cannot spin the advance loop forever.
constexpr f32 min_frame_duration = 0.001f;

bool holds(const anim_graph_cond &cond, f32 value) {
  switch (cond.cmp) {
  case anim_gt: return value > cond.value;
  case anim_lt: return value < cond.value;
  case anim_ge: return value >= cond.value;
  case anim_le: return value <= cond.value;
  case anim_eq: return value == cond.value;
  case anim_ne: return value != cond.value;
  case anim_true:
  case anim_trigger: return value != 0.0f;
  case anim_false: return value == 0.0f;
  }
  return false;
}

// Follows at most one transition: the first, in declaration order, whose
// source, conditions and finish rule all hold.
void follow_transitions(const anim_graph &graph, const anim_sheet &sheet,
                        animator &anim) {
  const bool done_once = anim.finished || anim.loops > 0;
  for (const anim_graph_transition &t : graph.transitions) {
    if (t.from >= 0 ? t.from != anim.state : t.to == anim.state)
      continue;
    if (t.after_finish && !done_once)
      continue;
    bool ok = true;
    for (const anim_graph_cond &cond : t.when) {
      if (!holds(cond, anim.params[(usize)cond.param])) {
        ok = false;
        break;
      }
    }
    if (!ok)
      continue;
    for (const anim_graph_cond &cond : t.when) {
      if (cond.cmp == anim_trigger)
        anim.params[(usize)cond.param] = 0.0f;
    }
    animator_enter_state(graph, sheet, anim, t.to);
    return;
  }
}

void advance(const anim_sheet &sheet, animator &anim, f32 dt) {
  const anim_clip &clip = sheet.clips[(usize)anim.clip];
  if (clip.sequence.empty())
    return;
  if (anim.step < 0 || anim.step >= (i32)clip.sequence.size())
    anim.step = 0;
  anim.state_time += dt;
  if (!anim.playing || anim.finished || dt <= 0.0f)
    return;
  anim.time += dt;
  while (true) {
    const i32 frame = clip.sequence[(usize)anim.step];
    const f32 duration = std::max(sheet.frames[(usize)frame].duration, min_frame_duration);
    if (anim.time < duration)
      return;
    anim.time -= duration;
    if (anim.step + 1 < (i32)clip.sequence.size()) {
      anim.step++;
      continue;
    }
    anim.loops++;
    if (anim.repeat > 0 && anim.loops >= anim.repeat) {
      anim.finished = true;
      anim.time = 0.0f;
      return;
    }
    anim.step = 0;
  }
}

void update(njin_ctx &ctx) {
  const f32 dt = delta(ctx);
  for (auto [entity, anim, spr] : world(ctx).view<animator, sprite>().each()) {
    const anim_graph *graph = anim_graph_of(ctx.anim, anim.graph);
    const anim_sheet *sheet =
        anim_sheet_of(ctx.anim, graph != nullptr ? graph->sheet : anim.sheet);
    if (sheet == nullptr)
      continue;
    if (graph != nullptr) {
      if (anim.state < 0 || anim.state >= (i32)graph->states.size())
        animator_enter_state(*graph, *sheet, anim, graph->start);
      follow_transitions(*graph, *sheet, anim);
      // Triggers last one frame: one not used now must not fire later.
      for (usize i = 0; i < graph->is_trigger.size(); i++) {
        if (graph->is_trigger[i])
          anim.params[i] = 0.0f;
      }
    }
    if (anim.clip < 0 || anim.clip >= (i32)sheet->clips.size())
      continue;
    advance(*sheet, anim, dt * anim.speed *
                              (graph != nullptr ? graph->states[(usize)anim.state].speed : 1.0f));
    const anim_clip &clip = sheet->clips[(usize)anim.clip];
    if (clip.sequence.empty())
      continue;
    anim.frame = clip.sequence[(usize)anim.step];
    spr.texture = sheet->texture;
    spr.source = sheet->frames[(usize)anim.frame].source;
  }
}

void setup(njin_ctx &ctx) { ecs_register(ctx, phase_post_update, update, "update"); }
} // namespace

mod_desc anim_module() { return mod_desc{.name = "njin.anim", .setup = setup}; }
} // namespace njin
