# njin_ui_editor: builds .ui.json layouts by dragging nodes, like Godot's Control
# scenes, with dockable windows. The Viewport draws the layout with the engine's
# own UI code, so it is engine side (it reaches into context), like the inspector.

include("${PROJECT_SOURCE_DIR}/cmake/njin_imgui.cmake")

add_executable(njin_ui_editor
  main.cpp
  app.cpp
  preview.cpp
  file_dialog.cpp
  panel_scene.cpp
  panel_viewport.cpp
  panel_inspector.cpp
  panel_style.cpp
  panel_codegen.cpp
  panel_json.cpp
)

target_include_directories(njin_ui_editor PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/runtime)
set_target_properties(njin_ui_editor PROPERTIES NJIN_ENGINE_INTERNAL ON)
# preview.cpp reaches the context's members as the runtime does (njin_ctx_get.h).
target_compile_definitions(njin_ui_editor PRIVATE $<$<NOT:$<CONFIG:Debug>>:NJIN_CTX_INLINE>)
# The engine first, raylib after it: njin calls raylib's PlaySound, which
# MSVC would otherwise take from winmm.lib, found first after the engine.
njin_use(njin_ui_editor)
njin_precompile_headers(njin_ui_editor)
# A Njin context on SDL3 + bgfx with Dear ImGui (njin_imgui_sdl3); it calls no raylib.
# The runtime's private headers it reads (ui, view) still name raylib's types until
# the legacy renderer goes (docs/renderer-legacy-removal.md, step 4): headers only.
target_link_libraries(njin_ui_editor PRIVATE njin_imgui_sdl3 njin_warnings)
target_include_directories(njin_ui_editor SYSTEM PRIVATE $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)
njin_icon(njin_ui_editor)
if(WIN32)
  target_link_libraries(njin_ui_editor PRIVATE comdlg32)
endif()

# The editor window on each bgfx backend: ImGui panels plus the Viewport, where the
# engine's UI draws its demo layout into a render target; it must exit cleanly.
if(NJIN_BGFX)
  set(_njin_tool_renderers vulkan bgfx-opengl)
  if(WIN32)
    list(APPEND _njin_tool_renderers d3d12)
  endif()
  foreach(_renderer IN LISTS _njin_tool_renderers)
    add_test(NAME tool_ui_editor_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_ui_editor>" "-DWORKDIR=$<TARGET_FILE_DIR:njin_ui_editor>"
      "-DRENDERER=${_renderer}" -DFRAMES=60
      "-DSHOT=${PROJECT_BINARY_DIR}/tool-ui_editor-${_renderer}.png"
      "-DLOG=${PROJECT_BINARY_DIR}/tool-ui_editor-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_example_smoke.cmake")
    set_tests_properties(tool_ui_editor_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
  endforeach()
endif()
