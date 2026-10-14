#pragma once
#include "njin_internal_only.h"

#include "njin_json.h"
#include "njin_quality.h"

namespace njin {
struct context;

// graphics_quality_set(). The defaults cap nothing, so a game that never sets
// it draws exactly as before. Effects read their limits from here when they
// draw; post3d and post_fx keep the game's request apart from what is drawn
// (post3d_apply_quality, post_fx_apply_quality).
struct quality_state {
  graphics_quality q{};
  // A game set it at least once: settings_save() stores it from then on, so
  // the settings file of a game that never does keeps its old shape.
  bool set = false;
};

// The share of the screen the world pass draws at this frame: q.world_scale,
// clamped, or 1 under a virtual size.
f32 quality_world_scale(const context &ctx);

// settings_save()/settings_load(): the "graphics" object, written only once a
// game has set a quality (quality_state::set).
bool quality_was_set(const context &ctx);
json_value quality_to_json(const graphics_quality &q);
// Fields missing from `json` keep their value in `base`.
graphics_quality quality_from_json(const json_value &json, graphics_quality base);
} // namespace njin
