#include "document.h"
#include "live_preview.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "raymath.h"
#include "rlImGui.h"
#include "rlgl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>

namespace model_editor {
namespace {
const char *kinds[] = {"Sphere", "Box", "Capsule", "Cylinder", "Torus"};
const char *operations[] = {"Union", "Smooth union", "Subtract", "Intersect"};
std::string dump(const Document &d) { return njin::json_dump(serialize(d), false); }
struct App {
  Document doc = humanoid();
  std::string committed = dump(doc), saved, status = "Ready. Start from the character or File > New.";
  std::string saved_path, pending_open_path;
  std::vector<std::string> undo, redo;
  int selected_shape = 0, selected_bone = -1;
  bool posed = false, bones_visible = true, wire = false, grid = true, exit = false;
  int resolution = 40, export_resolution = 64;
  char path[1024] = "character.model.json", export_path[1024] = "character.obj";
  int pending = 0; // New, humanoid, open, quit.
  bool request_confirm = false;
  float yaw = 0.6f, pitch = 0.2f, zoom = 6.5f;
  Vector3 target{0,1.3f,0};
  Camera3D camera{};
  RenderTexture2D viewport{};
  Model model{};
  bool has_model = false;
  LivePreview live;
  int active_clip = -1;
  bool animation_enabled = false, playing = false;
  float playhead = 0, speed = 1;
  int preview_quality = 1; // Full quality by default; half resolution is available for slower GPUs.
  bool reset_layout = false;
  bool timeline_focused = false;
  int gizmo_operation = 0;
  int dragging_key_bone = -1;
  float dragging_key_time = 0;
  bool animation_active() const { return animation_enabled && active_clip>=0 && active_clip<(int)doc.clips.size(); }
  std::vector<Matrix> displayed_bones() const {
    if (!animation_active()) return bone_matrices(doc,posed);
    auto pose=sample_animation(doc,doc.clips[active_clip],playhead);
    return bone_matrices(doc,true,&pose);
  }
  void select_clip(int index) { active_clip=index; playhead=0; playing=false; animation_enabled=index>=0; }
  void toggle_play() {
    if (!live.valid() || active_clip<0 || active_clip>=(int)doc.clips.size()) return;
    animation_enabled=true; playing=!playing;
    if (playing && playhead>=doc.clips[active_clip].duration) playhead=0;
  }
  Keyframe current_key(int bone) const {
    const auto &clip=doc.clips[active_clip];
    for (const auto &k:clip.keys) if (k.bone==bone && std::abs(k.time-playhead)<0.0001f) return k;
    const auto pose=sample_animation(doc,clip,playhead)[bone];
    return {bone,playhead,pose.translation,matrix_euler(QuaternionToMatrix(pose.rotation))};
  }
  void key_pose(int bone) {
    if (!animation_active() || bone<0) return;
    if (!set_key(doc.clips[active_clip],current_key(bone))) status="Clip key limit reached (10000).";
  }
  std::future<Surface> worker;
  std::string wanted, building, installed;
  Surface surface;
  double changed_at = 0;
  bool dirty() const { return dump(doc) != saved; }
  void selection_clear() { selected_shape = selected_bone = -1; }
  void commit() {
    auto now = dump(doc);
    if (now == committed) return;
    undo.push_back(committed);
    if (undo.size() > 100) undo.erase(undo.begin());
    redo.clear(); committed = std::move(now);
  }
  void history(bool forward) {
    commit();
    auto &from = forward ? redo : undo;
    auto &to = forward ? undo : redo;
    if (from.empty()) return;
    njin::json_value j; std::string error;
    if (njin::json_parse(from.back(),j) && deserialize(j,doc,error)) {
      to.push_back(committed); committed=from.back(); from.pop_back(); selection_clear();
      playing=false; active_clip=std::min(active_clip,(int)doc.clips.size()-1);
      if (active_clip>=0) playhead=std::min(playhead,doc.clips[active_clip].duration);
    }
  }
  void save() {
    if (!path[0]) { status="Enter a project path first."; return; }
    if (std::filesystem::path(reinterpret_cast<const char8_t*>(path)).extension()!=".json") {
      status="Use a .json extension for the editable project."; return;
    }
    if (njin::json_save(path, serialize(doc))) { saved=dump(doc); saved_path=path; status=std::string("Saved: ")+path; }
    else status="Save failed. Check the path and write permissions.";
  }
  void perform(int action) {
    if (action==4) { exit=true; return; }
    if (action==3) {
      njin::json_value j; Document next; std::string error;
      if (!njin::json_load(path,j) || !deserialize(j,next,error)) {
        status="Open failed: "+(error.empty()?std::string("cannot read project JSON"):error); return;
      }
      doc=std::move(next); saved=dump(doc); saved_path=path; status=std::string("Opened: ")+path;
    } else {
      doc=action==2 ? humanoid() : Document{}; saved.clear(); saved_path.clear();
      status="New project. Choose a path and Save.";
    }
    undo.clear(); redo.clear(); committed=dump(doc); selection_clear();
    select_clip(doc.clips.empty() ? -1 : 0);
  }
  void request(int action) {
    playing=false;
    if (action==3) pending_open_path=path;
    if (dirty()) { pending=action; request_confirm=true; }
    else perform(action);
  }
  void remove() {
    if (selected_shape>=0) {
      doc.shapes.erase(doc.shapes.begin()+selected_shape); selected_shape=-1;
    } else if (selected_bone>=0) {
      remove_bone(doc,selected_bone); selected_bone=-1;
      status="Removed bone subtree. Attached shapes kept in their rest-world positions.";
    }
  }
  void duplicate() {
    if (selected_shape<0 || doc.shapes.size()>=128) return;
    Shape s=doc.shapes[selected_shape]; s.name=s.name.substr(0,115)+" copy"; s.position.x+=0.2f;
    doc.shapes.push_back(s); selected_shape=(int)doc.shapes.size()-1;
  }
  void focus() {
    auto matrices=displayed_bones(); Field f(doc,posed,&matrices); target=Vector3Scale(Vector3Add(f.low,f.high),0.5f);
    zoom=std::max(1.0f,Vector3Length(Vector3Subtract(f.high,f.low))*1.15f);
  }
};

void upload(App &a, Surface next) {
  if (a.has_model) { UnloadModel(a.model); a.has_model=false; }
  a.surface=std::move(next);
  if (a.surface.vertices.empty()) return;
  Mesh mesh{}; mesh.vertexCount=(int)a.surface.vertices.size(); mesh.triangleCount=mesh.vertexCount/3;
  size_t bytes=a.surface.vertices.size()*sizeof(Vector3);
  mesh.vertices=(float*)MemAlloc((unsigned int)bytes);
  mesh.normals=(float*)MemAlloc((unsigned int)bytes);
  mesh.colors=(unsigned char*)MemAlloc((unsigned int)mesh.vertexCount*4);
  std::memcpy(mesh.vertices,a.surface.vertices.data(),bytes);
  std::memcpy(mesh.normals,a.surface.normals.data(),bytes);
  Vector3 light=Vector3Normalize({-0.6f,1,0.8f});
  for (int i=0;i<mesh.vertexCount;++i) {
    float shade=0.3f+0.7f*std::max(0.0f,Vector3DotProduct(a.surface.normals[i],light));
    mesh.colors[4*i]=mesh.colors[4*i+1]=mesh.colors[4*i+2]=(unsigned char)(shade*255);
    mesh.colors[4*i+3]=255;
  }
  UploadMesh(&mesh,false); a.model=LoadModelFromMesh(mesh); a.has_model=true;
}
void update_surface(App &a) {
  // Animated SDFs are rendered directly by LivePreview; only wire mode needs meshing.
  if (a.live.valid() && (!a.wire || a.animation_active())) return;
  auto key=dump(a.doc)+std::to_string(a.posed)+":"+std::to_string(a.resolution);
  if (key!=a.wanted) { a.wanted=key; a.changed_at=GetTime(); }
  if (a.worker.valid() && a.worker.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
    try {
      auto next=a.worker.get();
      if (a.building==a.wanted) { upload(a,std::move(next)); a.installed=a.building; }
    } catch (const std::exception &e) { a.status=std::string("Preview failed: ")+e.what(); a.installed=a.building; }
  }
  if (!a.worker.valid() && a.installed!=a.wanted && GetTime()-a.changed_at>0.12) {
    a.building=a.wanted;
    a.worker=std::async(std::launch::async,[doc=a.doc,pose=a.posed,n=a.resolution] { return triangulate(Field(doc,pose),n); });
  }
}
void draw_preview(App &a, int w,int h) {
  if (a.live.valid() && (!a.wire || a.animation_active()) && a.preview_quality==0) {
    w=std::max(1,w/2); h=std::max(1,h/2);
  }
  if (a.viewport.id==0 || a.viewport.texture.width!=w || a.viewport.texture.height!=h) {
    if (a.viewport.id) UnloadRenderTexture(a.viewport);
    a.viewport=LoadRenderTexture(w,h);
    SetTextureFilter(a.viewport.texture,TEXTURE_FILTER_BILINEAR);
  }
  a.camera={Vector3Add(a.target,{a.zoom*std::cos(a.pitch)*std::sin(a.yaw),a.zoom*std::sin(a.pitch),
    a.zoom*std::cos(a.pitch)*std::cos(a.yaw)}), a.target,{0,1,0},45,CAMERA_PERSPECTIVE};
  BeginTextureMode(a.viewport);
  ClearBackground({23,29,39,255});
  BeginMode3D(a.camera);
  if (a.grid) DrawGrid(20,0.5f);
  bool use_live=a.live.valid() && (!a.wire || a.animation_active());
  if (!use_live && a.has_model) {
    Color color=ColorFromNormalized({a.doc.color.x,a.doc.color.y,a.doc.color.z,1});
    DrawModel(a.model,{},1,color);
    if (a.wire) DrawModelWires(a.model,{},1,{40,60,75,100});
  }
  EndMode3D();
  if (use_live) {
    auto matrices=a.displayed_bones();
    a.live.draw(Field(a.doc,a.posed,&matrices),a.camera,w,h,a.doc.color);
  }
  EndTextureMode();
}
void name_input(std::string &name) {
  char buffer[128]; std::snprintf(buffer,sizeof(buffer),"%s",name.c_str());
  if (ImGui::InputText("Name",buffer,sizeof(buffer))) name=buffer;
}
bool vector_input(const char *label,Vector3 &v,float min=-100,float max=100) {
  float values[]={v.x,v.y,v.z};
  if (ImGui::DragFloat3(label,values,0.02f,min,max,"%.3f",ImGuiSliderFlags_AlwaysClamp)) {
    v={values[0],values[1],values[2]}; return true;
  }
  return false;
}
void menu(App &a) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New empty model")) a.request(1);
      if (ImGui::MenuItem("New humanoid template")) a.request(2);
      if (ImGui::MenuItem("Open project from path","Ctrl+O")) a.request(3);
      if (ImGui::MenuItem("Save project","Ctrl+S")) a.save();
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) a.request(4);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo","Ctrl+Z",false,!a.undo.empty())) a.history(false);
      if (ImGui::MenuItem("Redo","Ctrl+Y",false,!a.redo.empty())) a.history(true);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
      if (ImGui::MenuItem("Reset layout")) a.reset_layout=true;
      ImGui::EndMenu();
    }
    ImGui::TextUnformatted(a.dirty()?"  njin Model Editor *":"  njin Model Editor");
    ImGui::EndMainMenuBar();
  }
  if (a.request_confirm) { ImGui::OpenPopup("Unsaved project"); a.request_confirm=false; }
  if (ImGui::BeginPopupModal("Unsaved project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Save changes before continuing?");
    ImGui::TextWrapped("The project path is set in the Project panel.");
    bool needs_save_as=a.pending==3 && a.saved_path.empty();
    if (needs_save_as) ImGui::TextUnformatted("To keep this untitled model, Cancel and save it to a different path first.");
    ImGui::BeginDisabled(needs_save_as);
    if (ImGui::Button("Save and continue")) {
      // The path typed for Open belongs to the next document, not the current one.
      if (a.pending==3) std::snprintf(a.path,sizeof(a.path),"%s",a.saved_path.c_str());
      a.save();
      if (a.pending==3) std::snprintf(a.path,sizeof(a.path),"%s",a.pending_open_path.c_str());
      if (!a.dirty()) { a.perform(a.pending); ImGui::CloseCurrentPopup(); }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard changes")) { a.perform(a.pending); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
void bone_tree(App &a,int parent) {
  for (int i=0;i<(int)a.doc.bones.size();++i) {
    auto &b=a.doc.bones[i]; if (b.parent!=parent) continue;
    ImGui::PushID(i);
    auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth;
    if (a.selected_bone==i) flags|=ImGuiTreeNodeFlags_Selected;
    bool opened=ImGui::TreeNodeEx("bone",flags,"%s",b.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { a.selected_bone=i; a.selected_shape=-1; }
    if (opened) { bone_tree(a,i); ImGui::TreePop(); }
    ImGui::PopID();
  }
}
void scene(App &a) {
  ImGui::Begin("Scene");
  ImGui::TextUnformatted("SHAPES / ordered CSG");
  if (ImGui::Button("+ Shape")) ImGui::OpenPopup("Add shape");
  if (ImGui::BeginPopup("Add shape")) {
    for (int k=0;k<5;++k) if (ImGui::MenuItem(kinds[k],nullptr,false,a.doc.shapes.size()<128)) {
      Shape s; s.kind=k; s.name=std::string(kinds[k])+" "+std::to_string(a.doc.shapes.size()+1);
      s.position={0,1,0};
      if (a.selected_bone>=0) {
        s.bone=a.selected_bone; s.position={0,a.doc.bones[s.bone].length*0.5f,0};
      }
      a.doc.shapes.push_back(s); a.selected_shape=(int)a.doc.shapes.size()-1;
      a.selected_bone=-1;
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine(); if (ImGui::Button("Duplicate")) a.duplicate();
  ImGui::SameLine(); if (ImGui::Button("Delete")) a.remove();
  for (int i=0;i<(int)a.doc.shapes.size();++i) {
    auto &s=a.doc.shapes[i]; ImGui::PushID(i);
    ImGui::Checkbox("##visible",&s.visible); ImGui::SameLine();
    std::string label=std::to_string(i+1)+"  "+s.name+"  ["+operations[s.operation]+"]";
    if (ImGui::Selectable(label.c_str(),a.selected_shape==i)) { a.selected_shape=i; a.selected_bone=-1; }
    ImGui::PopID();
  }
  if (a.selected_shape>=0) {
    int i=a.selected_shape;
    if (ImGui::Button("Move up") && i>0) { std::swap(a.doc.shapes[i],a.doc.shapes[i-1]); --a.selected_shape; }
    ImGui::SameLine();
    if (ImGui::Button("Move down") && i+1<(int)a.doc.shapes.size()) { std::swap(a.doc.shapes[i],a.doc.shapes[i+1]); ++a.selected_shape; }
  }
  ImGui::Separator(); ImGui::TextUnformatted("SKELETON / parent > child");
  if (ImGui::Button("+ Root bone") && a.doc.bones.size()<128) {
    a.doc.bones.push_back(Bone{}); a.selected_bone=(int)a.doc.bones.size()-1; a.selected_shape=-1;
  }
  ImGui::SameLine();
  if (ImGui::Button("+ Child") && a.selected_bone>=0 && a.doc.bones.size()<128) {
    Bone b; b.parent=a.selected_bone; b.position.y=a.doc.bones[b.parent].length;
    a.doc.bones.push_back(b); a.selected_bone=(int)a.doc.bones.size()-1; a.selected_shape=-1;
  }
  bone_tree(a,-1);
  ImGui::End();
}
void inspector(App &a) {
  ImGui::Begin("Properties");
  if (a.selected_shape>=0) {
    auto &s=a.doc.shapes[a.selected_shape];
    ImGui::TextUnformatted("SHAPE"); name_input(s.name);
    ImGui::Combo("Primitive",&s.kind,kinds,5); ImGui::Combo("Operation",&s.operation,operations,4);
    if (s.operation==1) ImGui::DragFloat("Blend",&s.blend,0.01f,0,5,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    vector_input("Position",s.position); vector_input("Rotation",s.rotation,-360,360);
    if (s.kind==1) vector_input("Half extents",s.size,0.01f,20);
    else ImGui::DragFloat("Radius",&s.radius,0.01f,0.01f,20,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    if (s.kind==2 || s.kind==3) ImGui::DragFloat("Height",&s.height,0.01f,0.01f,20,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    if (s.kind==4) ImGui::DragFloat("Tube radius",&s.thickness,0.01f,0.01f,20,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::BeginCombo("Attach to bone",s.bone<0?"World":a.doc.bones[s.bone].name.c_str())) {
      for (int i=-1;i<(int)a.doc.bones.size();++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(i<0?"World":a.doc.bones[i].name.c_str(),s.bone==i)) bind_shape(a.doc,a.selected_shape,i);
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    ImGui::TextWrapped("Attachment preserves rest position. Coordinates become local to the bone. The first visible shape seeds the CSG result.");
  } else if (a.selected_bone>=0) {
    auto &b=a.doc.bones[a.selected_bone];
    ImGui::TextUnformatted("BONE"); name_input(b.name);
    if (a.animation_active()) {
      ImGui::Text("Animation: %.3f s",a.playhead);
      auto key=a.current_key(a.selected_bone);
      ImGui::BeginDisabled(a.playing);
      bool changed=vector_input("Key translation",key.translation);
      changed=vector_input("Key rotation",key.rotation,-360,360)||changed;
      if (changed && !set_key(a.doc.clips[a.active_clip],key)) a.status="Clip key limit reached.";
      if (ImGui::Button("Add / update keyframe")) a.key_pose(a.selected_bone);
      ImGui::EndDisabled();
      ImGui::TextWrapped("Edits and gizmo drags create a key at the playhead. Pause playback to edit. Translation is relative to rest in parent space.");
    }
    ImGui::BeginDisabled(a.animation_active());
    if (ImGui::BeginCombo("Parent",b.parent<0?"World":a.doc.bones[b.parent].name.c_str())) {
      for (int i=-1;i<(int)a.doc.bones.size();++i) if (can_parent(a.doc,a.selected_bone,i)) {
        ImGui::PushID(i);
        if (ImGui::Selectable(i<0?"World":a.doc.bones[i].name.c_str(),b.parent==i)) b.parent=i;
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    vector_input("Rest position",b.position); vector_input("Rest rotation",b.rotation,-360,360);
    ImGui::DragFloat("Bone length",&b.length,0.01f,0.01f,20,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::Separator();
    vector_input("Pose translation",b.offset);
    vector_input("Pose rotation",b.pose,-360,360);
    if (ImGui::Button("Reset this pose")) { b.pose={}; b.offset={}; }
    ImGui::TextWrapped("Turn on Pose preview to see pose rotations. Parent changes keep local coordinates. Deleting a bone removes its descendants, keeping their shapes detached in rest space.");
    ImGui::EndDisabled();
  } else ImGui::TextWrapped("Select a shape or bone from Scene or click it in the viewport.");
  ImGui::Separator();
  float color[]={a.doc.color.x,a.doc.color.y,a.doc.color.z};
  if (ImGui::ColorEdit3("Clay color",color)) a.doc.color={color[0],color[1],color[2]};
  ImGui::End();
}
void project(App &a) {
  ImGui::Begin("Project");
  ImGui::TextUnformatted("Editable project (shapes + skeleton + pose)");
  ImGui::InputText("Project path",a.path,sizeof(a.path));
  if (ImGui::Button("Open")) a.request(3);
  ImGui::SameLine(); if (ImGui::Button("Save")) a.save();
  ImGui::SameLine(); if (ImGui::Button("Undo")) a.history(false);
  ImGui::SameLine(); if (ImGui::Button("Redo")) a.history(true);
  ImGui::Separator();
  ImGui::InputText("OBJ path",a.export_path,sizeof(a.export_path));
  ImGui::SliderInt("Export detail",&a.export_resolution,24,96);
  if (ImGui::Button("Export OBJ")) ImGui::OpenPopup("Export surface");
  if (ImGui::BeginPopupModal("Export surface",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Export %s surface to:",a.animation_active()?"animation frame":a.posed?"posed":"rest");
    ImGui::TextUnformatted(a.export_path);
    ImGui::TextUnformatted("Existing OBJ will be replaced. Rig stays in project JSON.");
    if (ImGui::Button("Export")) {
      if (!a.export_path[0]) a.status="Enter an OBJ path first.";
      else if (std::filesystem::path(reinterpret_cast<const char8_t*>(a.export_path)).extension()!=".obj")
        a.status="Use an .obj extension for the exported mesh.";
      else {
        try {
          auto matrices=a.displayed_bones();
          auto surface=triangulate(Field(a.doc,a.posed,&matrices),a.export_resolution);
          if (surface.vertices.empty()) a.status="No surface to export. Check shape visibility and CSG operations.";
          else a.status=export_obj(a.export_path,surface)?std::string("Exported: ")+a.export_path:"Export failed: check the path.";
        } catch (const std::exception &e) { a.status=std::string("Export failed: ")+e.what(); }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SameLine(); ImGui::TextDisabled("Static mesh; save JSON to retain rig.");
  ImGui::Separator(); ImGui::TextWrapped("%s",a.status.c_str());
  ImGui::End();
}

ImVec2 screen(Vector3 p,const App &a,ImVec2 origin,ImVec2 size) {
  Vector2 v=GetWorldToScreenEx(p,a.camera,(int)size.x,(int)size.y);
  return {origin.x+v.x,origin.y+v.y};
}
float pixel_distance(ImVec2 a,ImVec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
void viewport(App &a) {
  ImGui::Begin("Viewport");
  ImGui::BeginDisabled(a.animation_active());
  ImGui::Checkbox("Pose preview",&a.posed); ImGui::EndDisabled(); ImGui::SameLine();
  ImGui::Checkbox("Bones",&a.bones_visible); ImGui::SameLine();
  ImGui::BeginDisabled(a.animation_active()); ImGui::Checkbox("Wire",&a.wire); ImGui::EndDisabled();
  ImGui::SameLine(); ImGui::Checkbox("Grid",&a.grid);
  if (ImGui::Button("Frame all (F)")) a.focus();
  ImGui::SameLine();
  if (ImGui::Button("Front")) { a.yaw=0; a.pitch=0; } ImGui::SameLine();
  if (ImGui::Button("Side")) { a.yaw=PI/2; a.pitch=0; } ImGui::SameLine();
  if (ImGui::Button("Top")) { a.yaw=0; a.pitch=1.55f; } ImGui::SameLine();
  ImGui::BeginDisabled(a.animation_active());
  if (ImGui::Button("Reset all poses")) for (auto &b:a.doc.bones) { b.pose={}; b.offset={}; }
  ImGui::EndDisabled();
  ImGui::RadioButton("Move",&a.gizmo_operation,0); ImGui::SameLine();
  ImGui::RadioButton("Rotate",&a.gizmo_operation,1); ImGui::SameLine();
  if (a.live.valid() && (!a.wire || a.animation_active())) ImGui::Text("Live SDF / %d FPS",GetFPS());
  else { ImGui::SetNextItemWidth(140); ImGui::SliderInt("Mesh detail",&a.resolution,16,64); }
  ImGui::SameLine(); ImGui::SetNextItemWidth(120);
  ImGui::Combo("Quality",&a.preview_quality,"Fast (50%)\0Full (100%)\0");
  ImGui::TextDisabled("RMB orbit | MMB pan | Wheel zoom | Click select | W move / E rotate");
  ImVec2 origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
  size.x=std::max(1.0f,size.x); size.y=std::max(1.0f,size.y);
  draw_preview(a,(int)size.x,(int)size.y);
  ImGui::Image((ImTextureID)(uintptr_t)a.viewport.texture.id,size,{0,1},{1,0});
  bool hovered=ImGui::IsItemHovered();
  auto &io=ImGui::GetIO();
  if (hovered) {
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
      a.yaw-=io.MouseDelta.x*0.008f; a.pitch=std::clamp(a.pitch+io.MouseDelta.y*0.008f,-1.55f,1.55f);
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      Vector3 forward=Vector3Normalize(Vector3Subtract(a.camera.target,a.camera.position));
      Vector3 right=Vector3Normalize(Vector3CrossProduct(forward,{0,1,0}));
      Vector3 up=Vector3CrossProduct(right,forward);
      a.target=Vector3Add(a.target,Vector3Scale(Vector3Add(Vector3Scale(right,-io.MouseDelta.x),Vector3Scale(up,io.MouseDelta.y)),a.zoom/size.y));
    }
    a.zoom=std::clamp(a.zoom*std::exp(-io.MouseWheel*0.12f),0.1f,1000.0f);
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) a.focus();
  }
  auto *draw=ImGui::GetWindowDrawList();
  draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
  auto bones=a.displayed_bones();
  bool clicked=hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left), consumed=false;
  if (hovered && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_W)) a.gizmo_operation=0;
    if (ImGui::IsKeyPressed(ImGuiKey_E)) a.gizmo_operation=1;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
      if (ImGui::IsKeyPressed(ImGuiKey_I) && !a.playing) a.key_pose(a.selected_bone);
      if (ImGui::IsKeyPressed(ImGuiKey_Space) && a.animation_active()) a.toggle_play();
    }
  }
  if (a.selected_shape>=0 || a.selected_bone>=0) {
    Matrix world=a.selected_shape>=0 ? shape_matrix(a.doc.shapes[a.selected_shape],bones) : bones[a.selected_bone];
    auto transform=MatrixToFloatV(world), view=MatrixToFloatV(GetCameraMatrix(a.camera));
    auto projection=MatrixToFloatV(MatrixPerspective(a.camera.fovy*DEG2RAD,size.x/size.y,rlGetCullDistanceNear(),rlGetCullDistanceFar()));
    ImGuizmo::SetOrthographic(false); ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y); ImGuizmo::Enable(!a.playing);
    if (ImGuizmo::Manipulate(view.v,projection.v,a.gizmo_operation==0?ImGuizmo::TRANSLATE:ImGuizmo::ROTATE,
                            ImGuizmo::LOCAL,transform.v)) {
      const float *v=transform.v;
      Matrix edited{v[0],v[4],v[8],v[12],v[1],v[5],v[9],v[13],v[2],v[6],v[10],v[14],v[3],v[7],v[11],v[15]};
      if (a.selected_shape>=0) {
        auto &s=a.doc.shapes[a.selected_shape];
        if (s.bone>=0) edited=MatrixMultiply(edited,MatrixInvert(bones[s.bone]));
        s.position=Vector3Clamp({edited.m12,edited.m13,edited.m14},{-100,-100,-100},{100,100,100});
        s.rotation=matrix_euler(edited);
      } else {
        auto &b=a.doc.bones[a.selected_bone];
        if (b.parent>=0) edited=MatrixMultiply(edited,MatrixInvert(bones[b.parent]));
        if (a.animation_active() || a.posed) {
          Vector3 offset=Vector3Clamp(Vector3Subtract({edited.m12,edited.m13,edited.m14},b.position),{-100,-100,-100},{100,100,100});
          auto rotation=matrix_euler(MatrixMultiply(edited,MatrixInvert(local_matrix({},b.rotation))));
          if (a.animation_active()) {
            if (!set_key(a.doc.clips[a.active_clip],{a.selected_bone,a.playhead,offset,rotation})) a.status="Clip key limit reached.";
          } else { b.offset=offset; b.pose=rotation; }
        } else {
          b.position=Vector3Clamp({edited.m12,edited.m13,edited.m14},{-100,-100,-100},{100,100,100});
          b.rotation=matrix_euler(edited);
        }
      }
    }
    consumed=ImGuizmo::IsOver() || ImGuizmo::IsUsing();
  }
  int hit_bone=-1; float nearest=12;
  if (a.bones_visible) for (int i=0;i<(int)bones.size();++i) {
    Vector3 p=Vector3Transform({},bones[i]),tip=Vector3Transform({0,a.doc.bones[i].length,0},bones[i]);
    if (Vector3DotProduct(Vector3Subtract(p,a.camera.position),Vector3Subtract(a.camera.target,a.camera.position))<=0) continue;
    ImVec2 start=screen(p,a,origin,size),end=screen(tip,a,origin,size);
    ImU32 color=i==a.selected_bone?IM_COL32(255,203,85,255):IM_COL32(223,235,250,180);
    draw->AddLine(start,end,color,2); draw->AddCircleFilled(start,4,color); draw->AddCircle(end,4,color);
    float d=pixel_distance(start,io.MousePos);
    if (d<nearest) { nearest=d; hit_bone=i; }
    if (i==a.selected_bone) draw->AddText({start.x+8,start.y+4},color,a.doc.bones[i].name.c_str());
  }
  if (clicked && !consumed) {
    if (hit_bone>=0) { a.selected_bone=hit_bone; a.selected_shape=-1; }
    else {
      Ray ray=GetScreenToWorldRayEx({io.MousePos.x-origin.x,io.MousePos.y-origin.y},a.camera,(int)size.x,(int)size.y);
      float best=1e9f; int selected=-1;
      for (int i=0;i<(int)a.doc.shapes.size();++i) if (a.doc.shapes[i].visible) {
        Document one; one.bones=a.doc.bones; one.shapes={a.doc.shapes[i]}; Field f(one,a.posed,&bones);
        // Start at the bounding box, so distant objects remain pickable.
        auto box=GetRayCollisionBox(ray,{f.low,f.high}); if (!box.hit) continue;
        float t=std::max(0.0f,box.distance);
        for (int step=0;step<160 && t<best;++step) {
          float d=f.distance(Vector3Add(ray.position,Vector3Scale(ray.direction,t)));
          if (d<0.002f) { best=t; selected=i; break; }
          t+=std::max(d,0.001f); if (t>box.distance+Vector3Distance(f.low,f.high)+1) break;
        }
      }
      a.selected_shape=selected; a.selected_bone=-1;
    }
  }
  draw->PopClipRect();
  ImGui::SetCursorScreenPos({origin.x,origin.y+size.y});
  ImGui::End();
}
void animation_panel(App &a) {
  ImGui::Begin("Animation");
  a.timeline_focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
  const char *selected=a.active_clip>=0 && a.active_clip<(int)a.doc.clips.size() ? a.doc.clips[a.active_clip].name.c_str() : "Choose a clip";
  ImGui::SetNextItemWidth(160);
  if (ImGui::BeginCombo("##clip",selected)) {
    for (int i=0;i<(int)a.doc.clips.size();++i) {
      ImGui::PushID(i);
      if (ImGui::Selectable(a.doc.clips[i].name.c_str(),a.active_clip==i)) a.select_clip(i);
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ Clip") && a.doc.clips.size()<64) {
    AnimationClip c; c.name="Animation "+std::to_string(a.doc.clips.size()+1);
    // Capture the current static pose as the start of each track.
    for (int i=0;i<(int)a.doc.bones.size();++i) set_key(c,{i,0,a.doc.bones[i].offset,a.doc.bones[i].pose});
    a.doc.clips.push_back(std::move(c)); a.select_clip((int)a.doc.clips.size()-1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Wave demo") && a.doc.clips.size()<64) {
    bool compatible=false;
    for (const auto &b:a.doc.bones) compatible|=b.name=="Forearm.R";
    if (compatible) { add_demo_animation(a.doc); a.select_clip((int)a.doc.clips.size()-1); }
    else a.status="Wave demo needs the humanoid template's Forearm.R bone. Use + Clip for your own rig.";
  }
  if (a.active_clip<0 || a.active_clip>=(int)a.doc.clips.size()) {
    ImGui::TextWrapped("Create a clip, select a bone, move the playhead, then rotate the bone or add a keyframe. Save the project to keep animations.");
    ImGui::End(); return;
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy") && a.doc.clips.size()<64) {
    auto copy=a.doc.clips[a.active_clip]; copy.name=copy.name.substr(0,120)+" copy";
    a.doc.clips.push_back(std::move(copy)); a.select_clip((int)a.doc.clips.size()-1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete clip")) {
    a.doc.clips.erase(a.doc.clips.begin()+a.active_clip);
    a.select_clip(a.doc.clips.empty()?-1:std::min(a.active_clip,(int)a.doc.clips.size()-1));
    ImGui::End(); return;
  }
  auto &clip=a.doc.clips[a.active_clip];
  ImGui::SameLine();
  if (ImGui::Checkbox("Preview",&a.animation_enabled) && !a.animation_enabled) a.playing=false;
  ImGui::SetNextItemWidth(160); name_input(clip.name);
  ImGui::SameLine(); ImGui::SetNextItemWidth(70);
  float last=0.05f; for (const auto &k:clip.keys) last=std::max(last,k.time);
  ImGui::DragFloat("Duration",&clip.duration,0.05f,last,600,"%.2fs",ImGuiSliderFlags_AlwaysClamp);
  a.playhead=std::min(a.playhead,clip.duration);
  ImGui::SameLine(); ImGui::SetNextItemWidth(55);
  ImGui::DragInt("FPS",&clip.fps,1,1,120,"%d",ImGuiSliderFlags_AlwaysClamp);
  ImGui::SameLine(); ImGui::Checkbox("Loop",&clip.loop);
  ImGui::SameLine(); ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("Speed",&a.speed,0.05f,0.1f,4,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
  ImGui::BeginDisabled(!a.live.valid());
  if (ImGui::Button(a.playing?"Pause":"Play")) a.toggle_play();
  ImGui::EndDisabled();
  ImGui::SameLine(); if (ImGui::Button("Stop")) { a.playing=false; a.playhead=0; }
  auto snap=[&](float t) { return std::clamp(std::round(t*clip.fps)/clip.fps,0.0f,clip.duration); };
  ImGui::SameLine(); if (ImGui::Button("< Frame")) { a.playing=false; a.playhead=snap(a.playhead-1.0f/clip.fps); }
  ImGui::SameLine(); if (ImGui::Button("Frame >")) { a.playing=false; a.playhead=snap(a.playhead+1.0f/clip.fps); }
  ImGui::SameLine(); ImGui::SetNextItemWidth(160);
  if (ImGui::SliderFloat("Time",&a.playhead,0,clip.duration,"%.3fs")) { a.playing=false; a.playhead=snap(a.playhead); }
  ImGui::SameLine(); ImGui::Text("Frame %d",(int)std::lround(a.playhead*clip.fps));
  ImGui::BeginDisabled(a.playing || !a.animation_active());
  ImGui::BeginDisabled(a.selected_bone<0);
  if (ImGui::Button("Add key (I)")) a.key_pose(a.selected_bone);
  ImGui::SameLine(); if (ImGui::Button("Delete key")) remove_key(clip,a.selected_bone,a.playhead);
  ImGui::EndDisabled();
  ImGui::SameLine(); if (ImGui::Button("Key all bones")) for (int i=0;i<(int)a.doc.bones.size();++i) a.key_pose(i);
  ImGui::EndDisabled();
  ImGui::SameLine(); ImGui::TextDisabled("Edit bone = auto key. Drag diamonds to retime.");
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) a.toggle_play();
    if (ImGui::IsKeyPressed(ImGuiKey_I) && !a.playing) a.key_pose(a.selected_bone);
  }
  ImGui::BeginChild("Tracks",{0,0},ImGuiChildFlags_Borders);
  ImVec2 origin=ImGui::GetCursorScreenPos();
  float width=std::max(220.0f,ImGui::GetContentRegionAvail().x),label=110,track=width-label-12;
  float height=28+26*(float)a.doc.bones.size();
  ImGui::InvisibleButton("timeline",{width,std::max(height,40.0f)});
  bool hover=ImGui::IsItemHovered(), active=ImGui::IsItemActive();
  auto &io=ImGui::GetIO();
  auto *draw=ImGui::GetWindowDrawList();
  auto x=[&](float time) { return origin.x+label+track*time/clip.duration; };
  for (int tick=0;tick<=5;++tick) {
    float time=clip.duration*tick/5; char text[24]; std::snprintf(text,sizeof(text),"%.2f",time);
    draw->AddText({x(time)-6,origin.y},IM_COL32(170,180,200,255),text);
    draw->AddLine({x(time),origin.y+22},{x(time),origin.y+height},IM_COL32(65,72,85,255));
  }
  for (int i=0;i<(int)a.doc.bones.size();++i) {
    float y=origin.y+28+i*26;
    if (a.selected_bone==i) draw->AddRectFilled({origin.x,y},{origin.x+width,y+25},IM_COL32(45,75,105,100));
    draw->PushClipRect({origin.x,y},{origin.x+label-4,y+25},true);
    draw->AddText({origin.x+4,y+3},IM_COL32(215,225,235,255),a.doc.bones[i].name.c_str());
    draw->PopClipRect();
  }
  for (const auto &k:clip.keys) {
    float px=x(k.time),py=origin.y+28+k.bone*26+12;
    bool selected_key=k.bone==a.selected_bone && std::abs(k.time-a.playhead)<0.0001f;
    draw->AddQuadFilled({px,py-5},{px+5,py},{px,py+5},{px-5,py},selected_key?IM_COL32(255,210,85,255):IM_COL32(100,190,240,255));
  }
  draw->AddLine({x(a.playhead),origin.y+20},{x(a.playhead),origin.y+height},IM_COL32(255,105,105,255),2);
  if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    a.playing=false; a.animation_enabled=true; a.dragging_key_bone=-1;
    int row=(int)std::floor((io.MousePos.y-origin.y-28)/26);
    if (row>=0 && row<(int)a.doc.bones.size()) { a.selected_bone=row; a.selected_shape=-1; }
    if (io.MousePos.x>=origin.x+label) {
      float time=snap((io.MousePos.x-origin.x-label)/track*clip.duration);
      for (const auto &k:clip.keys) if (k.bone==row && std::abs(x(k.time)-io.MousePos.x)<8) {
        a.dragging_key_bone=row; a.dragging_key_time=k.time; time=k.time; break;
      }
      a.playhead=time;
    }
  }
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left,3) && io.MousePos.x>=origin.x+label) {
    float time=snap((io.MousePos.x-origin.x-label)/track*clip.duration);
    if (a.dragging_key_bone>=0) {
      bool collision=false;
      for (const auto &k:clip.keys) if (k.bone==a.dragging_key_bone && std::abs(k.time-time)<0.0001f) collision=true;
      if (!collision) {
        auto key=a.current_key(a.dragging_key_bone); key.time=time;
        remove_key(clip,a.dragging_key_bone,a.dragging_key_time); set_key(clip,key);
        a.dragging_key_time=time; a.playhead=time;
      }
    } else a.playhead=time;
  }
  ImGui::EndChild(); ImGui::End();
}
void dock_layout(App &a) {
  ImGuiID dock=ImGui::DockSpaceOverViewport(0,ImGui::GetMainViewport());
  if (ImGui::DockBuilderGetNode(dock)->ChildNodes[0] && !a.reset_layout) return;
  a.reset_layout=false;
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock,ImGui::GetMainViewport()->WorkSize);
  ImGuiID center=dock,left,right,bottom,project;
  ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.23f,&left,&center);
  ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.30f,&right,&center);
  ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,0.38f,&bottom,&center);
  ImGui::DockBuilderSplitNode(right,ImGuiDir_Down,0.38f,&project,&right);
  ImGui::DockBuilderDockWindow("Scene",left); ImGui::DockBuilderDockWindow("Properties",right);
  ImGui::DockBuilderDockWindow("Viewport",center); ImGui::DockBuilderDockWindow("Project",project);
  ImGui::DockBuilderDockWindow("Animation",bottom);
  ImGui::DockBuilderFinish(dock);
}
int app_self_test() {
  App a;
  auto original=dump(a.doc);
  a.doc.bones[0].position.x=2; a.commit();
  a.history(false);
  if (dump(a.doc)!=original) return 1;
  a.history(true);
  if (a.doc.bones[0].position.x!=2) return 1;
  a.history(false); a.doc.bones[0].position.y=3; a.commit();
  if (!a.redo.empty()) return 1;
  add_demo_animation(a.doc); a.select_clip(0); a.commit();
  a.selected_bone=6; a.playhead=0.25f; a.key_pose(6); a.commit();
  if (a.doc.clips[0].keys.size()!=26) return 1;
  a.history(false);
  if (a.doc.clips[0].keys.size()!=25) return 1;
  a.history(true);
  if (a.doc.clips[0].keys.size()!=26) return 1;
  auto folder=std::filesystem::temp_directory_path()/
    ("njin-model-editor-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  auto path=(folder/"project.json").u8string();
  std::string filename(path.begin(),path.end());
  std::snprintf(a.path,sizeof(a.path),"%s",filename.c_str());
  a.save();
  bool ok=!a.dirty();
  a.doc.bones[0].pose.x=42; a.save(); // Saving over an existing project must work too.
  ok=ok && !a.dirty();
  a.doc=Document{}; a.perform(3);
  ok=ok && a.doc.bones.size()==11 && a.doc.bones[0].pose.x==42 && a.doc.clips.size()==1 && a.doc.clips[0].keys.size()==26;
  a.doc.bones[0].pose.x=15; a.request(1);
  ok=ok && a.pending==1 && a.request_confirm && a.doc.bones[0].pose.x==15;
  std::error_code ec;
  std::filesystem::remove(folder/"project.json",ec); std::filesystem::remove(folder,ec);
  if (!ok) { std::fprintf(stderr,"FAIL: editor history/save/reopen checks\n"); return 1; }
  std::printf("PASS: editor undo/redo, keyframe history, save/overwrite/reopen with clips, unsaved guard\n");
  return 0;
}
}
}

int main(int argc,char **argv) {
  using namespace model_editor;
  if (argc>1 && std::string(argv[1])=="--self-test") return self_test() || app_self_test();
  bool animation_smoke=argc>1 && std::string(argv[1])=="--animation-smoke-test";
  bool smoke=animation_smoke || (argc>1 && std::string(argv[1])=="--smoke-test");
  SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT|FLAG_VSYNC_HINT);
  InitWindow(1600,960,"njin Model Editor"); SetWindowMinSize(1000,700); SetExitKey(KEY_NULL); SetTargetFPS(60);
  rlImGuiBeginInitImGui();
  ImGui::StyleColorsDark();
  if (FileExists("C:/Windows/Fonts/segoeui.ttf"))
    if (auto *font=ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",17))
      ImGui::GetIO().FontDefault=font;
  rlImGuiEndInitImGui();
  auto &io=ImGui::GetIO(); io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly=true; io.IniFilename=smoke?nullptr:"njin_model_editor.ini";
  ImGui::GetStyle().FrameRounding=4; ImGui::GetStyle().GrabRounding=4;
  App app;
  if (!app.live.initialize()) app.status="Live SDF shader failed. Mesh preview available; animation playback disabled.";
  if (animation_smoke) {
    add_demo_animation(app.doc); app.select_clip(0); app.selected_shape=-1; app.selected_bone=6;
    app.committed=dump(app.doc); app.saved=app.committed;
  }
  if (argc>1 && !smoke) { std::snprintf(app.path,sizeof(app.path),"%s",argv[1]); app.perform(3); }
  int frames=0;
  bool smoke_ok=false;
  Image baseline{};
  bool animation_pixels_changed=false;
  while (!app.exit) {
    if (WindowShouldClose()) app.request(4);
    update_surface(app);
    if (app.animation_active()) app.playhead=advance_animation(app.playhead,GetFrameTime()*app.speed,app.doc.clips[app.active_clip],app.playing);
    BeginDrawing(); ClearBackground({20,24,31,255}); rlImGuiBegin();
    ImGuizmo::BeginFrame();
    menu(app); dock_layout(app); scene(app); animation_panel(app); inspector(app); project(app); viewport(app);
    if (!io.WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) {
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl|ImGuiKey_S)) app.save();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl|ImGuiKey_O)) app.request(3);
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl|ImGuiKey_Z)) app.history(false);
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl|ImGuiKey_Y)) app.history(true);
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl|ImGuiKey_D)) app.duplicate();
      if (ImGui::IsKeyPressed(ImGuiKey_Delete,false)) {
        if (app.timeline_focused && app.animation_active()) {
          if (!app.playing) remove_key(app.doc.clips[app.active_clip],app.selected_bone,app.playhead);
        } else app.remove();
      }
    }
    if (!ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing()) app.commit();
    rlImGuiEnd(); EndDrawing();
    ++frames;
    if (animation_smoke && frames==30) {
      baseline=LoadImageFromTexture(app.viewport.texture);
      Image screen=LoadImageFromScreen(); ExportImage(screen,"build/animation_rest.png"); UnloadImage(screen);
      app.playhead=0.5f;
    }
    if (animation_smoke && frames==60) {
      Image posed=LoadImageFromTexture(app.viewport.texture);
      if (baseline.width==posed.width && baseline.height==posed.height) {
        Color *before=LoadImageColors(baseline),*after=LoadImageColors(posed); int changed=0;
        for (int i=0;i<posed.width*posed.height;++i)
          changed+=std::abs((int)before[i].r-after[i].r)+std::abs((int)before[i].g-after[i].g)+std::abs((int)before[i].b-after[i].b)>30;
        animation_pixels_changed=changed>100;
        UnloadImageColors(before); UnloadImageColors(after);
      }
      UnloadImage(baseline); baseline={}; UnloadImage(posed);
      Image screen=LoadImageFromScreen(); ExportImage(screen,"build/animation_key.png"); UnloadImage(screen);
      app.playing=true;
    }
    if (smoke && frames>120 && app.live.valid()) {
      Image image=LoadImageFromScreen(); smoke_ok=ExportImage(image,"build/model_editor_smoke.png"); UnloadImage(image); app.exit=true;
      if (animation_smoke) {
        smoke_ok=smoke_ok && animation_pixels_changed && !app.dirty() && app.undo.empty();
        std::printf("Animation smoke: changed pixels=%s, playback left document clean=%s\n",animation_pixels_changed?"yes":"NO",!app.dirty()?"yes":"NO");
      }
    }
    if (smoke && frames>1800) { app.exit=true; std::fprintf(stderr,"Preview smoke test timed out\n"); }
  }
  if (app.worker.valid()) app.worker.wait();
  if (app.has_model) UnloadModel(app.model);
  if (app.viewport.id) UnloadRenderTexture(app.viewport);
  if (baseline.data) UnloadImage(baseline);
  app.live.shutdown();
  rlImGuiShutdown(); CloseWindow();
  return smoke && !smoke_ok ? 1 : 0;
}
