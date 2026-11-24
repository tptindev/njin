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
# A Njin context on SDL3 + bgfx with Dear ImGui (njin_imgui_sdl3); it calls no raylib.
# Its math (njin_rlmath.h) is raymath's header until the legacy renderer goes
# (docs/renderer-legacy-removal.md, step 4): headers only.
target_link_libraries(njin_anim_editor PRIVATE njin_imgui_sdl3 njin_warnings)
target_include_directories(njin_anim_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
target_include_directories(njin_anim_editor SYSTEM PRIVATE $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)
if(WIN32)
  target_link_libraries(njin_anim_editor PRIVATE ws2_32)
endif()
njin_icon(njin_anim_editor)

# The editor on SDL3 + bgfx: its own checks (no window), the glTF round trip through
# njin's model_load (a window of its own), and the smoke test (a model posed by a key
# moves pixels of the Viewport, which Njin draws into a render texture).
add_test(NAME tool_anim_editor_self_test COMMAND njin_anim_editor --self-test)
add_test(NAME tool_anim_editor_roundtrip COMMAND njin_anim_editor --roundtrip-test)
add_test(NAME tool_anim_editor_smoke COMMAND njin_anim_editor --smoke-test WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
set_tests_properties(tool_anim_editor_roundtrip tool_anim_editor_smoke PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 180)
set_tests_properties(tool_anim_editor_self_test PROPERTIES TIMEOUT 300)
