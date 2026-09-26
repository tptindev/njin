// "Process": what the game costs the machine, as the operating system sees it.
#include "imgui.h"
#include "implot.h"
#include "panels.h"
#include "util.h"
#include <algorithm>
#include <cstdio>

namespace inspector {
namespace {
constexpr double window_seconds = 120.0;

// One graph: a line and a light fill under it, over the last two minutes.
void plot(const app &a, const char *id, const char *unit, const std::vector<double> &y, double fixed_max,
          ImVec4 color, double scale = 1.0) {
  const usage_history &h = a.usage;
  if (!ImPlot::BeginPlot(id, {-1, -1}, ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText))
    return;
  ImPlot::SetupAxes(nullptr, unit, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                    fixed_max > 0 ? 0 : ImPlotAxisFlags_AutoFit);
  ImPlot::SetupAxisLimits(ImAxis_X1, a.clock - window_seconds, a.clock, ImPlotCond_Always);
  if (fixed_max > 0)
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, fixed_max, ImPlotCond_Always);
  else
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, 1e15);
  if (!h.t.empty()) {
    std::vector<double> scaled(y.size());
    for (size_t i = 0; i < y.size(); i++)
      scaled[i] = y[i] * scale;
    ImPlot::PlotShaded("##fill", h.t.data(), scaled.data(), (int)h.t.size(), 0.0,
                       ImPlotSpec(ImPlotProp_FillColor, color, ImPlotProp_FillAlpha, 0.25f));
    ImPlot::PlotLine("##line", h.t.data(), scaled.data(), (int)h.t.size(),
                     ImPlotSpec(ImPlotProp_LineColor, color, ImPlotProp_LineWeight, 2.0f));
  }
  ImPlot::EndPlot();
}

void gauge(const char *label, double fraction, const std::string &text, ImVec4 color) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine(96);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
  ImGui::ProgressBar((float)std::clamp(fraction, 0.0, 1.0), {-1, 16}, text.c_str());
  ImGui::PopStyleColor();
}
} // namespace

void monitor_window(app &a) {
  if (!begin_panel(a, "Process"))
    return;
  const proc_sample &s = a.mon.last;
  if (!a.handshake || a.game_pid == 0) {
    ImGui::TextDisabled("Waiting for a game...");
    ImGui::End();
    return;
  }
  if (!s.valid) {
    ImGui::TextDisabled("Cannot read process %u (is it on this machine?)", (unsigned)a.game_pid);
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("process %u  -  %d logical cores", (unsigned)a.game_pid, a.mon.cores);

  char text[96];
  std::snprintf(text, sizeof text, "%.1f %%   (%.0f %% of one core)", s.cpu_percent, s.cpu_core_percent);
  gauge("CPU", s.cpu_percent / 100.0, text, {0.35f, 0.65f, 1.0f, 1});
  const std::string ws = fmt_bytes(s.ram_working_set);
  const std::string priv = fmt_bytes(s.ram_private);
  const std::string peak = fmt_bytes(s.ram_peak);
  std::snprintf(text, sizeof text, "%s   private %s   peak %s", ws.c_str(), priv.c_str(), peak.c_str());
  // RAM has no natural 100 %: show it against its own peak.
  gauge("RAM", s.ram_peak > 0 ? s.ram_working_set / s.ram_peak : 0.0, text, {0.45f, 0.85f, 0.5f, 1});
  if (s.gpu_valid) {
    std::snprintf(text, sizeof text, "%.1f %%   (3D %.1f %%, copy %.1f %%)", s.gpu_percent, s.gpu_3d_percent,
                  s.gpu_copy_percent);
    gauge("GPU", s.gpu_percent / 100.0, text, {0.95f, 0.65f, 0.3f, 1});
    const std::string vram = fmt_bytes(s.gpu_dedicated);
    const std::string shared = fmt_bytes(s.gpu_shared);
    std::snprintf(text, sizeof text, "%s video memory   +%s shared", vram.c_str(), shared.c_str());
    ImGui::TextUnformatted("VRAM");
    ImGui::SameLine(96);
    ImGui::TextUnformatted(text);
  } else {
#if defined(_WIN32)
    ImGui::TextDisabled("GPU counters are not available yet (they need Windows 10 1709+ and a WDDM driver).");
#else
    ImGui::TextDisabled("GPU figures are only read on Windows. See the Assets window for the game's own GPU estimate.");
#endif
  }

  ImGui::SeparatorText("Last two minutes");
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (ImPlot::BeginSubplots("##graphs", 2, 2, avail, ImPlotSubplotFlags_NoTitle | ImPlotSubplotFlags_LinkAllX)) {
    plot(a, "CPU (% of machine)", "CPU %", a.usage.cpu, 100.0, {0.35f, 0.65f, 1.0f, 1});
    plot(a, "RAM (MB)", "RAM MB", a.usage.ram, 0, {0.45f, 0.85f, 0.5f, 1}, 1.0 / (1024.0 * 1024.0));
    plot(a, "GPU (%)", "GPU %", a.usage.gpu, 100.0, {0.95f, 0.65f, 0.3f, 1});
    plot(a, "VRAM (MB)", "VRAM MB", a.usage.vram, 0, {0.85f, 0.45f, 0.85f, 1}, 1.0 / (1024.0 * 1024.0));
    ImPlot::EndSubplots();
  }
  ImGui::End();
}
} // namespace inspector
