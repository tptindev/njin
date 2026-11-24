# njin_inspector: a separate window that connects to a running game over
# 127.0.0.1 (see njin_debug.h) and shows FPS, entities, colliders, the log and
# watches with Dear ImGui, so the game itself draws no debug UI.

include("${PROJECT_SOURCE_DIR}/cmake/njin_imgui.cmake")

add_executable(njin_inspector
  main.cpp
  app.cpp
  util.cpp
  panel_performance.cpp
  panel_entities.cpp
  panel_world.cpp
  panel_inspector.cpp
  panel_watches.cpp
  panel_log.cpp
  panel_monitor.cpp
  panel_systems.cpp
  panel_memory.cpp
  panel_assets.cpp
  panel_docs.cpp
  os_open.cpp
  sysmon.cpp)
# The inspector reuses the runtime's JSON and socket code, so it sees the
# runtime's private headers.
target_include_directories(njin_inspector PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/runtime)
# The inspector is part of the engine: it reads the runtime's protocol headers,
# so the game-side guard does not apply.
set_target_properties(njin_inspector PROPERTIES NJIN_ENGINE_INTERNAL ON)
# A Njin context on SDL3 + bgfx with Dear ImGui (njin_imgui_sdl3): no raylib.
njin_use(njin_inspector)
njin_precompile_headers(njin_inspector)
target_link_libraries(njin_inspector PRIVATE njin_imgui_sdl3 njin_warnings)
# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_inspector)
if(WIN32)
  # Process memory (PSAPI) and GPU counters (PDH) of the game, by process id; the shell opens folders.
  target_link_libraries(njin_inspector PRIVATE psapi pdh shell32)
endif()

# The inspector window on each bgfx backend: it must open, draw its panels with ImGui
# and exit cleanly (no game needed: it shows "waiting for a game").
if(NJIN_BGFX)
  set(_njin_tool_renderers vulkan bgfx-opengl)
  if(WIN32)
    list(APPEND _njin_tool_renderers d3d12)
  endif()
  foreach(_renderer IN LISTS _njin_tool_renderers)
    add_test(NAME tool_inspector_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_inspector>" "-DWORKDIR=$<TARGET_FILE_DIR:njin_inspector>"
      "-DRENDERER=${_renderer}" -DFRAMES=60
      "-DSHOT=${PROJECT_BINARY_DIR}/tool-inspector-${_renderer}.png"
      "-DLOG=${PROJECT_BINARY_DIR}/tool-inspector-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_example_smoke.cmake")
    set_tests_properties(tool_inspector_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
  endforeach()
endif()
