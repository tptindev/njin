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
FetchContent_Declare(
  rlimgui
  GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
  GIT_TAG 1550009359ad975927f7f0e4a3f47e3f27123ea9
  SYSTEM)
# Neither ships a CMakeLists.txt: this only downloads them.
FetchContent_MakeAvailable(imgui rlimgui)

add_library(njin_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(njin_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(njin_imgui PUBLIC raylib)

add_executable(njin_inspector main.cpp)
# The inspector reuses the runtime's JSON and socket code, so it sees the
# runtime's private headers.
target_include_directories(njin_inspector PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/runtime)
target_link_libraries(njin_inspector PRIVATE njin::rt njin_imgui raylib njin_warnings)
