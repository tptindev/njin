#include "panels.h"
#include "util.h"
#include "imgui.h"
#include <algorithm>
#include <vector>

namespace inspector {
void status_bar(app &a) {
  if (!ImGui::BeginMainMenuBar())
    return;
  ImGui::TextDisabled("Layout");
  if (ImGui::SmallButton(a.layout == 0 ? "[Overview]" : "Overview")) {
    a.layout = 0;
    a.layout_dirty = true;
  }
  if (ImGui::SmallButton(a.layout == 1 ? "[Consumption]" : "Consumption")) {
    a.layout = 1;
    a.layout_dirty = true;
  }
  ImGui::TextDisabled("|");
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
  if (!begin_panel(a, "Performance"))
    return;
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
} // namespace inspector
