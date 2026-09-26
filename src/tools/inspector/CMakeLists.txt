# njin_inspector: a separate window that connects to a running game over
# 127.0.0.1 (see njin_debug.h) and shows FPS, entities, colliders, the log and
# watches with Dear ImGui, so the game itself draws no debug UI.

include(FetchContent)

# Pinned together: this rlImGui commit needs the font API of Dear ImGui 1.92.9.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9
  SYSTEM)
# ImPlot draws the consumption graphs (CPU, RAM, GPU, frame budget).
FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG v1.0
  SYSTEM)
FetchContent_Declare(
  rlimgui
  GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
  GIT_TAG 1550009359ad975927f7f0e4a3f47e3f27123ea9
  SYSTEM)
# Neither ships a CMakeLists.txt: this only downloads them.
FetchContent_MakeAvailable(imgui implot rlimgui)

add_library(njin_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp
  ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(njin_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${implot_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(njin_imgui PUBLIC raylib)

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
  sysmon.cpp)
# The inspector reuses the runtime's JSON and socket code, so it sees the
# runtime's private headers.
target_include_directories(njin_inspector PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/runtime)
target_link_libraries(njin_inspector PRIVATE njin::rt njin_imgui raylib njin_warnings)
if(WIN32)
  # Process memory (PSAPI) and GPU counters (PDH) of the game, by process id.
  target_link_libraries(njin_inspector PRIVATE psapi pdh)
endif()
