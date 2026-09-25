#pragma once

#include "njin_anim.h"
#include <string>
#include <vector>

namespace njin {
struct anim_frame {
  rect source{};
  f32 duration = 0.1f; // seconds
};

struct anim_clip {
  std::string name;
  std::vector<i32> sequence; // frame indices in play order, one pass
  i32 repeat = 0;            // 0 loops forever
};

struct anim_sheet {
  texture_handle texture{};
  bool owns_texture = false; // loaded by anim_sheet_load, freed with the sheet
  std::vector<anim_frame> frames;
  std::vector<anim_clip> clips;
  bool alive = false;
};

struct anim_graph_state {
  std::string name;
  i32 clip = -1;
  f32 speed = 1.0f;
  i32 repeat = -1;
};

struct anim_graph_cond {
  i32 param = 0;
  anim_cmp cmp = anim_true;
  f32 value = 0.0f;
};

struct anim_graph_transition {
  i32 from = -1; // -1 is any state
  i32 to = 0;
  std::vector<anim_graph_cond> when;
  bool after_finish = false;
};

struct anim_graph {
  anim_sheet_handle sheet{};
  std::vector<anim_graph_state> states;
  std::vector<anim_graph_transition> transitions;
  std::vector<std::string> params;
  std::vector<bool> is_trigger; // per param: cleared at the end of each tick
  i32 start = 0;
};

// Handle id N maps to sheets[N - 1] / graphs[N - 1]; slots are never reused.
// Graphs are never removed; a graph whose sheet was unloaded just stops.
struct anim_store {
  std::vector<anim_sheet> sheets;
  std::vector<anim_graph> graphs;
};

// Starts `clip` from its first frame. `repeat` 0 loops forever.
void animator_enter_clip(animator &anim, i32 clip, i32 repeat);
// Enters a graph state, starting its clip with the state's repeat override.
void animator_enter_state(const anim_graph &graph, const anim_sheet &sheet,
                          animator &anim, i32 state);

inline const anim_sheet *anim_sheet_of(const anim_store &store,
                                       anim_sheet_handle handle) {
  if (handle.id == 0 || handle.id > store.sheets.size())
    return nullptr;
  const anim_sheet &sheet = store.sheets[handle.id - 1];
  return sheet.alive ? &sheet : nullptr;
}

inline const anim_graph *anim_graph_of(const anim_store &store,
                                       anim_graph_handle handle) {
  if (handle.id == 0 || handle.id > store.graphs.size())
    return nullptr;
  return &store.graphs[handle.id - 1];
}
} // namespace njin
