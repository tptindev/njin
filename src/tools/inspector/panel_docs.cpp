#include "panels.h"
#include "util.h"
#include "imgui.h"
#include <cstring>
#include <string>

namespace inspector {
namespace {
// One line of a document, markdown read lightly: headings in colour, a bullet
// for "- ", the rest wrapped.
void md_line(const std::string &line) {
  if (line.rfind("# ", 0) == 0) {
    ImGui::TextColored({0.98f, 0.78f, 0.30f, 1}, "%s", line.c_str() + 2);
    ImGui::Separator();
  } else if (line.rfind("## ", 0) == 0) {
    ImGui::Spacing();
    ImGui::TextColored({0.55f, 0.80f, 0.95f, 1}, "%s", line.c_str() + 3);
  } else if (line.rfind("- ", 0) == 0) {
    ImGui::Bullet();
    ImGui::TextWrapped("%s", line.c_str() + 2);
  } else {
    ImGui::TextWrapped("%s", line.c_str());
  }
}

std::string source_name(const app &a) {
  return a.doc_source >= 0 && a.doc_source < (int)a.doc_sources.size() ? a.doc_sources[(size_t)a.doc_source] : std::string();
}

void ask_list(app &a) {
  a.doc_items.clear();
  a.doc_list_asked = true;
  send_cmd(a, json_value::make_object().set("cmd", "doc_list").set("src", source_name(a)));
}

void ask_doc(app &a) {
  a.doc_refresh = 2.0f;
  send_cmd(a, json_value::make_object().set("cmd", "doc_get").set("src", source_name(a)).set("id", a.doc_open));
}
} // namespace

void docs_window(app &a) {
  if (!begin_panel(a, "Documents"))
    return;
  if (a.doc_sources.empty()) {
    ImGui::TextDisabled("The game has registered no documents (njin::debug_doc_source).");
    ImGui::End();
    return;
  }
  if (a.doc_source >= (int)a.doc_sources.size())
    a.doc_source = 0;
  ImGui::SetNextItemWidth(220);
  if (ImGui::BeginCombo("##source", source_name(a).c_str())) {
    for (int i = 0; i < (int)a.doc_sources.size(); i++)
      if (ImGui::Selectable(a.doc_sources[(size_t)i].c_str(), i == a.doc_source) && i != a.doc_source) {
        a.doc_source = i;
        a.doc_open.clear();
        a.doc_text.clear();
        a.doc_list_asked = false;
      }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  // Asked once a source is open, and again every few seconds while it is empty
  // (the game may still be making what it lists).
  a.doc_list_wait -= ImGui::GetIO().DeltaTime;
  if (ImGui::Button("Refresh list") || (a.handshake && (!a.doc_list_asked || (a.doc_items.empty() && a.doc_list_wait <= 0)))) {
    ask_list(a);
    a.doc_list_wait = 3.0f;
  }
  ImGui::SameLine();
  ImGui::Text("%zu documents", a.doc_items.size());
  // The open one asked again now and then, while it shows.
  if (!a.doc_open.empty()) {
    a.doc_refresh -= ImGui::GetIO().DeltaTime;
    if (a.doc_refresh <= 0)
      ask_doc(a);
  }
  const float list_w = 280;
  ImGui::BeginChild("doc_list", {list_w, 0}, ImGuiChildFlags_Borders);
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##docfilter", "filter", a.doc_filter, sizeof a.doc_filter);
  for (const auto &[id, label] : a.doc_items) {
    if (!contains_ci(label, a.doc_filter) && !contains_ci(id, a.doc_filter))
      continue;
    ImGui::PushID(id.c_str());
    if (ImGui::Selectable(label.c_str(), id == a.doc_open)) {
      a.doc_open = id;
      a.doc_text.clear();
      ask_doc(a);
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("doc_text", {0, 0}, ImGuiChildFlags_Borders);
  if (a.doc_open.empty()) {
    ImGui::TextDisabled("Pick a document on the left.");
  } else {
    if (ImGui::SmallButton("Copy"))
      ImGui::SetClipboardText(a.doc_text.c_str());
    size_t at = 0;
    while (at < a.doc_text.size()) {
      size_t end = a.doc_text.find('\n', at);
      if (end == std::string::npos)
        end = a.doc_text.size();
      const std::string line = a.doc_text.substr(at, end - at);
      at = end + 1;
      if (!line.empty())
        md_line(line);
    }
  }
  ImGui::EndChild();
  ImGui::End();
}
} // namespace inspector
