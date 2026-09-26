// njin_inspector: connects to a running njin game (njin::debug_server_start)
// over 127.0.0.1 and shows its state with Dear ImGui, in its own window.
//
//   njin_inspector [--port 7779]
//
// It retries until a game is listening, and again whenever the game restarts.
#include "imgui.h"
#include "njin_json.h"
#include "njin_net.h"
#include "raylib.h"
#include "rlImGui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using njin::json_value;

// Must match the game's debug module (modules/debug.cpp).
constexpr int protocol_version = 1;
constexpr size_t frame_history = 600;
constexpr size_t log_history = 5000;

struct collider_row {
  bool circle = false;
  float x = 0, y = 0, w = 0, h = 0;
  bool trigger = false, enabled = true;
};

struct entity_row {
  uint32_t id = 0;
  std::string label;
  std::vector<int> comps;
  bool has_pos = false;
  float x = 0, y = 0;
  bool has_col = false;
  collider_row col;
};

struct tilemap_row {
  uint32_t id = 0;
  float x = 0, y = 0, w = 0, h = 0;
  bool solid = false;
};

struct log_row {
  int level = 2;
  std::string src, msg;
};

struct app {
  // Connection.
  std::string host = "127.0.0.1";
  int port = 7779;
  njin::net_link link;
  njin::net_socket connecting;
  float retry = 0.0f;
  bool handshake = false;
  std::string game_title;
  std::string problem; // shown in the status bar (e.g. protocol mismatch)

  // Game state, as last reported.
  std::deque<float> frames;
  long long entity_count = 0;
  bool paused = false;
  float scale = 1.0f;
  std::string scene;
  bool collision_debug = false;
  long long dropped = 0;

  std::vector<std::string> types;
  std::vector<entity_row> ents;
  bool truncated = false;
  float cam[4] = {0, 0, 0, 0};
  bool has_cam = false;
  std::vector<tilemap_row> maps;

  long long selected = -1;
  json_value selected_detail;
  json_value watches;
  std::deque<log_row> logs;

  // UI.
  char entity_filter[128] = "";
  char log_filter[128] = "";
  int log_min_level = 0;
  bool log_autoscroll = true;
  float view_x = 0, view_y = 0, zoom = 1.0f; // world view camera
  bool follow_game_camera = true;
  bool fitted = false;
  std::unordered_map<std::string, float> edits; // live edit buffers, by field key
  std::string active_edit;                      // field being dragged right now
};

void send_cmd(app &a, const json_value &cmd) {
  if (a.link.connected())
    a.link.send(njin::json_dump(cmd, false));
}

void select(app &a, long long id) {
  a.selected = id;
  a.selected_detail = json_value{};
  send_cmd(a, json_value::make_object().set("cmd", "select").set("id", (double)id));
}

// --- messages from the game ---

void on_world(app &a, const json_value &m) {
  a.types.clear();
  for (const json_value &t : m["types"].items)
    a.types.push_back(t.string_or("?"));
  a.ents.clear();
  a.ents.reserve(m["ents"].size());
  for (const json_value &e : m["ents"].items) {
    entity_row r;
    r.id = (uint32_t)e["id"].number_or(0);
    r.label = e["n"].string_or("");
    for (const json_value &c : e["c"].items)
      r.comps.push_back(c.int_or(0));
    if (e["p"].size() == 2) {
      r.has_pos = true;
      r.x = e["p"][(size_t)0].f32_or(0);
      r.y = e["p"][(size_t)1].f32_or(0);
    }
    if (e["col"].is(json_value::object)) {
      const json_value &c = e["col"];
      r.has_col = true;
      r.col.circle = c["s"].int_or(0) == 1;
      r.col.x = c["b"][(size_t)0].f32_or(0);
      r.col.y = c["b"][(size_t)1].f32_or(0);
      r.col.w = c["b"][(size_t)2].f32_or(0);
      r.col.h = c["b"][(size_t)3].f32_or(0);
      r.col.trigger = c["tr"].bool_or(false);
      r.col.enabled = c["en"].bool_or(true);
    }
    a.ents.push_back(std::move(r));
  }
  a.truncated = m["truncated"].bool_or(false);
  a.has_cam = m["camera"].size() == 4;
  for (int i = 0; i < 4 && a.has_cam; i++)
    a.cam[i] = m["camera"][(size_t)i].f32_or(0);
  a.maps.clear();
  for (const json_value &t : m["tilemaps"].items) {
    tilemap_row r;
    r.id = (uint32_t)t["id"].number_or(0);
    r.x = t["b"][(size_t)0].f32_or(0);
    r.y = t["b"][(size_t)1].f32_or(0);
    r.w = t["b"][(size_t)2].f32_or(0);
    r.h = t["b"][(size_t)3].f32_or(0);
    r.solid = t["solid"].bool_or(false);
    a.maps.push_back(r);
  }
}

void on_message(app &a, const std::string &line) {
  json_value m;
  if (!njin::json_parse(line, m))
    return;
  const std::string t = m["t"].string_or("");
  if (t == "hello") {
    if (m["v"].int_or(0) != protocol_version) {
      char buf[128];
      std::snprintf(buf, sizeof buf, "game speaks protocol %d, this inspector %d: rebuild one of them",
                    m["v"].int_or(0), protocol_version);
      a.problem = buf;
      a.link.close();
      return;
    }
    a.handshake = true;
    a.problem.clear();
    a.game_title = m["title"].string_or("njin");
    a.frames.clear();
    a.fitted = false;
    if (a.selected >= 0)
      select(a, a.selected); // the game forgets the selection on reconnect
  } else if (t == "stats") {
    for (const json_value &f : m["frames"].items) {
      a.frames.push_back(f.f32_or(0));
      if (a.frames.size() > frame_history)
        a.frames.pop_front();
    }
    a.entity_count = (long long)m["entities"].number_or(0);
    a.paused = m["paused"].bool_or(false);
    a.scale = m["scale"].f32_or(1);
    a.scene = m["scene"].string_or("");
    a.collision_debug = m["collision_debug"].bool_or(false);
    a.dropped = (long long)m["dropped"].number_or(0);
  } else if (t == "world") {
    on_world(a, m);
  } else if (t == "entity") {
    if ((long long)m["id"].number_or(-1) == a.selected)
      a.selected_detail = std::move(m);
  } else if (t == "watch") {
    a.watches = m["values"];
  } else if (t == "log") {
    a.logs.push_back({m["lv"].int_or(2), m["src"].string_or(""), m["msg"].string_or("")});
    if (a.logs.size() > log_history)
      a.logs.pop_front();
  }
}

void update_connection(app &a, float dt) {
  if (a.link.connected()) {
    if (!a.link.pump()) {
      a.handshake = false;
      return;
    }
    std::string line;
    while (a.link.connected() && a.link.next(line))
      on_message(a, line);
    return;
  }
  a.handshake = false;
  if (a.connecting.valid()) {
    const int r = njin::net_connect_poll(a.connecting);
    if (r == 1) {
      a.link.sock = a.connecting;
      a.connecting = {};
    }
    return;
  }
  a.retry -= dt;
  if (a.retry <= 0.0f) {
    a.retry = 0.5f;
    a.connecting = njin::net_connect_start(a.host.c_str(), (uint16_t)a.port);
  }
}

// --- widgets ---

ImVec4 level_color(int lv) {
  switch (lv) {
  case 0: return {0.55f, 0.55f, 0.60f, 1};
  case 1: return {0.65f, 0.75f, 0.85f, 1};
  case 3: return {0.98f, 0.78f, 0.30f, 1};
  case 4:
  case 5: return {1.00f, 0.42f, 0.42f, 1};
  default: return {0.90f, 0.92f, 0.96f, 1};
  }
}

const char *level_name(int lv) {
  static const char *names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
  return lv >= 0 && lv <= 5 ? names[lv] : "?";
}

void status_bar(app &a) {
  if (!ImGui::BeginMainMenuBar())
    return;
  if (a.handshake) {
    ImGui::TextColored({0.45f, 0.85f, 0.5f, 1}, "connected");
    ImGui::Text("%s  -  %s:%d", a.game_title.c_str(), a.host.c_str(), a.port);
    if (!a.scene.empty())
      ImGui::TextDisabled("scene: %s", a.scene.c_str());
  } else if (!a.problem.empty()) {
    ImGui::TextColored({1, 0.45f, 0.45f, 1}, "%s", a.problem.c_str());
  } else {
    ImGui::TextColored({0.95f, 0.75f, 0.3f, 1}, "waiting for a game on %s:%d ...", a.host.c_str(), a.port);
    ImGui::TextDisabled("(call njin::debug_server_start in the game)");
  }
  if (a.dropped > 0)
    ImGui::TextColored({1, 0.6f, 0.3f, 1}, "%lld messages dropped", a.dropped);
  ImGui::EndMainMenuBar();
}

void performance_window(app &a) {
  ImGui::SetNextWindowPos({8, 30}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({420, 300}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Performance")) {
    ImGui::End();
    return;
  }
  std::vector<float> f(a.frames.begin(), a.frames.end());
  float avg = 0, mx = 0, mn = f.empty() ? 0 : 1e9f;
  const size_t recent = std::min<size_t>(f.size(), 60);
  for (size_t i = f.size() - recent; i < f.size(); i++) {
    avg += f[i];
    mx = std::max(mx, f[i]);
    mn = std::min(mn, f[i]);
  }
  if (recent > 0)
    avg /= (float)recent;
  ImGui::Text("FPS %.0f", avg > 0 ? 1000.0f / avg : 0.0f);
  ImGui::SameLine(110);
  ImGui::Text("frame %.2f ms  (min %.2f, max %.2f)", avg, mn, mx);
  float top = 34.0f;
  for (float v : f)
    top = std::max(top, v * 1.1f);
  ImGui::PlotLines("##frames", f.data(), (int)f.size(), 0, "frame time, ms (last 600)", 0.0f, top,
                   {ImGui::GetContentRegionAvail().x, 90});
  ImGui::Text("entities %lld", a.entity_count);

  ImGui::SeparatorText("Time");
  if (ImGui::Button(a.paused ? "Resume" : "Pause", {80, 0}))
    send_cmd(a, json_value::make_object().set("cmd", "pause").set("value", !a.paused));
  ImGui::SameLine();
  ImGui::BeginDisabled(!a.paused);
  if (ImGui::Button("Step frame"))
    send_cmd(a, json_value::make_object().set("cmd", "step"));
  ImGui::EndDisabled();
  float s = a.scale;
  ImGui::SetNextItemWidth(180);
  if (ImGui::SliderFloat("time scale", &s, 0.0f, 2.0f, "%.2fx")) {
    a.scale = s;
    send_cmd(a, json_value::make_object().set("cmd", "scale").set("value", s));
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("1x"))
    send_cmd(a, json_value::make_object().set("cmd", "scale").set("value", 1.0));

  ImGui::SeparatorText("In game");
  bool cd = a.collision_debug;
  if (ImGui::Checkbox("Draw collider outlines in the game window", &cd))
    send_cmd(a, json_value::make_object().set("cmd", "collision_debug").set("value", cd));
  ImGui::End();
}

std::string comps_text(const app &a, const entity_row &r) {
  std::string s;
  for (int c : r.comps) {
    if (!s.empty())
      s += ", ";
    s += c >= 0 && c < (int)a.types.size() ? a.types[(size_t)c] : "?";
  }
  return s;
}

bool contains_ci(const std::string &hay, const char *needle) {
  if (*needle == '\0')
    return true;
  const size_t n = std::strlen(needle);
  for (size_t i = 0; i + n <= hay.size(); i++) {
    size_t k = 0;
    while (k < n && std::tolower((unsigned char)hay[i + k]) == std::tolower((unsigned char)needle[k]))
      k++;
    if (k == n)
      return true;
  }
  return false;
}

void entities_window(app &a) {
  ImGui::SetNextWindowPos({8, 338}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({420, 520}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Entities")) {
    ImGui::End();
    return;
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", "filter: name, id or component", a.entity_filter, sizeof a.entity_filter);
  ImGui::Text("%zu shown", a.ents.size());
  if (a.truncated) {
    ImGui::SameLine();
    ImGui::TextColored({1, 0.7f, 0.3f, 1}, "(list truncated: raise debug_server_desc::max_entities)");
  }
  if (ImGui::BeginTable("ents", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 120);
    ImGui::TableSetupColumn("components");
    ImGui::TableHeadersRow();
    for (const entity_row &r : a.ents) {
      const std::string comps = comps_text(a, r);
      const std::string id = std::to_string(r.id & 0xFFFFFu);
      if (!contains_ci(r.label, a.entity_filter) && !contains_ci(comps, a.entity_filter) &&
          !contains_ci(id, a.entity_filter))
        continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID((int)r.id);
      if (ImGui::Selectable(id.c_str(), a.selected == (long long)r.id, ImGuiSelectableFlags_SpanAllColumns))
        select(a, r.id);
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.label.c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", comps.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

// A number as people read it: 1480, 0.25, 12.5, not 1.48e+03.
std::string fmt_num(double v) {
  char buf[48];
  if (v == std::floor(v) && std::fabs(v) < 1e12)
    std::snprintf(buf, sizeof buf, "%.0f", v);
  else {
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0')
      s.pop_back();
    if (!s.empty() && s.back() == '.')
      s.pop_back();
    return s;
  }
  return buf;
}

// Shows a JSON value as a compact read-only tree.
void show_value(const char *key, const json_value &v) {
  switch (v.kind) {
  case json_value::object:
    if (ImGui::TreeNodeEx(key, ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const auto &[k, child] : v.members)
        show_value(k.c_str(), child);
      ImGui::TreePop();
    }
    return;
  case json_value::array: {
    bool numbers = v.size() <= 4;
    for (const json_value &x : v.items)
      numbers = numbers && x.is(json_value::number);
    if (numbers) {
      std::string s;
      for (const json_value &x : v.items)
        s += (s.empty() ? "" : ", ") + fmt_num(x.num);
      ImGui::Text("%s: [%s]", key, s.c_str());
      return;
    }
    if (ImGui::TreeNode(key, "%s [%zu]", key, v.size())) {
      for (size_t i = 0; i < v.items.size(); i++)
        show_value(std::to_string(i).c_str(), v.items[i]);
      ImGui::TreePop();
    }
    return;
  }
  case json_value::string: ImGui::Text("%s: \"%s\"", key, v.str.c_str()); return;
  case json_value::number: ImGui::Text("%s: %s", key, fmt_num(v.num).c_str()); return;
  case json_value::boolean: ImGui::Text("%s: %s", key, v.b ? "true" : "false"); return;
  default: ImGui::TextDisabled("%s: null", key); return;
  }
}

// A float field that is edited in the game. While dragged, the inspector's
// own value is shown so the 10 Hz snapshots do not fight the drag.
bool edit_floats(app &a, const char *label, const std::string &key, float *v, int n, float speed) {
  for (int i = 0; i < n; i++) {
    const std::string k = key + std::to_string(i);
    if (a.active_edit == key)
      v[i] = a.edits[k];
    else
      a.edits[k] = v[i];
  }
  const bool changed = n == 1 ? ImGui::DragFloat(label, v, speed) : ImGui::DragFloat2(label, v, speed);
  if (ImGui::IsItemActive()) {
    a.active_edit = key;
    for (int i = 0; i < n; i++)
      a.edits[key + std::to_string(i)] = v[i];
  } else if (a.active_edit == key) {
    a.active_edit.clear();
  }
  return changed;
}

void set_field(app &a, const char *comp, const char *field, json_value value) {
  send_cmd(a, json_value::make_object()
                  .set("cmd", "set")
                  .set("id", (double)a.selected)
                  .set("comp", comp)
                  .set("field", field)
                  .set("value", std::move(value)));
}

void inspector_window(app &a) {
  ImGui::SetNextWindowPos({1032, 30}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({360, 520}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Inspector")) {
    ImGui::End();
    return;
  }
  if (a.selected < 0) {
    ImGui::TextDisabled("Select an entity in the list or the world view.");
    ImGui::End();
    return;
  }
  const json_value &d = a.selected_detail;
  const auto row = std::find_if(a.ents.begin(), a.ents.end(), [&](const entity_row &r) { return (long long)r.id == a.selected; });
  ImGui::Text("entity %lld  %s", a.selected & 0xFFFFF, row != a.ents.end() ? row->label.c_str() : "");
  if (d.is(json_value::null)) {
    ImGui::TextDisabled("waiting for the game...");
    ImGui::End();
    return;
  }
  if (!d["alive"].bool_or(false)) {
    ImGui::TextColored({1, 0.5f, 0.4f, 1}, "destroyed");
    ImGui::End();
    return;
  }
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
  if (ImGui::SmallButton("Destroy"))
    send_cmd(a, json_value::make_object().set("cmd", "destroy").set("id", (double)a.selected));
  ImGui::Separator();

  const std::string base = std::to_string(a.selected) + ":";
  for (const json_value &c : d["comps"].items) {
    const std::string name = c["name"].string_or("?");
    const json_value &v = c["value"];
    if (!ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      continue;
    ImGui::PushID(name.c_str());
    if (v.is(json_value::null)) {
      ImGui::TextDisabled("no view: register it with njin::debug_component");
    } else if (name == "transform") {
      float pos[2] = {v["pos"][(size_t)0].f32_or(0), v["pos"][(size_t)1].f32_or(0)};
      float rot = v["rot"].f32_or(0), scale = v["scale"].f32_or(1);
      if (edit_floats(a, "pos", base + "pos", pos, 2, 1.0f))
        set_field(a, "transform", "pos", json_value::make_array().push(pos[0]).push(pos[1]));
      if (edit_floats(a, "rot", base + "rot", &rot, 1, 1.0f))
        set_field(a, "transform", "rot", rot);
      if (edit_floats(a, "scale", base + "scale", &scale, 1, 0.01f))
        set_field(a, "transform", "scale", scale);
    } else {
      for (const auto &[k, field] : v.members) {
        const bool editable_bool = field.is(json_value::boolean) &&
                                   ((name == "collider" && (k == "enabled" || k == "trigger")) ||
                                    (name == "sprite" && k == "visible"));
        if (editable_bool) {
          bool b = field.b;
          if (ImGui::Checkbox(k.c_str(), &b))
            set_field(a, name.c_str(), k.c_str(), b);
        } else {
          show_value(k.c_str(), field);
        }
      }
    }
    ImGui::PopID();
  }
  ImGui::End();
}

void watches_window(app &a) {
  ImGui::SetNextWindowPos({1032, 558}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({360, 300}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Watches")) {
    ImGui::End();
    return;
  }
  if (a.watches.size() == 0)
    ImGui::TextDisabled("Nothing watched. In the game: njin::debug_watch(ctx, \"name\", value);");
  for (const auto &[k, v] : a.watches.members)
    show_value(k.c_str(), v);
  ImGui::End();
}

void log_window(app &a) {
  ImGui::SetNextWindowPos({436, 614}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({588, 244}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Log")) {
    ImGui::End();
    return;
  }
  static const char *levels[] = {"trace", "debug", "info", "warn", "error", "fatal"};
  ImGui::SetNextItemWidth(90);
  ImGui::Combo("##lv", &a.log_min_level, levels, 6);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##lf", "filter", a.log_filter, sizeof a.log_filter);
  ImGui::SameLine();
  if (ImGui::Button("Clear"))
    a.logs.clear();
  ImGui::SameLine();
  ImGui::Checkbox("follow", &a.log_autoscroll);
  ImGui::BeginChild("lines", {0, 0}, ImGuiChildFlags_Borders);
  for (const log_row &l : a.logs) {
    if (l.level < a.log_min_level)
      continue;
    if (!contains_ci(l.msg, a.log_filter) && !contains_ci(l.src, a.log_filter))
      continue;
    ImGui::TextColored(level_color(l.level), "%-5s", level_name(l.level));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", l.src.c_str());
    ImGui::SameLine();
    ImGui::TextColored(level_color(l.level), "%s", l.msg.c_str());
  }
  if (a.log_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40)
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

// Frames the whole world: entities, level bounds and the game camera.
void fit_view(app &a, ImVec2 canvas) {
  float lo_x = 1e30f, lo_y = 1e30f, hi_x = -1e30f, hi_y = -1e30f;
  const auto add = [&](float x, float y) {
    lo_x = std::min(lo_x, x);
    lo_y = std::min(lo_y, y);
    hi_x = std::max(hi_x, x);
    hi_y = std::max(hi_y, y);
  };
  for (const entity_row &r : a.ents)
    if (r.has_pos)
      add(r.x, r.y);
  for (const tilemap_row &m : a.maps) {
    add(m.x, m.y);
    add(m.x + m.w, m.y + m.h);
  }
  if (a.has_cam) {
    add(a.cam[0], a.cam[1]);
    add(a.cam[0] + a.cam[2], a.cam[1] + a.cam[3]);
  }
  if (hi_x < lo_x)
    return;
  a.view_x = (lo_x + hi_x) * 0.5f;
  a.view_y = (lo_y + hi_y) * 0.5f;
  const float w = std::max(hi_x - lo_x, 64.0f), h = std::max(hi_y - lo_y, 64.0f);
  a.zoom = std::clamp(0.9f * std::min(canvas.x / w, canvas.y / h), 0.02f, 20.0f);
}

void world_window(app &a) {
  ImGui::SetNextWindowPos({436, 30}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({588, 576}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("World")) {
    ImGui::End();
    return;
  }
  if (ImGui::Button("Fit"))
    a.fitted = false;
  ImGui::SameLine();
  ImGui::Checkbox("follow game camera", &a.follow_game_camera);
  ImGui::SameLine();
  ImGui::TextDisabled("drag: pan (right/middle)  wheel: zoom  click: select");

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  if (size.x < 50 || size.y < 50) {
    ImGui::End();
    return;
  }
  ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  ImGuiIO &io = ImGui::GetIO();

  if (!a.fitted && (!a.ents.empty() || a.has_cam)) {
    fit_view(a, size);
    a.fitted = true;
  }
  if (a.follow_game_camera && a.has_cam) {
    a.view_x = a.cam[0] + a.cam[2] * 0.5f;
    a.view_y = a.cam[1] + a.cam[3] * 0.5f;
  }
  if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
    a.follow_game_camera = false;
    a.view_x -= io.MouseDelta.x / a.zoom;
    a.view_y -= io.MouseDelta.y / a.zoom;
  }
  const ImVec2 center{p0.x + size.x * 0.5f, p0.y + size.y * 0.5f};
  const auto to_screen = [&](float x, float y) {
    return ImVec2{center.x + (x - a.view_x) * a.zoom, center.y + (y - a.view_y) * a.zoom};
  };
  if (hovered && io.MouseWheel != 0.0f) {
    // Zoom about the mouse: the world point under it stays under it.
    const float wx = a.view_x + (io.MousePos.x - center.x) / a.zoom;
    const float wy = a.view_y + (io.MousePos.y - center.y) / a.zoom;
    a.zoom = std::clamp(a.zoom * std::pow(1.15f, io.MouseWheel), 0.02f, 40.0f);
    if (!a.follow_game_camera) {
      a.view_x = wx - (io.MousePos.x - center.x) / a.zoom;
      a.view_y = wy - (io.MousePos.y - center.y) / a.zoom;
    }
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p0, {p0.x + size.x, p0.y + size.y}, true);
  dl->AddRectFilled(p0, {p0.x + size.x, p0.y + size.y}, IM_COL32(20, 22, 28, 255));
  // Grid, every 64 world units (or coarser when zoomed out).
  float step = 64.0f;
  while (step * a.zoom < 24.0f)
    step *= 4.0f;
  const float left = a.view_x - size.x * 0.5f / a.zoom, top = a.view_y - size.y * 0.5f / a.zoom;
  for (float x = std::floor(left / step) * step; x < left + size.x / a.zoom; x += step)
    dl->AddLine(to_screen(x, top), to_screen(x, top + size.y / a.zoom), IM_COL32(40, 44, 54, 255));
  for (float y = std::floor(top / step) * step; y < top + size.y / a.zoom; y += step)
    dl->AddLine(to_screen(left, y), to_screen(left + size.x / a.zoom, y), IM_COL32(40, 44, 54, 255));

  for (const tilemap_row &m : a.maps)
    dl->AddRect(to_screen(m.x, m.y), to_screen(m.x + m.w, m.y + m.h),
                m.solid ? IM_COL32(200, 90, 90, 160) : IM_COL32(110, 110, 130, 140), 0, 0, 1.5f);
  if (a.has_cam)
    dl->AddRect(to_screen(a.cam[0], a.cam[1]), to_screen(a.cam[0] + a.cam[2], a.cam[1] + a.cam[3]),
                IM_COL32(80, 150, 255, 220), 0, 0, 2.0f);

  // Entities, and the one nearest the mouse.
  const entity_row *hover = nullptr;
  float best = 10.0f * 10.0f;
  for (const entity_row &r : a.ents) {
    const bool sel = (long long)r.id == a.selected;
    if (r.has_col) {
      const collider_row &c = r.col;
      const ImU32 col = !c.enabled ? IM_COL32(130, 130, 130, 160)
                        : c.trigger ? IM_COL32(250, 210, 60, 220)
                                    : IM_COL32(80, 230, 110, 220);
      const float t = sel ? 3.0f : 1.2f;
      if (c.circle) {
        dl->AddCircle(to_screen(c.x + c.w * 0.5f, c.y + c.h * 0.5f), c.w * 0.5f * a.zoom, sel ? IM_COL32_WHITE : col, 0, t);
      } else {
        dl->AddRect(to_screen(c.x, c.y), to_screen(c.x + c.w, c.y + c.h), sel ? IM_COL32_WHITE : col, 0, 0, t);
      }
    }
    if (r.has_pos) {
      const ImVec2 s = to_screen(r.x, r.y);
      dl->AddCircleFilled(s, sel ? 4.0f : 2.5f, sel ? IM_COL32_WHITE : IM_COL32(200, 205, 220, 200));
      const float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
      if (hovered && dx * dx + dy * dy < best) {
        best = dx * dx + dy * dy;
        hover = &r;
      }
    }
  }
  if (hover != nullptr) {
    const ImVec2 s = to_screen(hover->x, hover->y);
    dl->AddCircle(s, 7.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
    ImGui::SetTooltip("%s  (entity %u)\n%s", hover->label.c_str(), hover->id & 0xFFFFFu, comps_text(a, *hover).c_str());
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      select(a, hover->id);
  }
  if (hovered) {
    const float wx = a.view_x + (io.MousePos.x - center.x) / a.zoom;
    const float wy = a.view_y + (io.MousePos.y - center.y) / a.zoom;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f, %.0f   zoom %.2f", wx, wy, a.zoom);
    dl->AddText({p0.x + 6, p0.y + size.y - 20}, IM_COL32(160, 165, 180, 255), buf);
  }
  dl->PopClipRect();
  ImGui::End();
}

// A system font with Vietnamese glyphs, so level names and log lines in the
// game's language read correctly. Falls back to ImGui's built-in font.
void load_font() {
  const char *candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                              "/System/Library/Fonts/Supplemental/Arial.ttf"};
  for (const char *path : candidates) {
    if (FileExists(path)) {
      ImGuiIO &io = ImGui::GetIO();
      // rlImGui has already added ImGui's own font, which stays the default
      // unless told otherwise.
      if (ImFont *font = io.Fonts->AddFontFromFileTTF(path, 17.0f))
        io.FontDefault = font;
      return;
    }
  }
}
} // namespace

int main(int argc, char **argv) {
  app a;
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--port") == 0)
      a.port = std::atoi(argv[i + 1]);
  }
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
  InitWindow(1400, 866, "njin inspector");
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  rlImGuiBeginInitImGui();
  ImGui::StyleColorsDark();
  load_font();
  rlImGuiEndInitImGui();
  ImGui::GetIO().IniFilename = "njin_inspector.ini"; // remembers the layout

  while (!WindowShouldClose()) {
    update_connection(a, GetFrameTime());
    BeginDrawing();
    ClearBackground(Color{14, 15, 19, 255});
    rlImGuiBegin();
    status_bar(a);
    performance_window(a);
    entities_window(a);
    world_window(a);
    inspector_window(a);
    watches_window(a);
    log_window(a);
    rlImGuiEnd();
    EndDrawing();
  }
  a.link.close();
  njin::net_close(a.connecting);
  rlImGuiShutdown();
  CloseWindow();
  return 0;
}
