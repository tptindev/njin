#pragma once
#include "app.h"

namespace ui_editor {

// Dock windows, one file each.
void scene_window(editor_app &app);    // panel_scene.cpp: the node tree
void nodes_window(editor_app &app);    // panel_scene.cpp: the palette of node types
void viewport_window(editor_app &app); // panel_viewport.cpp
void inspector_window(editor_app &app);
void theme_window(editor_app &app);
void codegen_window(editor_app &app);
void json_window(editor_app &app);
void output_window(editor_app &app);

// "Add child node" menu entries for `target` (scene tree, + button).
void add_node_menu_items(editor_app &app, const node_ref &target);
// Moves a node one place up (dir < 0) or down among its siblings.
void move_selection(editor_app &app, const node_ref &n, int dir);

} // namespace ui_editor
