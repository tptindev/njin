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
  os_open.cpp
  sysmon.cpp)
# The inspector reuses the runtime's JSON and socket code, so it sees the
# runtime's private headers.
target_include_directories(njin_inspector PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/runtime)
# The inspector is part of the engine: it draws with raylib and reads the
# runtime's protocol headers, so the game-side guard does not apply.
set_target_properties(njin_inspector PROPERTIES NJIN_ENGINE_INTERNAL ON)
target_link_libraries(njin_inspector PRIVATE njin::rt njin_imgui raylib njin_warnings)
# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_inspector)
if(WIN32)
  # Process memory (PSAPI) and GPU counters (PDH) of the game, by process id; the shell opens folders.
  target_link_libraries(njin_inspector PRIVATE psapi pdh shell32)
endif()
