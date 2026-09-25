#include "njin_anim.h"
#include "njin_anim_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_file.h"
#include "njin_json.h"
#include "njin_log.h"
#include "njin_path.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace njin {
namespace {
anim_sheet *sheet_of(anim_store &store, anim_sheet_handle handle) {
  return const_cast<anim_sheet *>(
      anim_sheet_of(static_cast<const anim_store &>(store), handle));
}

i32 clip_index(const anim_sheet &sheet, const char *name) {
  if (name == nullptr)
    return -1;
  for (usize i = 0; i < sheet.clips.size(); i++) {
    if (sheet.clips[i].name == name)
      return (i32)i;
  }
  return -1;
}

i32 state_index(const anim_graph &graph, const char *name) {
  if (name == nullptr)
    return -1;
  for (usize i = 0; i < graph.states.size(); i++) {
    if (graph.states[i].name == name)
      return (i32)i;
  }
  return -1;
}

i32 param_index(const anim_graph &graph, const char *name) {
  if (name == nullptr)
    return -1;
  for (usize i = 0; i < graph.params.size(); i++) {
    if (graph.params[i] == name)
      return (i32)i;
  }
  return -1;
}

// Play order of one pass over frames from..to.
std::vector<i32> make_sequence(i32 from, i32 to, anim_direction direction) {
  std::vector<i32> forward;
  for (i32 i = from; i <= to; i++)
    forward.push_back(i);
  std::vector<i32> backward(forward.rbegin(), forward.rend());
  switch (direction) {
  case anim_forward:
    return forward;
  case anim_reverse:
    return backward;
  case anim_ping_pong:
  case anim_ping_pong_reverse: {
    // 0 1 2 3 then 2 1: the ends are not repeated when the pass loops.
    std::vector<i32> out = direction == anim_ping_pong ? forward : backward;
    const std::vector<i32> &back = direction == anim_ping_pong ? backward : forward;
    for (usize i = 1; i + 1 < back.size(); i++)
      out.push_back(back[i]);
    return out;
  }
  }
  return forward;
}

bool add_clip(anim_sheet &sheet, const anim_clip_desc &desc) {
  if (desc.name == nullptr) {
    NJIN_WARN("anim: clip name is null");
    return false;
  }
  if (clip_index(sheet, desc.name) >= 0) {
    NJIN_WARN("anim: clip '%s' already exists", desc.name);
    return false;
  }
  const i32 count = (i32)sheet.frames.size();
  if (desc.from < 0 || desc.to < desc.from || desc.to >= count) {
    NJIN_WARN("anim: clip '%s' frames %d..%d outside the sheet (%d frames)",
              desc.name, desc.from, desc.to, count);
    return false;
  }
  sheet.clips.push_back(anim_clip{
      .name = desc.name,
      .sequence = make_sequence(desc.from, desc.to, desc.direction),
      .repeat = desc.repeat > 0 ? desc.repeat : 0});
  return true;
}

anim_direction direction_of(const char *name) {
  if (std::strcmp(name, "reverse") == 0)
    return anim_reverse;
  if (std::strcmp(name, "pingpong") == 0)
    return anim_ping_pong;
  if (std::strcmp(name, "pingpong_reverse") == 0)
    return anim_ping_pong_reverse;
  return anim_forward;
}

// Reads one Aseprite frame entry ("frame": {x, y, w, h}, "duration": ms).
bool read_frame(const json_value &entry, anim_frame &out, bool &trimmed) {
  const json_value &f = entry["frame"];
  if (!f.is(json_value::object))
    return false;
  out.source = rect{{(f32)f["x"].number_or(0.0), (f32)f["y"].number_or(0.0)},
                    {(f32)f["w"].number_or(0.0), (f32)f["h"].number_or(0.0)}};
  out.duration = (f32)(entry["duration"].number_or(100.0) / 1000.0);
  if (entry["trimmed"].is(json_value::boolean) && entry["trimmed"].b)
    trimmed = true;
  return true;
}
} // namespace

anim_sheet_handle anim_sheet_load(njin_ctx &ctx, const char *json_path) {
  if (json_path == nullptr) {
    NJIN_WARN("anim_sheet_load: path is null");
    return anim_sheet_handle{};
  }
  const std::string resolved = asset_path(json_path);
  std::string text;
  if (!file_read(resolved.c_str(), text)) {
    NJIN_WARN("anim_sheet_load: cannot read %s", json_path);
    return anim_sheet_handle{};
  }
  json_value doc;
  std::string error;
  if (!json_parse(text, doc, error)) {
    NJIN_WARN("anim_sheet_load: %s: %s", json_path, error.c_str());
    return anim_sheet_handle{};
  }

  anim_sheet sheet{};
  bool trimmed = false;
  const json_value &frames = doc["frames"];
  // "Array" layout is a list; "Hash" layout is an object keyed by file name,
  // in frame order.
  if (frames.is(json_value::array)) {
    for (const json_value &entry : frames.items) {
      anim_frame frame{};
      if (read_frame(entry, frame, trimmed))
        sheet.frames.push_back(frame);
    }
  } else if (frames.is(json_value::object)) {
    for (const auto &[name, entry] : frames.members) {
      anim_frame frame{};
      if (read_frame(entry, frame, trimmed))
        sheet.frames.push_back(frame);
    }
  }
  if (sheet.frames.empty()) {
    NJIN_WARN("anim_sheet_load: %s has no frames (not an Aseprite export?)", json_path);
    return anim_sheet_handle{};
  }
  if (trimmed)
    NJIN_WARN("anim_sheet_load: %s was exported with Trim; frames will shift. "
              "Export with Trim off.", json_path);

  const json_value &meta = doc["meta"];
  const char *image = meta["image"].string_or(nullptr);
  if (image == nullptr) {
    NJIN_WARN("anim_sheet_load: %s has no meta.image", json_path);
    return anim_sheet_handle{};
  }
  // meta.image is relative to the JSON file.
  const std::filesystem::path json_fs(reinterpret_cast<const char8_t *>(resolved.c_str()));
  const std::filesystem::path image_fs =
      json_fs.parent_path() / std::filesystem::path(reinterpret_cast<const char8_t *>(image));
  const std::u8string image_u8 = image_fs.generic_u8string();
  const std::string image_path(image_u8.begin(), image_u8.end());
  sheet.texture = texture_store_load(ctx.texture, image_path.c_str());
  if (sheet.texture.id == 0)
    return anim_sheet_handle{};
  sheet.owns_texture = true;
  sheet.alive = true;

  for (const json_value &tag : meta["frameTags"].items) {
    const json_value &repeat = tag["repeat"];
    // Aseprite writes repeat as a string ("2"); accept a number too.
    const i32 times = repeat.is(json_value::string) ? std::atoi(repeat.str.c_str())
                                                    : (i32)repeat.number_or(0.0);
    add_clip(sheet, anim_clip_desc{.name = tag["name"].string_or(nullptr),
                                   .from = (i32)tag["from"].number_or(0.0),
                                   .to = (i32)tag["to"].number_or(0.0),
                                   .direction = direction_of(tag["direction"].string_or("forward")),
                                   .repeat = times});
  }
  if (sheet.clips.empty())
    add_clip(sheet, anim_clip_desc{.name = "default",
                                   .from = 0,
                                   .to = (i32)sheet.frames.size() - 1});

  ctx.anim.sheets.push_back(std::move(sheet));
  return anim_sheet_handle{.id = (u32)ctx.anim.sheets.size()};
}

anim_sheet_handle anim_sheet_grid(njin_ctx &ctx, texture_handle texture,
                                  vec2 frame_size, f32 fps) {
  const vec2 size = texture_store_size(ctx.texture, texture);
  if (size.x <= 0.0f || frame_size.x <= 0.0f || frame_size.y <= 0.0f) {
    NJIN_WARN("anim_sheet_grid: invalid texture or frame size");
    return anim_sheet_handle{};
  }
  const i32 columns = (i32)(size.x / frame_size.x);
  const i32 rows = (i32)(size.y / frame_size.y);
  if (columns <= 0 || rows <= 0) {
    NJIN_WARN("anim_sheet_grid: frame larger than the texture");
    return anim_sheet_handle{};
  }
  anim_sheet sheet{};
  sheet.texture = texture;
  sheet.alive = true;
  const f32 duration = fps > 0.0f ? 1.0f / fps : 0.1f;
  for (i32 y = 0; y < rows; y++) {
    for (i32 x = 0; x < columns; x++)
      sheet.frames.push_back(anim_frame{
          .source = {{(f32)x * frame_size.x, (f32)y * frame_size.y}, frame_size},
          .duration = duration});
  }
  ctx.anim.sheets.push_back(std::move(sheet));
  return anim_sheet_handle{.id = (u32)ctx.anim.sheets.size()};
}

bool anim_sheet_add_clip(njin_ctx &ctx, anim_sheet_handle handle,
                         const anim_clip_desc &desc) {
  anim_sheet *sheet = sheet_of(ctx.anim, handle);
  if (sheet == nullptr) {
    NJIN_WARN("anim_sheet_add_clip: invalid sheet handle %u", handle.id);
    return false;
  }
  return add_clip(*sheet, desc);
}

void anim_sheet_unload(njin_ctx &ctx, anim_sheet_handle handle) {
  anim_sheet *sheet = sheet_of(ctx.anim, handle);
  if (sheet == nullptr)
    return;
  if (sheet->owns_texture)
    texture_store_unload(ctx.texture, sheet->texture);
  *sheet = anim_sheet{};
}

i32 anim_clip_find(const njin_ctx &ctx, anim_sheet_handle handle,
                   const char *name) {
  const anim_sheet *sheet = anim_sheet_of(ctx.anim, handle);
  return sheet != nullptr ? clip_index(*sheet, name) : -1;
}

f32 anim_clip_duration(const njin_ctx &ctx, anim_sheet_handle handle,
                       const char *name) {
  const anim_sheet *sheet = anim_sheet_of(ctx.anim, handle);
  const i32 clip = sheet != nullptr ? clip_index(*sheet, name) : -1;
  if (clip < 0)
    return 0.0f;
  f32 total = 0.0f;
  for (const i32 frame : sheet->clips[(usize)clip].sequence)
    total += sheet->frames[(usize)frame].duration;
  return total;
}

anim_graph_handle anim_graph_create(njin_ctx &ctx, const anim_graph_desc &desc) {
  const anim_sheet *sheet = anim_sheet_of(ctx.anim, desc.sheet);
  if (sheet == nullptr) {
    NJIN_WARN("anim_graph_create: invalid sheet handle %u", desc.sheet.id);
    return anim_graph_handle{};
  }
  if (desc.states.empty()) {
    NJIN_WARN("anim_graph_create: no states");
    return anim_graph_handle{};
  }
  anim_graph graph{};
  graph.sheet = desc.sheet;
  for (const anim_state_desc &s : desc.states) {
    if (s.name == nullptr || state_index(graph, s.name) >= 0) {
      NJIN_WARN("anim_graph_create: state name missing or duplicated");
      return anim_graph_handle{};
    }
    const i32 clip = clip_index(*sheet, s.clip);
    if (clip < 0) {
      NJIN_WARN("anim_graph_create: state '%s': no clip named '%s'", s.name,
                s.clip != nullptr ? s.clip : "");
      return anim_graph_handle{};
    }
    graph.states.push_back(
        anim_graph_state{.name = s.name, .clip = clip, .speed = s.speed, .repeat = s.repeat});
  }
  for (const anim_transition_desc &t : desc.transitions) {
    anim_graph_transition out{};
    out.after_finish = t.after_finish;
    out.from = t.from != nullptr ? state_index(graph, t.from) : -1;
    out.to = state_index(graph, t.to);
    if ((t.from != nullptr && out.from < 0) || out.to < 0) {
      NJIN_WARN("anim_graph_create: transition %s -> %s names a missing state",
                t.from != nullptr ? t.from : "*", t.to != nullptr ? t.to : "");
      return anim_graph_handle{};
    }
    for (const anim_cond &c : t.when) {
      if (c.param == nullptr) {
        NJIN_WARN("anim_graph_create: condition without a parameter name");
        return anim_graph_handle{};
      }
      i32 param = param_index(graph, c.param);
      if (param < 0) {
        if ((i32)graph.params.size() >= anim_max_params) {
          NJIN_WARN("anim_graph_create: more than %d parameters", anim_max_params);
          return anim_graph_handle{};
        }
        graph.params.push_back(c.param);
        graph.is_trigger.push_back(false);
        param = (i32)graph.params.size() - 1;
      }
      if (c.cmp == anim_trigger)
        graph.is_trigger[(usize)param] = true;
      out.when.push_back(anim_graph_cond{.param = param, .cmp = c.cmp, .value = c.value});
    }
    graph.transitions.push_back(std::move(out));
  }
  graph.start = desc.start != nullptr ? state_index(graph, desc.start) : 0;
  if (graph.start < 0) {
    NJIN_WARN("anim_graph_create: no start state named '%s'", desc.start);
    return anim_graph_handle{};
  }
  ctx.anim.graphs.push_back(std::move(graph));
  return anim_graph_handle{.id = (u32)ctx.anim.graphs.size()};
}

void animator_enter_clip(animator &anim, i32 clip, i32 repeat) {
  anim.clip = clip;
  anim.repeat = repeat;
  anim.step = 0;
  anim.time = 0.0f;
  anim.state_time = 0.0f;
  anim.loops = 0;
  anim.finished = false;
}

void animator_enter_state(const anim_graph &graph, const anim_sheet &sheet,
                          animator &anim, i32 state) {
  const anim_graph_state &s = graph.states[(usize)state];
  anim.state = state;
  const i32 clip_repeat = sheet.clips[(usize)s.clip].repeat;
  animator_enter_clip(anim, s.clip, s.repeat >= 0 ? s.repeat : clip_repeat);
}

bool animator_play(const njin_ctx &ctx, animator &anim, const char *name,
                   bool restart) {
  if (const anim_graph *graph = anim_graph_of(ctx.anim, anim.graph)) {
    const anim_sheet *sheet = anim_sheet_of(ctx.anim, graph->sheet);
    const i32 state = state_index(*graph, name);
    if (sheet == nullptr || state < 0)
      return false;
    if (!restart && anim.state == state && !anim.finished)
      return true;
    animator_enter_state(*graph, *sheet, anim, state);
    return true;
  }
  const anim_sheet *sheet = anim_sheet_of(ctx.anim, anim.sheet);
  const i32 clip = sheet != nullptr ? clip_index(*sheet, name) : -1;
  if (clip < 0)
    return false;
  if (!restart && anim.clip == clip && !anim.finished)
    return true;
  animator_enter_clip(anim, clip, sheet->clips[(usize)clip].repeat);
  return true;
}

void animator_set(const njin_ctx &ctx, animator &anim, const char *param,
                  f32 value) {
  const anim_graph *graph = anim_graph_of(ctx.anim, anim.graph);
  const i32 index = graph != nullptr ? param_index(*graph, param) : -1;
  if (index >= 0)
    anim.params[(usize)index] = value;
}

void animator_set_bool(const njin_ctx &ctx, animator &anim, const char *param,
                       bool value) {
  animator_set(ctx, anim, param, value ? 1.0f : 0.0f);
}

void animator_trigger(const njin_ctx &ctx, animator &anim, const char *param) {
  animator_set(ctx, anim, param, 1.0f);
}

bool animator_in(const njin_ctx &ctx, const animator &anim, const char *name) {
  return name != nullptr && std::strcmp(animator_current(ctx, anim), name) == 0;
}

const char *animator_current(const njin_ctx &ctx, const animator &anim) {
  if (const anim_graph *graph = anim_graph_of(ctx.anim, anim.graph)) {
    if (anim.state >= 0 && anim.state < (i32)graph->states.size())
      return graph->states[(usize)anim.state].name.c_str();
    return "";
  }
  const anim_sheet *sheet = anim_sheet_of(ctx.anim, anim.sheet);
  if (sheet != nullptr && anim.clip >= 0 && anim.clip < (i32)sheet->clips.size())
    return sheet->clips[(usize)anim.clip].name.c_str();
  return "";
}
} // namespace njin
