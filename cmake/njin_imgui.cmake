# Dear ImGui + ImPlot for njin's tools (inspector, ui_editor, anim_editor) on SDL3 +
# bgfx (step 3F of docs/renderer-legacy-removal.md): ImGui's own SDL3 platform backend,
# drawn through Njin's renderer API by src/tools/common/njin_imgui_host.cpp. No raylib.
# The docking branch: the UI and animation editors dock their windows.
if(NOT TARGET njin_imgui_sdl3)
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
  FetchContent_MakeAvailable(imgui implot)

  add_library(njin_imgui_sdl3 STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${PROJECT_SOURCE_DIR}/src/tools/common/njin_imgui_host.cpp)
  target_include_directories(njin_imgui_sdl3 SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR})
  target_include_directories(njin_imgui_sdl3 PUBLIC ${PROJECT_SOURCE_DIR}/src/tools/common)
  # The host reads the runtime's window (SdlPlatform): engine side of the boundary.
  target_include_directories(njin_imgui_sdl3 PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/runtime)
  set_target_properties(njin_imgui_sdl3 PROPERTIES NJIN_ENGINE_INTERNAL ON)
  # 32-bit indices: the inspector's world view draws every entity into one draw
  # list, and a few thousand entities pass the 65536 vertices of 16-bit indices
  # (the host cuts commands into 16-bit runs for Njin). C++ only: the resource
  # compiler cannot take a definition with a space.
  target_compile_definitions(njin_imgui_sdl3 PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:ImDrawIdx=unsigned int>")
  target_link_libraries(njin_imgui_sdl3 PUBLIC njin_core SDL3::SDL3)
endif()
