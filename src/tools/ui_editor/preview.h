#pragma once
#include "app.h"
#include "njin_renderer.h"

namespace ui_editor {

// Once, after the context opened: the Viewport's input filter on the window.
void preview_install(editor_app &app);
// After njin.ui's frame_begin (phase_pre_update): no focus highlight while editing.
void preview_begin_frame(editor_app &app);
// Draws the layout with the engine into the preview target and records where
// each panel and widget went (editor_app::boxes). While Njin renders (phase_render).
void preview_render(editor_app &app);
// The preview target's colour texture (empty before the first render).
njin::RenderTextureHandle preview_texture(editor_app &app);
void preview_shutdown(editor_app &app);

} // namespace ui_editor
