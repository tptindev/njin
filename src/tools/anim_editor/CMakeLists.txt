include("${PROJECT_SOURCE_DIR}/cmake/njin_imgui.cmake")
add_executable(njin_model_editor main.cpp document.cpp animation.cpp surface.cpp live_preview.cpp self_test.cpp
  third_party/ImGuizmo/ImGuizmo.cpp)
target_include_directories(njin_model_editor SYSTEM PRIVATE third_party/ImGuizmo)
set_target_properties(njin_model_editor PROPERTIES NJIN_ENGINE_INTERNAL ON)
target_link_libraries(njin_model_editor PRIVATE njin::rt njin_imgui raylib njin_warnings)
njin_icon(njin_model_editor)
