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
target_link_libraries(njin_ui_editor PRIVATE njin::rt njin_imgui raylib njin_warnings)
njin_icon(njin_ui_editor)
if(WIN32)
  target_link_libraries(njin_ui_editor PRIVATE comdlg32)
endif()
