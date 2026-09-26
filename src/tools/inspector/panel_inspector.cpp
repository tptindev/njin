#include "panels.h"
#include "util.h"
#include "imgui.h"
#include <algorithm>
#include <string>

namespace inspector {
namespace {
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
} // namespace

void inspector_window(app &a) {
  if (!begin_panel(a, "Inspector"))
    return;
  if (a.selected < 0) {
    ImGui::TextDisabled("Select an entity in the list or the world view.");
    ImGui::End();
    return;
  }
  const json_value &d = a.selected_detail;
  const auto row = std::find_if(a.ents.begin(), a.ents.end(), [&](const entity_row &r) { return (long long)r.id == a.selected; });
  ImGui::Text("entity %lld  %s", a.selected & 0xFFFFF, row != a.ents.end() ? row->label.c_str() : "");
  if (row != a.ents.end() && row->ram > 0) {
    ImGui::TextDisabled("holds %s of RAM%s%s", fmt_bytes((double)row->ram).c_str(),
                        row->gpu_mem > 0 ? ", and GPU images of " : "",
                        row->gpu_mem > 0 ? fmt_bytes((double)row->gpu_mem).c_str() : "");
  }
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
} // namespace inspector
