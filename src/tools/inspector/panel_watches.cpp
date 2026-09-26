#include "panels.h"
#include "util.h"
#include "imgui.h"

namespace inspector {
void watches_window(app &a) {
  if (!begin_panel(a, "Watches"))
    return;
  if (a.watches.size() == 0)
    ImGui::TextDisabled("Nothing watched. In the game: njin::debug_watch(ctx, \"name\", value);");
  for (const auto &[k, v] : a.watches.members)
    show_value(k.c_str(), v);
  ImGui::End();
}
} // namespace inspector
