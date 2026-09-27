#include "panels.h"
#include "os_open.h"
#include "util.h"
#include "imgui.h"
#include <algorithm>
#include <vector>

namespace inspector {
namespace {
constexpr int rec_fps_choices[] = {10, 15, 20, 30};
constexpr const char *rec_fps_labels[] = {"10 fps", "15 fps", "20 fps", "30 fps"};
constexpr float rec_scale_choices[] = {1.0f, 0.75f, 0.5f, 1.0f / 3.0f};
constexpr const char *rec_scale_labels[] = {"100% of the window", "75%", "50%", "33%"};

// Starts recording with the chosen settings, or stops the one running.
void toggle_recording(app &a) {
  if (!a.handshake)
    return;
  json_value cmd = json_value::make_object().set("cmd", "rec").set("value", !a.rec.on);
  if (!a.rec.on)
    cmd.set("fps", rec_fps_choices[a.rec_fps])
        .set("scale", rec_scale_choices[a.rec_scale])
        .set("max_s", a.rec_max);
  send_cmd(a, cmd);
}

void recording_section(app &a) {
  ImGui::SeparatorText("Screen recording");
  recording_state &r = a.rec;
  ImGui::BeginDisabled(!a.handshake);
  if (r.on) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.16f, 0.16f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.22f, 0.22f, 1));
  }
  if (ImGui::Button(r.on ? "Stop  (F9)" : "Record  (F9)", {110, 0}))
    toggle_recording(a);
  if (r.on)
    ImGui::PopStyleColor(2);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (r.on) {
    const int whole = (int)r.secs;
    ImGui::TextColored({1, 0.4f, 0.4f, 1}, "REC %d:%02d", whole / 60, whole % 60);
    ImGui::SameLine();
    ImGui::TextDisabled("%d frames, %s, %dx%d", r.frames, fmt_bytes((double)r.bytes).c_str(), r.width, r.height);
  } else {
    ImGui::TextDisabled("records the game window to a GIF");
  }

  // The settings apply to the next recording: the game reads them when it starts.
  ImGui::BeginDisabled(r.on);
  ImGui::SetNextItemWidth(90);
  ImGui::Combo("##recfps", &a.rec_fps, rec_fps_labels, 4);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  ImGui::Combo("##recsize", &a.rec_scale, rec_scale_labels, 4);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  ImGui::SliderFloat("##recmax", &a.rec_max, 5.0f, 120.0f, "max %.0f s");
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Recording stops by itself after this long.\nSmaller and slower means a smaller file.");

  if (!r.error.empty())
    ImGui::TextColored({1, 0.45f, 0.45f, 1}, "%s", r.error.c_str());
  if (!r.file.empty() && !r.on) {
    ImGui::TextWrapped("saved: %s", r.file.c_str());
    ImGui::TextDisabled("%d frames, %.1f s, %s", r.frames, r.secs, fmt_bytes((double)r.bytes).c_str());
    if (ImGui::SmallButton("Open folder"))
      open_folder(r.dir);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy path"))
      ImGui::SetClipboardText(r.file.c_str());
  }
}
} // namespace

void status_bar(app &a) {
  // F9 starts and stops the recording from anywhere in the inspector, except while typing in a text box.
  if (ImGui::IsKeyPressed(ImGuiKey_F9, false) && !ImGui::GetIO().WantTextInput)
    toggle_recording(a);
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
    if (!a.engine_version.empty())
      ImGui::TextDisabled("njin %s", a.engine_version.c_str());
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
  track_window_size(a);
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

  recording_section(a);

  ImGui::SeparatorText("Rendering (last frame)");
  const app::render_row &r = a.render;
  ImGui::Text("draw calls (estimated) %lld", r.batches);
  ImGui::SameLine(0, 24);
  ImGui::TextDisabled("%lld instanced, %d post passes", r.instanced, (int)r.post_passes);
  ImGui::Text("sprites %lld", r.sprites);
  ImGui::SameLine(0, 24);
  ImGui::TextDisabled("%lld culled off screen", r.sprites_culled);
  ImGui::Text("tile chunks %lld", r.tile_chunks);
  ImGui::Text("particles %lld", r.particles);
  ImGui::SameLine(0, 24);
  ImGui::TextDisabled("%lld on the GPU, %lld emitters, %lld culled", r.particles_gpu, r.emitters,
                      r.emitters_culled);

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
