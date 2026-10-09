#include "document.h"
#include "imgui.h"
#include "mocap.h"
#include "mocap_net.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "raymath.h"
#include "rlImGui.h"
#include "rlgl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace anim_editor {
namespace {
namespace fs = std::filesystem;
fs::path u8path(const std::string &s) { return fs::path(reinterpret_cast<const char8_t *>(s.c_str())); }
std::string u8string(const fs::path &p) {
  auto s = p.u8string();
  return std::string(s.begin(), s.end());
}
std::string extension(const char *path) {
  std::string e = u8string(u8path(path).extension());
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return e;
}

// A clip's content, to tell edits apart without serialising: a model with
// dozens of clips holds 100 000+ keys, too many to dump every frame.
uint64_t clip_hash(const AnimationClip &c) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  for (char ch : c.name)
    mix((uint8_t)ch);
  uint32_t d = 0;
  std::memcpy(&d, &c.duration, 4);
  mix(d);
  mix((uint64_t)c.fps << 1 | (uint64_t)c.loop);
  static_assert(sizeof(Keyframe) % 8 == 0);
  const uint64_t *words = reinterpret_cast<const uint64_t *>(c.keys.data());
  for (size_t i = 0; i < c.keys.size() * sizeof(Keyframe) / 8; ++i)
    mix(words[i]);
  return h;
}
// Undo history: unchanged clips are shared between steps.
struct Snapshot {
  std::vector<std::shared_ptr<const AnimationClip>> clips;
  std::vector<uint64_t> hashes;
};
uint64_t combined(const std::vector<uint64_t> &hashes) {
  uint64_t h = hashes.size();
  for (uint64_t v : hashes)
    h = (h ^ v) * 1099511628211ull;
  return h;
}

struct App {
  Rig rig;
  Document doc;
  bool has_rig = false;
  Model model{};
  bool has_model = false;
  std::vector<Transform> pose_buffer;
  std::string status = "Open a skinned .glb or .gltf model (File > Open model).";
  Snapshot committed;
  uint64_t saved = 0;
  std::string saved_path, pending_path;
  std::vector<Snapshot> undo, redo;
  int selected_bone = -1;
  bool bones_visible = true, grid = true, exit = false;
  char model_path[1024] = "", path[1024] = "character.anim.json", export_path[1024] = "character.glb";
  int pending = 0; // 1 open model, 3 open project, 4 quit.
  bool request_confirm = false;
  float yaw = 0.6f, pitch = 0.2f, zoom = 6.5f;
  Vector3 target{0, 1, 0};
  Camera3D camera{};
  RenderTexture2D viewport{};
  int active_clip = -1;
  bool playing = false;
  float playhead = 0, speed = 1;
  bool reset_layout = false, timeline_focused = false;
  int gizmo_operation = 1;
  int dragging_key_bone = -1;
  float dragging_key_time = 0;
  // Webcam capture drives these bones on top of the clip while it runs.
  bool live = false;
  std::vector<BonePose> live_pose;
  std::vector<char> live_mask;

  std::vector<uint64_t> hashes() const {
    std::vector<uint64_t> h;
    h.reserve(doc.clips.size());
    for (const auto &c : doc.clips)
      h.push_back(clip_hash(c));
    return h;
  }
  bool dirty() const { return has_rig && combined(hashes()) != saved; }
  Snapshot snapshot(const std::vector<uint64_t> &h, const Snapshot *reuse) const {
    Snapshot s;
    s.hashes = h;
    for (size_t i = 0; i < doc.clips.size(); ++i) {
      std::shared_ptr<const AnimationClip> same;
      for (size_t j = 0; reuse && !same && j < reuse->hashes.size(); ++j)
        if (reuse->hashes[(i + j) % reuse->hashes.size()] == h[i])
          same = reuse->clips[(i + j) % reuse->hashes.size()];
      s.clips.push_back(same ? same : std::make_shared<const AnimationClip>(doc.clips[i]));
    }
    return s;
  }
  bool clip_active() const { return has_rig && active_clip >= 0 && active_clip < (int)doc.clips.size(); }
  std::vector<BonePose> clip_pose() const {
    if (!clip_active())
      return std::vector<BonePose>(rig.bones.size());
    return sample_animation((int)rig.bones.size(), doc.clips[active_clip], playhead);
  }
  std::vector<BonePose> pose() const {
    auto p = clip_pose();
    if (live && live_pose.size() == p.size() && live_mask.size() == p.size())
      for (size_t b = 0; b < p.size(); ++b)
        if (live_mask[b])
          p[b] = live_pose[b];
    return p;
  }
  void select_clip(int index) {
    active_clip = index;
    playhead = 0;
    playing = false;
  }
  void toggle_play() {
    if (!clip_active())
      return;
    playing = !playing;
    if (playing && playhead >= doc.clips[active_clip].duration)
      playhead = 0;
  }
  Keyframe current_key(int bone) const {
    const auto &clip = doc.clips[active_clip];
    for (const auto &k : clip.keys)
      if (k.bone == bone && std::abs(k.time - playhead) < 0.0001f)
        return k;
    const auto p = sample_animation((int)rig.bones.size(), clip, playhead)[bone];
    return {bone, playhead, p.translation, matrix_euler(QuaternionToMatrix(p.rotation))};
  }
  void key_pose(int bone) {
    if (!clip_active() || bone < 0)
      return;
    if (!set_key(doc.clips[active_clip], current_key(bone)))
      status = "Clip key limit reached (10000).";
  }
  void commit() {
    if (!has_rig)
      return;
    auto now = hashes();
    if (now == committed.hashes)
      return;
    undo.push_back(std::move(committed));
    if (undo.size() > 100)
      undo.erase(undo.begin());
    redo.clear();
    committed = snapshot(now, &undo.back());
  }
  void history(bool forward) {
    commit();
    auto &from = forward ? redo : undo;
    auto &to = forward ? undo : redo;
    if (from.empty())
      return;
    to.push_back(std::move(committed));
    committed = std::move(from.back());
    from.pop_back();
    doc.clips.clear();
    for (const auto &c : committed.clips)
      doc.clips.push_back(*c);
    playing = false;
    active_clip = std::min(active_clip, (int)doc.clips.size() - 1);
    if (active_clip >= 0)
      playhead = std::min(playhead, doc.clips[active_clip].duration);
  }
  // Puts a loaded rig in place; the GPU model only when a window exists.
  void install(Rig next, Document next_doc, const std::string &model_file) {
    rig = std::move(next);
    doc = std::move(next_doc);
    doc.model = model_file;
    has_rig = true;
    if (has_model) {
      UnloadModel(model);
      has_model = false;
    }
    if (IsWindowReady()) {
      model = LoadModel(model_file.c_str());
      has_model = model.meshCount > 0;
      if (has_model && model.skeleton.boneCount != (int)rig.bones.size()) {
        status = "Warning: raylib sees a different skeleton; the preview may not follow the bones.";
      }
    }
    pose_buffer.assign(rig.bones.size(), Transform{});
    live = false;
    live_pose.clear();
    live_mask.clear();
    std::snprintf(model_path, sizeof(model_path), "%s", model_file.c_str());
    undo.clear();
    redo.clear();
    selected_bone = -1;
    select_clip(doc.clips.empty() ? -1 : 0);
    committed = snapshot(hashes(), nullptr);
    focus();
  }
  bool open_model(const std::string &file) {
    if (extension(file.c_str()) != ".glb" && extension(file.c_str()) != ".gltf") {
      status = "Open a .glb or .gltf model.";
      return false;
    }
    Rig next;
    std::vector<AnimationClip> clips;
    std::string error;
    if (!load_rig(file, next, clips, error)) {
      status = "Open failed: " + error;
      return false;
    }
    const size_t count = clips.size();
    install(std::move(next), Document{file, std::move(clips)}, file);
    saved = combined(committed.hashes); // Nothing to lose yet: the clips are still in the model.
    saved_path.clear();
    status = "Opened " + file + ": " + std::to_string(rig.bones.size()) + " bones, " + std::to_string(count) +
             " clip(s). Save a project to keep edits, Export to write a .glb.";
    return true;
  }
  bool open_project(const std::string &file) {
    njin::json_value j;
    std::string model_file, error;
    if (!njin::json_load(file.c_str(), j) || !project_model(j, model_file, error)) {
      status = "Open failed: " + (error.empty() ? std::string("cannot read project JSON") : error);
      return false;
    }
    const fs::path resolved = u8path(model_file).is_absolute() ? u8path(model_file) : u8path(file).parent_path() / u8path(model_file);
    Rig next;
    std::vector<AnimationClip> ignored;
    Document next_doc;
    if (!load_rig(u8string(resolved), next, ignored, error) || !deserialize(j, next, next_doc, error)) {
      status = "Open failed: " + error;
      return false;
    }
    install(std::move(next), std::move(next_doc), u8string(resolved));
    saved = combined(committed.hashes);
    saved_path = file;
    status = "Opened project " + file;
    return true;
  }
  void save() {
    if (!has_rig) {
      status = "Open a model first.";
      return;
    }
    if (!path[0] || extension(path) != ".json") {
      status = "Use a .json path (for example character.anim.json) for the project.";
      return;
    }
    // The model is stored relative to the project, so the pair can move together.
    Document out = doc;
    std::error_code ec;
    const fs::path relative = fs::relative(u8path(doc.model), fs::absolute(u8path(path)).parent_path(), ec);
    if (!ec && !relative.empty()) {
      const auto generic = relative.generic_u8string();
      out.model.assign(generic.begin(), generic.end());
    }
    if (njin::json_save(path, serialize(out, rig))) {
      saved = combined(hashes());
      saved_path = path;
      status = std::string("Saved: ") + path;
    } else
      status = "Save failed. Check the path and write permissions.";
  }
  void export_model() {
    if (!has_rig) {
      status = "Open a model first.";
      return;
    }
    if (extension(export_path) != ".glb") {
      status = "Use a .glb path for the export.";
      return;
    }
    std::string error;
    std::error_code ec;
    if (fs::equivalent(u8path(export_path), u8path(doc.model), ec)) {
      status = "Export to a different file than the source model.";
      return;
    }
    status = export_glb(doc.model, rig, doc.clips, export_path, error)
                 ? std::string("Exported ") + export_path + " (" + std::to_string(doc.clips.size()) + " clips)"
                 : "Export failed: " + error;
  }
  void perform(int action) {
    if (action == 4)
      exit = true;
    else if (action == 1)
      open_model(pending_path);
    else if (action == 3)
      open_project(pending_path);
  }
  void request(int action, const std::string &file = {}) {
    playing = false;
    pending_path = file;
    if (has_rig && dirty()) {
      pending = action;
      request_confirm = true;
    } else
      perform(action);
  }
  void focus() {
    const BoundingBox b = rig.bounds;
    if (b.max.x < b.min.x)
      return;
    target = Vector3Scale(Vector3Add(b.min, b.max), 0.5f);
    zoom = std::max(0.5f, Vector3Length(Vector3Subtract(b.max, b.min)) * 1.4f);
  }
};

void draw_preview(App &a, int w, int h) {
  if (a.viewport.id == 0 || a.viewport.texture.width != w || a.viewport.texture.height != h) {
    if (a.viewport.id)
      UnloadRenderTexture(a.viewport);
    a.viewport = LoadRenderTexture(w, h);
    SetTextureFilter(a.viewport.texture, TEXTURE_FILTER_BILINEAR);
  }
  a.camera = {Vector3Add(a.target, {a.zoom * std::cos(a.pitch) * std::sin(a.yaw), a.zoom * std::sin(a.pitch),
                                    a.zoom * std::cos(a.pitch) * std::cos(a.yaw)}),
              a.target, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
  if (a.has_model) {
    // raylib skins on the CPU from model-space bone transforms: hand it the
    // editor's pose as a one-frame animation.
    auto pose = a.pose();
    auto world = bone_world(a.rig, &pose);
    for (size_t b = 0; b < world.size(); ++b)
      MatrixDecompose(world[b], &a.pose_buffer[b].translation, &a.pose_buffer[b].rotation, &a.pose_buffer[b].scale);
    ModelAnimation frame{};
    Transform *frames[1]{a.pose_buffer.data()};
    frame.boneCount = (int)a.pose_buffer.size();
    frame.keyframeCount = 1;
    frame.keyframePoses = frames;
    if (frame.boneCount == a.model.skeleton.boneCount)
      UpdateModelAnimation(a.model, frame, 0);
  }
  BeginTextureMode(a.viewport);
  ClearBackground({23, 29, 39, 255});
  BeginMode3D(a.camera);
  if (a.grid)
    DrawGrid(20, std::max(0.1f, a.zoom / 12.0f));
  if (a.has_model)
    DrawModel(a.model, {}, 1, WHITE);
  EndMode3D();
  EndTextureMode();
}
void menu(App &a) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open model (path in Project)"))
        a.request(1, a.model_path);
      if (ImGui::MenuItem("Open project (path in Project)", "Ctrl+O"))
        a.request(3, a.path);
      if (ImGui::MenuItem("Save project", "Ctrl+S", false, a.has_rig))
        a.save();
      if (ImGui::MenuItem("Export glTF (.glb)", "Ctrl+E", false, a.has_rig))
        a.export_model();
      ImGui::Separator();
      if (ImGui::MenuItem("Quit"))
        a.request(4);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !a.undo.empty()))
        a.history(false);
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !a.redo.empty()))
        a.history(true);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
      if (ImGui::MenuItem("Reset layout"))
        a.reset_layout = true;
      ImGui::EndMenu();
    }
    ImGui::TextUnformatted(a.has_rig && a.dirty() ? "  njin Animation Editor *" : "  njin Animation Editor");
    ImGui::EndMainMenuBar();
  }
  if (a.request_confirm) {
    ImGui::OpenPopup("Unsaved project");
    a.request_confirm = false;
  }
  if (ImGui::BeginPopupModal("Unsaved project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Save the project before continuing?");
    const bool needs_path = a.saved_path.empty() && a.pending != 4 && std::string(a.path) == a.pending_path;
    if (needs_path)
      ImGui::TextUnformatted("The project path is the file being opened: Cancel and choose another path first.");
    ImGui::BeginDisabled(needs_path);
    if (ImGui::Button("Save and continue")) {
      a.save();
      if (!a.dirty()) {
        a.perform(a.pending);
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard changes")) {
      a.perform(a.pending);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
void bone_tree(App &a, int parent) {
  for (int i = 0; i < (int)a.rig.bones.size(); ++i) {
    const auto &b = a.rig.bones[i];
    if (b.parent != parent)
      continue;
    ImGui::PushID(i);
    auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    bool leaf = std::none_of(a.rig.bones.begin(), a.rig.bones.end(), [&](const Bone &c) { return c.parent == i; });
    if (leaf)
      flags |= ImGuiTreeNodeFlags_Leaf;
    if (a.selected_bone == i)
      flags |= ImGuiTreeNodeFlags_Selected;
    bool opened = ImGui::TreeNodeEx("bone", flags, "%s", b.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      a.selected_bone = i;
    if (opened) {
      bone_tree(a, i);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}
void skeleton(App &a) {
  ImGui::Begin("Skeleton");
  if (!a.has_rig)
    ImGui::TextWrapped("No model. Type a .glb/.gltf path in Project and press Open model.");
  else {
    ImGui::Text("%d bones (skin joints)", (int)a.rig.bones.size());
    bone_tree(a, -1);
  }
  ImGui::End();
}
bool vector_input(const char *label, Vector3 &v, float min, float max) {
  float values[]{v.x, v.y, v.z};
  if (ImGui::DragFloat3(label, values, 0.02f, min, max, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
    v = {values[0], values[1], values[2]};
    return true;
  }
  return false;
}
void inspector(App &a) {
  ImGui::Begin("Properties");
  if (a.has_rig && a.selected_bone >= 0) {
    const Bone &b = a.rig.bones[a.selected_bone];
    ImGui::Text("BONE  %s", b.name.c_str());
    ImGui::TextDisabled("Parent: %s", b.parent < 0 ? "(root)" : a.rig.bones[b.parent].name.c_str());
    const Vector3 e = matrix_euler(QuaternionToMatrix(b.rest_rotation));
    ImGui::TextDisabled("Rest: t(%.3f, %.3f, %.3f)  r(%.1f, %.1f, %.1f)", b.rest_translation.x, b.rest_translation.y,
                        b.rest_translation.z, e.x, e.y, e.z);
    ImGui::Separator();
    if (a.clip_active()) {
      ImGui::Text("Key at %.3f s", a.playhead);
      auto key = a.current_key(a.selected_bone);
      ImGui::BeginDisabled(a.playing);
      bool changed = vector_input("Key translation", key.translation, -1e5f, 1e5f);
      changed = vector_input("Key rotation", key.rotation, -360, 360) || changed;
      if (changed && !set_key(a.doc.clips[a.active_clip], key))
        a.status = "Clip key limit reached.";
      if (ImGui::Button("Add / update keyframe"))
        a.key_pose(a.selected_bone);
      ImGui::EndDisabled();
      ImGui::TextWrapped("Translation is added to the rest position, in the parent's space; rotation is applied "
                         "after the rest rotation. Gizmo drags and these fields key the bone at the playhead.");
    } else
      ImGui::TextWrapped("Create or pick a clip in Animation to pose this bone.");
  } else
    ImGui::TextWrapped("Select a bone in Skeleton, on the timeline or by clicking its joint in the viewport.");
  ImGui::End();
}
void project(App &a) {
  ImGui::Begin("Project");
  ImGui::TextUnformatted("Source model (skinned .glb / .gltf)");
  ImGui::InputText("Model path", a.model_path, sizeof(a.model_path));
  if (ImGui::Button("Open model"))
    a.request(1, a.model_path);
  ImGui::Separator();
  ImGui::TextUnformatted("Editable project (.anim.json: model path + clips)");
  ImGui::InputText("Project path", a.path, sizeof(a.path));
  if (ImGui::Button("Open project"))
    a.request(3, a.path);
  ImGui::SameLine();
  ImGui::BeginDisabled(!a.has_rig);
  if (ImGui::Button("Save"))
    a.save();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Undo"))
    a.history(false);
  ImGui::SameLine();
  if (ImGui::Button("Redo"))
    a.history(true);
  ImGui::Separator();
  ImGui::TextUnformatted("Export: the model with every clip, for model_load()");
  ImGui::InputText("GLB path", a.export_path, sizeof(a.export_path));
  ImGui::BeginDisabled(!a.has_rig);
  if (ImGui::Button("Export glTF"))
    a.export_model();
  ImGui::EndDisabled();
  ImGui::Separator();
  ImGui::TextWrapped("%s", a.status.c_str());
  ImGui::End();
}

ImVec2 screen(Vector3 p, const App &a, ImVec2 origin, ImVec2 size) {
  Vector2 v = GetWorldToScreenEx(p, a.camera, (int)size.x, (int)size.y);
  return {origin.x + v.x, origin.y + v.y};
}
void viewport(App &a) {
  ImGui::Begin("Viewport");
  ImGui::Checkbox("Bones", &a.bones_visible);
  ImGui::SameLine();
  ImGui::Checkbox("Grid", &a.grid);
  ImGui::SameLine();
  if (ImGui::Button("Frame all (F)"))
    a.focus();
  ImGui::SameLine();
  if (ImGui::Button("Front")) {
    a.yaw = 0;
    a.pitch = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Side")) {
    a.yaw = PI / 2;
    a.pitch = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Top")) {
    a.yaw = 0;
    a.pitch = 1.55f;
  }
  ImGui::SameLine();
  ImGui::RadioButton("Move", &a.gizmo_operation, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Rotate", &a.gizmo_operation, 1);
  ImGui::TextDisabled("RMB orbit | MMB pan | Wheel zoom | Click joint | W move / E rotate | I key | Space play");
  ImVec2 origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
  size.x = std::max(1.0f, size.x);
  size.y = std::max(1.0f, size.y);
  draw_preview(a, (int)size.x, (int)size.y);
  ImGui::Image((ImTextureID)(uintptr_t)a.viewport.texture.id, size, {0, 1}, {1, 0});
  const bool hovered = ImGui::IsItemHovered();
  auto &io = ImGui::GetIO();
  if (hovered) {
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
      a.yaw -= io.MouseDelta.x * 0.008f;
      a.pitch = std::clamp(a.pitch + io.MouseDelta.y * 0.008f, -1.55f, 1.55f);
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      Vector3 forward = Vector3Normalize(Vector3Subtract(a.camera.target, a.camera.position));
      Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, {0, 1, 0}));
      Vector3 up = Vector3CrossProduct(right, forward);
      a.target = Vector3Add(a.target, Vector3Scale(Vector3Add(Vector3Scale(right, -io.MouseDelta.x),
                                                              Vector3Scale(up, io.MouseDelta.y)),
                                                   a.zoom / size.y));
    }
    a.zoom = std::clamp(a.zoom * std::exp(-io.MouseWheel * 0.12f), 0.05f, 5000.0f);
    if (!io.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_F))
        a.focus();
      if (ImGui::IsKeyPressed(ImGuiKey_W))
        a.gizmo_operation = 0;
      if (ImGui::IsKeyPressed(ImGuiKey_E))
        a.gizmo_operation = 1;
      if (ImGui::IsKeyPressed(ImGuiKey_I) && !a.playing)
        a.key_pose(a.selected_bone);
      if (ImGui::IsKeyPressed(ImGuiKey_Space))
        a.toggle_play();
    }
  }
  if (!a.has_rig) {
    ImGui::End();
    return;
  }
  auto *draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
  const auto pose = a.pose();
  const auto bones = bone_world(a.rig, &pose);
  const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  bool consumed = false;
  if (a.selected_bone >= 0 && a.clip_active()) {
    auto transform = MatrixToFloatV(bones[a.selected_bone]), view = MatrixToFloatV(GetCameraMatrix(a.camera));
    auto projection = MatrixToFloatV(MatrixPerspective(a.camera.fovy * DEG2RAD, size.x / size.y,
                                                       rlGetCullDistanceNear(), rlGetCullDistanceFar()));
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::Enable(!a.playing);
    if (ImGuizmo::Manipulate(view.v, projection.v, a.gizmo_operation == 0 ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE,
                             ImGuizmo::LOCAL, transform.v)) {
      const float *v = transform.v;
      Matrix edited{v[0], v[4], v[8], v[12], v[1], v[5], v[9], v[13], v[2], v[6], v[10], v[14], v[3], v[7], v[11], v[15]};
      Matrix local = MatrixMultiply(edited, MatrixInvert(parent_world(a.rig, a.selected_bone, &pose)));
      if (!set_key(a.doc.clips[a.active_clip], key_from_local(a.rig, a.selected_bone, a.playhead, local)))
        a.status = "Clip key limit reached.";
    }
    consumed = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
  }
  int hit_bone = -1;
  float nearest = 12;
  if (a.bones_visible)
    for (int i = 0; i < (int)bones.size(); ++i) {
      const Vector3 p{bones[i].m12, bones[i].m13, bones[i].m14};
      if (Vector3DotProduct(Vector3Subtract(p, a.camera.position), Vector3Subtract(a.camera.target, a.camera.position)) <= 0)
        continue;
      const ImVec2 at = screen(p, a, origin, size);
      const ImU32 color = i == a.selected_bone ? IM_COL32(255, 203, 85, 255) : IM_COL32(223, 235, 250, 200);
      const int parent = a.rig.bones[i].parent;
      if (parent >= 0)
        draw->AddLine(screen({bones[parent].m12, bones[parent].m13, bones[parent].m14}, a, origin, size), at,
                      IM_COL32(150, 190, 230, 200), 2);
      draw->AddCircleFilled(at, 4, color);
      const float d = std::hypot(at.x - io.MousePos.x, at.y - io.MousePos.y);
      if (d < nearest) {
        nearest = d;
        hit_bone = i;
      }
      if (i == a.selected_bone)
        draw->AddText({at.x + 8, at.y + 4}, color, a.rig.bones[i].name.c_str());
    }
  if (clicked && !consumed)
    a.selected_bone = hit_bone;
  draw->PopClipRect();
  ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
  ImGui::End();
}
void name_input(std::string &name) {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "%s", name.c_str());
  if (ImGui::InputText("Name", buffer, sizeof(buffer)))
    name = buffer;
}
void animation_panel(App &a) {
  ImGui::Begin("Animation");
  a.timeline_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
  if (!a.has_rig) {
    ImGui::TextWrapped("Open a model to animate its skeleton.");
    ImGui::End();
    return;
  }
  const char *selected = a.clip_active() ? a.doc.clips[a.active_clip].name.c_str() : "Choose a clip";
  ImGui::SetNextItemWidth(160);
  if (ImGui::BeginCombo("##clip", selected)) {
    for (int i = 0; i < (int)a.doc.clips.size(); ++i) {
      ImGui::PushID(i);
      if (ImGui::Selectable(a.doc.clips[i].name.c_str(), a.active_clip == i))
        a.select_clip(i);
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ Clip") && a.doc.clips.size() < max_clips) {
    AnimationClip c;
    c.name = "Animation " + std::to_string(a.doc.clips.size() + 1);
    a.doc.clips.push_back(std::move(c));
    a.select_clip((int)a.doc.clips.size() - 1);
  }
  if (!a.clip_active()) {
    ImGui::TextWrapped("Create a clip, select a bone, move the playhead, then rotate or move the bone with the gizmo. "
                       "Export writes the clips into a .glb for model_load().");
    ImGui::End();
    return;
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy") && a.doc.clips.size() < max_clips) {
    auto copy = a.doc.clips[a.active_clip];
    copy.name = copy.name.substr(0, 120) + " copy";
    a.doc.clips.push_back(std::move(copy));
    a.select_clip((int)a.doc.clips.size() - 1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete clip")) {
    a.doc.clips.erase(a.doc.clips.begin() + a.active_clip);
    a.select_clip(a.doc.clips.empty() ? -1 : std::min(a.active_clip, (int)a.doc.clips.size() - 1));
    ImGui::End();
    return;
  }
  auto &clip = a.doc.clips[a.active_clip];
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160);
  name_input(clip.name);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  float last = 0.05f;
  for (const auto &k : clip.keys)
    last = std::max(last, k.time);
  ImGui::DragFloat("Duration", &clip.duration, 0.05f, last, 600, "%.2fs", ImGuiSliderFlags_AlwaysClamp);
  a.playhead = std::min(a.playhead, clip.duration);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(55);
  ImGui::DragInt("FPS", &clip.fps, 1, 1, 120, "%d", ImGuiSliderFlags_AlwaysClamp);
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &clip.loop);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("Speed", &a.speed, 0.05f, 0.1f, 4, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
  if (ImGui::Button(a.playing ? "Pause" : "Play"))
    a.toggle_play();
  ImGui::SameLine();
  if (ImGui::Button("Stop")) {
    a.playing = false;
    a.playhead = 0;
  }
  auto snap = [&](float t) { return std::clamp(std::round(t * clip.fps) / clip.fps, 0.0f, clip.duration); };
  ImGui::SameLine();
  if (ImGui::Button("< Frame")) {
    a.playing = false;
    a.playhead = snap(a.playhead - 1.0f / clip.fps);
  }
  ImGui::SameLine();
  if (ImGui::Button("Frame >")) {
    a.playing = false;
    a.playhead = snap(a.playhead + 1.0f / clip.fps);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160);
  if (ImGui::SliderFloat("Time", &a.playhead, 0, clip.duration, "%.3fs")) {
    a.playing = false;
    a.playhead = snap(a.playhead);
  }
  ImGui::SameLine();
  ImGui::Text("Frame %d", (int)std::lround(a.playhead * clip.fps));
  ImGui::BeginDisabled(a.playing);
  ImGui::BeginDisabled(a.selected_bone < 0);
  if (ImGui::Button("Add key (I)"))
    a.key_pose(a.selected_bone);
  ImGui::SameLine();
  if (ImGui::Button("Delete key"))
    remove_key(clip, a.selected_bone, a.playhead);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Key all bones"))
    for (int i = 0; i < (int)a.rig.bones.size(); ++i)
      a.key_pose(i);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("Edit bone = auto key. Drag diamonds to retime.");
  if (a.timeline_focused && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space))
      a.toggle_play();
    if (ImGui::IsKeyPressed(ImGuiKey_I) && !a.playing)
      a.key_pose(a.selected_bone);
  }
  ImGui::BeginChild("Tracks", {0, 0}, ImGuiChildFlags_Borders);
  ImVec2 origin = ImGui::GetCursorScreenPos();
  float width = std::max(220.0f, ImGui::GetContentRegionAvail().x), label = 150, track = width - label - 12;
  float height = 28 + 26 * (float)a.rig.bones.size();
  ImGui::InvisibleButton("timeline", {width, std::max(height, 40.0f)});
  const bool hover = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
  auto &io = ImGui::GetIO();
  auto *draw = ImGui::GetWindowDrawList();
  auto x = [&](float time) { return origin.x + label + track * time / clip.duration; };
  for (int tick = 0; tick <= 5; ++tick) {
    float time = clip.duration * tick / 5;
    char text[24];
    std::snprintf(text, sizeof(text), "%.2f", time);
    draw->AddText({x(time) - 6, origin.y}, IM_COL32(170, 180, 200, 255), text);
    draw->AddLine({x(time), origin.y + 22}, {x(time), origin.y + height}, IM_COL32(65, 72, 85, 255));
  }
  for (int i = 0; i < (int)a.rig.bones.size(); ++i) {
    float y = origin.y + 28 + i * 26;
    if (a.selected_bone == i)
      draw->AddRectFilled({origin.x, y}, {origin.x + width, y + 25}, IM_COL32(45, 75, 105, 100));
    draw->PushClipRect({origin.x, y}, {origin.x + label - 4, y + 25}, true);
    draw->AddText({origin.x + 4, y + 3}, IM_COL32(215, 225, 235, 255), a.rig.bones[i].name.c_str());
    draw->PopClipRect();
  }
  for (const auto &k : clip.keys) {
    float px = x(k.time), py = origin.y + 28 + k.bone * 26 + 12;
    bool selected_key = k.bone == a.selected_bone && std::abs(k.time - a.playhead) < 0.0001f;
    draw->AddQuadFilled({px, py - 5}, {px + 5, py}, {px, py + 5}, {px - 5, py},
                        selected_key ? IM_COL32(255, 210, 85, 255) : IM_COL32(100, 190, 240, 255));
  }
  draw->AddLine({x(a.playhead), origin.y + 20}, {x(a.playhead), origin.y + height}, IM_COL32(255, 105, 105, 255), 2);
  if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    a.playing = false;
    a.dragging_key_bone = -1;
    int row = (int)std::floor((io.MousePos.y - origin.y - 28) / 26);
    if (row >= 0 && row < (int)a.rig.bones.size())
      a.selected_bone = row;
    if (io.MousePos.x >= origin.x + label) {
      float time = snap((io.MousePos.x - origin.x - label) / track * clip.duration);
      for (const auto &k : clip.keys)
        if (k.bone == row && std::abs(x(k.time) - io.MousePos.x) < 8) {
          a.dragging_key_bone = row;
          a.dragging_key_time = k.time;
          time = k.time;
          break;
        }
      a.playhead = time;
    }
  }
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3) && io.MousePos.x >= origin.x + label) {
    float time = snap((io.MousePos.x - origin.x - label) / track * clip.duration);
    if (a.dragging_key_bone >= 0) {
      bool collision = false;
      for (const auto &k : clip.keys)
        if (k.bone == a.dragging_key_bone && std::abs(k.time - time) < 0.0001f)
          collision = true;
      if (!collision) {
        auto key = a.current_key(a.dragging_key_bone);
        key.time = time;
        remove_key(clip, a.dragging_key_bone, a.dragging_key_time);
        set_key(clip, key);
        a.dragging_key_time = time;
        a.playhead = time;
      }
    } else
      a.playhead = time;
  }
  ImGui::EndChild();
  ImGui::End();
}
// Webcam capture: pose_stream.py (MediaPipe) sends landmarks over UDP; the
// solver turns them into rotations of the masked bones.
struct Mocap {
  MocapLink link;
  Sidecar sidecar;
  MocapRig rig_map;
  const Rig *rig_for = nullptr;
  size_t rig_bones = 0;
  MocapSmoother smoother;
  MocapFrame latest{};
  bool have = false, fresh = false;
  int camera = 0, port = 47800, preset = mask_whole, countdown = 3;
  bool hands = false, preview = true, mirror = false, drive = true, reduce = true, launched = false;
  float min_cutoff = 1.5f, beta = 0.3f, tolerance = 0.5f;
  int packets = 0;
  double rate_from = 0;
  float fps = 0, latency = -1;
  double last_packet = 0;
  enum { idle, counting, recording } state = idle;
  double record_wall = 0, reduced_at = 0;
  float record_from = 0, next_key = 0;
  int removed = 0;
  std::string status = "Stopped.";
  std::string folder = NJIN_ANIM_EDITOR_MOCAP_DIR;

  std::string python() const { return folder + "/.venv/Scripts/python.exe"; }
  bool ready() const {
    return fs::exists(u8path(python())) && fs::exists(u8path(folder + "/pose_landmarker_full.task"));
  }
  bool running() const { return link.open(); }
  void start(bool spawn) {
    std::string error;
    if (!link.listen(port, error)) {
      status = "Cannot listen: " + error;
      return;
    }
    launched = false;
    if (spawn) {
      std::vector<std::string> args{"--camera", std::to_string(camera), "--port", std::to_string(port)};
      if (hands)
        args.push_back("--hands");
      if (preview)
        args.push_back("--preview");
      if (!sidecar.start(python(), folder + "/pose_stream.py", args, folder + "/last_run.log", error)) {
        link.close();
        status = error;
        return;
      }
      launched = true;
    }
    smoother.reset();
    have = false;
    packets = 0;
    latency = -1;
    rate_from = clock_seconds();
    status = spawn ? "Starting the camera (MediaPipe loads in a few seconds)..."
                   : "Listening on port " + std::to_string(port) + ".";
  }
  void stop(App &a) {
    end_recording(a);
    sidecar.stop();
    launched = false;
    link.close();
    a.live = false;
    have = false;
    status = "Stopped.";
  }
  std::vector<char> mask(const App &a) const {
    return mocap_mask(a.rig, rig_map, (MaskPreset)preset, a.selected_bone);
  }
  void end_recording(App &a, const char *why = nullptr) {
    if (state == recording) {
      char text[200];
      if (reduce && a.clip_active())
        removed += reduce_keys(a.doc.clips[a.active_clip], mask(a), record_from, a.playhead, tolerance);
      std::snprintf(text, sizeof(text), "%s %.1f s%s", why ? why : "Recorded", a.playhead - record_from,
                    reduce ? ("; key reduction removed " + std::to_string(removed) + " keys.").c_str() : ".");
      status = text;
    }
    state = idle;
  }
  void update(App &a) {
    if (rig_for != &a.rig || rig_bones != a.rig.bones.size()) {
      rig_map = a.has_rig ? find_mocap_rig(a.rig) : MocapRig{};
      rig_for = &a.rig;
      rig_bones = a.rig.bones.size();
    }
    if (launched && !sidecar.running()) {
      launched = false;
      link.close();
      a.live = false;
      state = idle;
      status = "The capture process ended (code " + std::to_string(sidecar.exit_code()) + "): see " + folder +
               "/last_run.log";
    }
    if (!running())
      return;
    std::vector<std::vector<unsigned char>> in;
    link.receive(in);
    fresh = false;
    MocapFrame newest;
    for (const auto &b : in) {
      MocapFrame f;
      if (!parse_mocap_packet(b.data(), b.size(), f))
        continue;
      ++packets;
      last_packet = clock_seconds();
      newest = f;
      fresh = true;
    }
    const double now = clock_seconds();
    if (now - rate_from >= 1) {
      fps = (float)(packets / (now - rate_from));
      packets = 0;
      rate_from = now;
    }
    if (fresh) {
      if (newest.time > 1e9) // camera frames carry the wall clock
        latency = (float)(now - newest.time);
      smoother.set(min_cutoff, beta);
      latest = smoother.filter(newest);
      if (mirror)
        mirror_frame(latest);
      have = true;
      if (status.rfind("Starting", 0) == 0)
        status = "Camera running.";
    }
    a.live = drive && have && latest.pose && a.has_rig && rig_map.usable();
    if (a.live) {
      a.live_mask = mask(a);
      a.live_pose = a.clip_pose();
      solve_mocap(a.rig, rig_map, latest, a.live_mask, a.live_pose);
    }
    if (state == counting && now >= record_wall) {
      state = recording;
      record_wall = now;
      record_from = a.playhead;
      next_key = a.playhead;
      reduced_at = now;
      removed = 0;
      status = "Recording...";
    }
    if (state == recording && a.clip_active()) {
      auto &clip = a.doc.clips[a.active_clip];
      const float t = record_from + (float)(now - record_wall);
      a.playing = false;
      if (t > 600) {
        end_recording(a);
        return;
      }
      clip.duration = std::max(clip.duration, t);
      a.playhead = t;
      bool full = false;
      if (a.live)
        for (; next_key <= t; next_key += 1.0f / clip.fps)
          for (size_t b = 0; b < a.live_mask.size(); ++b)
            if (a.live_mask[b])
              full |= !set_key(clip, {(int)b, std::round(next_key * clip.fps) / clip.fps, a.live_pose[b].translation,
                                      matrix_euler(QuaternionToMatrix(a.live_pose[b].rotation))});
      // Reduce as it goes (all but the last frames), so long takes stay under the clip's key limit.
      if (reduce && now - reduced_at > 0.5) {
        removed += reduce_keys(clip, a.live_mask, record_from, next_key - 3.0f / clip.fps, tolerance);
        reduced_at = now;
      }
      if (full)
        end_recording(a, "Stopped at the clip's key limit (10000) after");
    }
  }
};

void mocap_window(App &a, Mocap &m) {
  ImGui::Begin("Mocap");
  if (!m.ready()) {
    ImGui::TextWrapped("Webcam capture needs Python and MediaPipe. Run once:");
    ImGui::TextWrapped("%s/setup.bat", m.folder.c_str());
  }
  ImGui::BeginDisabled(m.running());
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("Camera", &m.camera);
  m.camera = std::clamp(m.camera, 0, 16);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  ImGui::InputInt("Port", &m.port, 0);
  m.port = std::clamp(m.port, 1024, 65535);
  ImGui::Checkbox("Hands (fingers)", &m.hands);
  ImGui::SameLine();
  ImGui::Checkbox("Camera preview window", &m.preview);
  ImGui::EndDisabled();
  if (!m.running()) {
    ImGui::BeginDisabled(!m.ready() || !a.has_rig);
    if (ImGui::Button("Start camera"))
      m.start(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Listen only"))
      m.start(false);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Receive from a pose_stream.py you started yourself.");
  } else if (ImGui::Button("Stop"))
    m.stop(a);
  if (m.running()) {
    const bool body = m.have && m.latest.pose && clock_seconds() - m.last_packet < 0.5;
    char latency[32] = "";
    if (m.latency >= 0)
      std::snprintf(latency, sizeof(latency), "  %d ms", (int)(m.latency * 1000));
    ImGui::TextColored(body ? ImVec4{0.5f, 1, 0.6f, 1} : ImVec4{1, 0.7f, 0.4f, 1}, "%s  %.0f fps%s",
                       body ? "Tracking" : (m.have ? "No body in view" : "Waiting for frames"), m.fps, latency);
  }
  if (a.has_rig && !m.rig_map.usable())
    ImGui::TextColored({1, 0.5f, 0.4f, 1}, "This rig has no recognisable humanoid bones (hips, arms, legs).");
  else if (a.has_rig)
    ImGui::TextDisabled("%d humanoid bones matched", m.rig_map.found);
  ImGui::Separator();
  ImGui::Checkbox("Drive bones", &m.drive);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(190);
  ImGui::Combo("##mask", &m.preset, mask_names, mask_preset_count);
  ImGui::Checkbox("Mirror", &m.mirror);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Off: raise your right arm, the character raises its right arm.\n"
                      "On: it moves like your reflection.");
  ImGui::SetNextItemWidth(150);
  ImGui::SliderFloat("Smoothing", &m.min_cutoff, 0.2f, 5.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("One Euro filter cutoff when still: lower is smoother, higher follows small moves.");
  ImGui::SetNextItemWidth(150);
  ImGui::SliderFloat("Responsiveness", &m.beta, 0.0f, 2.0f, "%.2f");
  ImGui::Separator();
  if (!a.clip_active())
    ImGui::TextWrapped("Choose or create a clip in Animation to record into.");
  ImGui::BeginDisabled(!a.clip_active() || !m.running());
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("Countdown (s)", &m.countdown);
  m.countdown = std::clamp(m.countdown, 0, 10);
  ImGui::Checkbox("Reduce keys", &m.reduce);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  ImGui::SliderFloat("Tolerance", &m.tolerance, 0.05f, 5.0f, "%.2f deg");
  if (m.state == Mocap::idle) {
    if (ImGui::Button("Record from playhead")) {
      a.playing = false;
      m.state = Mocap::counting;
      m.record_wall = clock_seconds() + m.countdown;
    }
  } else {
    if (ImGui::Button("Stop recording"))
      m.end_recording(a);
    ImGui::SameLine();
    if (m.state == Mocap::counting)
      ImGui::TextColored({1, 0.8f, 0.3f, 1}, "Recording in %d...", (int)std::ceil(m.record_wall - clock_seconds()));
    else
      ImGui::TextColored({1, 0.35f, 0.35f, 1}, "REC %.1f s", a.playhead - m.record_from);
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped("%s", m.status.c_str());
  ImGui::End();
}

void dock_layout(App &a) {
  ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  if (ImGui::DockBuilderGetNode(dock)->ChildNodes[0] && !a.reset_layout)
    return;
  a.reset_layout = false;
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
  ImGuiID center = dock, left, right, bottom, project;
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, &left, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, &right, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.38f, &bottom, &center);
  ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.50f, &project, &right);
  ImGui::DockBuilderDockWindow("Skeleton", left);
  ImGui::DockBuilderDockWindow("Properties", right);
  ImGui::DockBuilderDockWindow("Viewport", center);
  ImGui::DockBuilderDockWindow("Project", project);
  ImGui::DockBuilderDockWindow("Mocap", project);
  ImGui::DockBuilderDockWindow("Animation", bottom);
  ImGui::DockBuilderFinish(dock);
}

// Editor state without a window: open, key, undo/redo, save, reopen, guard.
int app_self_test() {
  const std::string folder = temp_folder("app");
  const std::string model = write_test_model(folder);
  App a;
  bool ok = a.open_model(model) && a.rig.bones.size() == 3 && a.doc.clips.size() == 1 && !a.dirty();
  auto step = [&](const char *what) {
    if (!ok)
      std::fprintf(stderr, "app self-test failed at: %s (%s)\n", what, a.status.c_str());
    return ok;
  };
  step("open");
  a.doc.clips.push_back(wave_clip());
  a.select_clip(1);
  a.commit();
  a.selected_bone = 1;
  a.playhead = 0.25f;
  a.key_pose(1);
  a.commit();
  ok = ok && a.doc.clips[1].keys.size() == 6;
  step("key");
  a.history(false);
  ok = ok && a.doc.clips[1].keys.size() == 5;
  step("undo");
  a.history(true);
  ok = ok && a.doc.clips[1].keys.size() == 6;
  step("redo");
  const std::string project = u8string(u8path(folder) / "sub" / "walk.anim.json");
  fs::create_directories(u8path(folder) / "sub");
  std::snprintf(a.path, sizeof(a.path), "%s", project.c_str());
  a.save();
  ok = ok && !a.dirty();
  step("save");
  njin::json_value saved_json;
  ok = ok && njin::json_load(project.c_str(), saved_json) && saved_json["model"].str == "../column.gltf";
  step("relative model path");
  a.doc.clips[1].loop = false;
  a.save();
  ok = ok && !a.dirty();
  step("overwrite");
  App b;
  ok = ok && b.open_project(project) && b.doc.clips.size() == 2 && !b.doc.clips[1].loop &&
       b.doc.clips[1].keys.size() == 6 && fs::equivalent(u8path(b.doc.model), u8path(model));
  if (!step("reopen"))
    std::fprintf(stderr, "  reopened: %s\n", b.status.c_str());
  b.doc.clips[1].fps = 12;
  b.request(1, model);
  ok = ok && b.pending == 1 && b.request_confirm && b.doc.clips[1].fps == 12;
  step("unsaved guard");
  std::snprintf(b.export_path, sizeof(b.export_path), "%s", u8string(u8path(folder) / "out.glb").c_str());
  b.export_model();
  ok = ok && fs::exists(u8path(folder) / "out.glb");
  if (!step("export"))
    std::fprintf(stderr, "  export: %s\n", b.status.c_str());
  std::error_code ec;
  fs::remove_all(u8path(folder), ec);
  if (!ok) {
    std::fprintf(stderr, "FAIL: editor open/undo/redo/save/reopen/export checks\n");
    return 1;
  }
  std::printf("PASS: editor open model, keyframe history, save relative path/overwrite/reopen, unsaved guard, export\n");
  return 0;
}
} // namespace
} // namespace anim_editor

int main(int argc, char **argv) {
  using namespace anim_editor;
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "--self-test")
    return self_test() || app_self_test() || mocap_unit_test();
  if (mode == "--mocap-test")
    return mocap_test(argc, argv);
  if (mode == "--roundtrip-test")
    return roundtrip_test(argc > 2 ? argv[2] : nullptr);
  // --mocap-smoke <model> <frames folder>: pose_stream.py on rendered frames
  // drives the model over UDP, records a second, checks undo, screenshots.
  const bool mocap_smoke = mode == "--mocap-smoke" && argc > 3;
  const bool smoke = mode == "--smoke-test" || mocap_smoke;
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(1600, 960, "njin Animation Editor");
  SetWindowMinSize(1000, 700);
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  rlImGuiBeginInitImGui();
  ImGui::StyleColorsDark();
  if (FileExists("C:/Windows/Fonts/segoeui.ttf"))
    if (auto *font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 17))
      ImGui::GetIO().FontDefault = font;
  rlImGuiEndInitImGui();
  auto &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.IniFilename = smoke ? nullptr : "njin_anim_editor.ini";
  ImGui::GetStyle().FrameRounding = 4;
  ImGui::GetStyle().GrabRounding = 4;
  App app;
  Mocap mocap;
  std::string smoke_folder;
  if (mocap_smoke) {
    if (!app.open_model(argv[2])) {
      std::fprintf(stderr, "Mocap smoke: %s\n", app.status.c_str());
      return 1;
    }
    AnimationClip c;
    c.name = "Mocap";
    c.duration = 0.5f;
    app.doc.clips.push_back(c);
    app.select_clip((int)app.doc.clips.size() - 1);
    app.commit();
    mocap.port = 47877;
    mocap.start(false);
    std::string error;
    mocap.launched = mocap.sidecar.start(mocap.python(), mocap.folder + "/pose_stream.py",
                                         {"--frames", argv[3], "--fps", "30", "--port", std::to_string(mocap.port)},
                                         mocap.folder + "/last_run.log", error);
    if (!mocap.launched) {
      std::fprintf(stderr, "Mocap smoke: %s\n", error.c_str());
      return 1;
    }
  } else if (smoke) {
    // The given model, or the generated test column; a key on its middle bone.
    smoke_folder = temp_folder("smoke");
    const std::string model = argc > 2 ? argv[2] : write_test_model(smoke_folder);
    if (!app.open_model(model)) {
      std::fprintf(stderr, "Smoke: %s\n", app.status.c_str());
      return 1;
    }
    AnimationClip c;
    c.name = "Smoke";
    app.doc.clips.push_back(c);
    app.select_clip((int)app.doc.clips.size() - 1);
    app.selected_bone = std::min(1, (int)app.rig.bones.size() - 1);
  } else if (argc > 1) {
    const std::string file = argv[1];
    if (extension(file.c_str()) == ".json") {
      std::snprintf(app.path, sizeof(app.path), "%s", file.c_str());
      app.open_project(file);
    } else
      app.open_model(file);
  }
  int frames = 0;
  bool smoke_ok = false;
  Image rest{};
  while (!app.exit) {
    if (WindowShouldClose())
      app.request(4);
    mocap.update(app);
    if (app.clip_active() && mocap.state != Mocap::recording)
      app.playhead = advance_animation(app.playhead, GetFrameTime() * app.speed, app.doc.clips[app.active_clip], app.playing);
    BeginDrawing();
    ClearBackground({20, 24, 31, 255});
    rlImGuiBegin();
    ImGuizmo::BeginFrame();
    menu(app);
    dock_layout(app);
    skeleton(app);
    animation_panel(app);
    inspector(app);
    project(app);
    viewport(app);
    mocap_window(app, mocap);
    if (!io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        app.save();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
        app.request(3, app.path);
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E))
        app.export_model();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        app.history(false);
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
        app.history(true);
      if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && app.timeline_focused && app.clip_active() && !app.playing)
        remove_key(app.doc.clips[app.active_clip], app.selected_bone, app.playhead);
    }
    if (!ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() && mocap.state != Mocap::recording)
      app.commit(); // a whole recording is one undo step
    rlImGuiEnd();
    EndDrawing();
    ++frames;
    if (mocap_smoke) {
      static double started = clock_seconds(), live_at = 0;
      static size_t undo_before = 0;
      static int phase = 0;
      const double now = clock_seconds();
      if (phase == 0 && app.live) {
        live_at = now;
        undo_before = app.undo.size();
        mocap.state = Mocap::counting;
        mocap.record_wall = now;
        phase = 1;
      }
      if (phase == 1 && now - live_at > 0.7) {
        Image shot = LoadImageFromScreen();
        ExportImage(shot, "build/anim_editor_mocap.png");
        UnloadImage(shot);
        phase = 2;
      }
      if (phase == 2 && now - live_at > 1.1) {
        mocap.end_recording(app);
        phase = 3;
      } else if (phase == 3) {
        const size_t keys = app.doc.clips[app.active_clip].keys.size();
        const bool one_step = app.undo.size() == undo_before + 1;
        app.history(false);
        const bool undone = app.doc.clips[app.active_clip].keys.empty();
        app.history(true);
        const bool redone = app.doc.clips[app.active_clip].keys.size() == keys;
        smoke_ok = keys > 0 && one_step && undone && redone;
        std::printf("%s: mocap smoke: live after %.1f s at %.0f fps, recorded %d keys over %.2f s (%s), "
                    "one undo step %s, undo/redo %s; screenshot build/anim_editor_mocap.png\n",
                    smoke_ok ? "PASS" : "FAIL", live_at - started, mocap.fps, (int)keys,
                    app.doc.clips[app.active_clip].duration, mocap.status.c_str(), one_step ? "yes" : "no",
                    undone && redone ? "ok" : "broken");
        app.exit = true;
      }
      if (phase == 0 && now - started > 30) {
        std::printf("FAIL: mocap smoke: no live pose after 30 s (%s)\n", mocap.status.c_str());
        app.exit = true;
      }
    }
    if (smoke && !mocap_smoke && frames == 30) {
      rest = LoadImageFromTexture(app.viewport.texture);
      // Bend the selected bone 60 degrees at the playhead, as a gizmo drag would.
      set_key(app.doc.clips[app.active_clip], {app.selected_bone, 0, {}, {0, 0, 60}});
    }
    if (smoke && !mocap_smoke && frames == 60) {
      Image posed = LoadImageFromTexture(app.viewport.texture);
      int changed = 0;
      if (rest.width == posed.width && rest.height == posed.height) {
        Color *before = LoadImageColors(rest), *after = LoadImageColors(posed);
        for (int i = 0; i < posed.width * posed.height; ++i)
          changed += std::abs((int)before[i].r - after[i].r) + std::abs((int)before[i].g - after[i].g) +
                         std::abs((int)before[i].b - after[i].b) >
                     30;
        UnloadImageColors(before);
        UnloadImageColors(after);
      }
      UnloadImage(posed);
      Image shot = LoadImageFromScreen();
      smoke_ok = ExportImage(shot, "build/anim_editor_smoke.png") && changed > 100;
      UnloadImage(shot);
      std::printf("%s: smoke, %d bones, keyed bone moved %d pixels, screenshot build/anim_editor_smoke.png\n",
                  smoke_ok ? "PASS" : "FAIL", (int)app.rig.bones.size(), changed);
      app.exit = true;
    }
  }
  mocap.stop(app);
  if (rest.data)
    UnloadImage(rest);
  if (app.has_model)
    UnloadModel(app.model);
  if (app.viewport.id)
    UnloadRenderTexture(app.viewport);
  rlImGuiShutdown();
  CloseWindow();
  if (!smoke_folder.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(smoke_folder, ec);
  }
  return smoke && !smoke_ok ? 1 : 0;
}
