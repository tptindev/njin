include("${PROJECT_SOURCE_DIR}/cmake/njin_imgui.cmake")
add_executable(njin_anim_editor main.cpp document.cpp animation.cpp rig.cpp roundtrip.cpp self_test.cpp
  third_party/ImGuizmo/ImGuizmo.cpp)
# cgltf_write.h includes "cgltf.h": raylib's copy, whose functions raylib compiles.
target_include_directories(njin_anim_editor SYSTEM PRIVATE third_party/ImGuizmo third_party/cgltf
  "${raylib_SOURCE_DIR}/src/external")
set_target_properties(njin_anim_editor PROPERTIES NJIN_ENGINE_INTERNAL ON)
target_link_libraries(njin_anim_editor PRIVATE njin::rt njin_imgui raylib njin_warnings)
njin_icon(njin_anim_editor)
