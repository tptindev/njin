#pragma once
#include "app.h"

namespace ui_editor {

// Before the ImGui frame: time, the input the Viewport lets through, and
// njin.ui's frame_begin.
void preview_begin_frame(editor_app &app);
// Draws the layout with the engine into the preview texture and records where
// each panel and widget went (editor_app::boxes). Between BeginDrawing and the
// ImGui frame.
void preview_render(editor_app &app);
unsigned int preview_texture_id();
void preview_shutdown();

} // namespace ui_editor
