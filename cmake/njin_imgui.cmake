# Shared ImGui + ImPlot + rlImGui target for njin tools (inspector, ui_editor).
# The docking branch: the UI editor docks its windows; the inspector does not care.
if(NOT TARGET njin_imgui)
  include(FetchContent)
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9-docking
    SYSTEM)
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
endif()
