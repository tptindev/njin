include("${PROJECT_SOURCE_DIR}/cmake/njin_imgui.cmake")
add_executable(njin_anim_editor main.cpp document.cpp animation.cpp rig.cpp roundtrip.cpp self_test.cpp
  mocap.cpp mocap_jpeg.cpp mocap_net.cpp mocap_test.cpp third_party/ImGuizmo/ImGuizmo.cpp)
# cgltf_write.h includes "cgltf.h": raylib's copy, whose functions raylib compiles;
# mocap_jpeg.cpp compiles its own static, JPEG-only copy of raylib's stb_image.h.
target_include_directories(njin_anim_editor SYSTEM PRIVATE third_party/ImGuizmo third_party/cgltf
  "${raylib_SOURCE_DIR}/src/external")
set_target_properties(njin_anim_editor PROPERTIES NJIN_ENGINE_INTERNAL ON)
# Webcam capture: mocap/pose_stream.py and its venv live next to the sources.
target_compile_definitions(njin_anim_editor PRIVATE NJIN_ANIM_EDITOR_MOCAP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mocap")
# The engine first, raylib after it: njin calls raylib's PlaySound, which
# MSVC would otherwise take from winmm.lib, found first after the engine.
njin_use(njin_anim_editor FEATURES 3d)
njin_precompile_headers(njin_anim_editor)
target_link_libraries(njin_anim_editor PRIVATE njin_imgui raylib njin_warnings)
if(WIN32)
  target_link_libraries(njin_anim_editor PRIVATE ws2_32)
endif()
njin_icon(njin_anim_editor)
