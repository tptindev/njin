#pragma once
// Dear ImGui for Njin's engine tools (inspector, ui_editor, anim_editor) on the
// SDL3 + bgfx renderer, in place of raylib + rlImGui
// (docs/renderer-legacy-removal.md, step 3F).
//
// Platform: ImGui's own SDL3 backend (imgui_impl_sdl3), fed every SDL event of
// Njin's window through SdlPlatform::set_event_hook. Renderer: ImGui's draw lists go
// through Njin's public renderer API (renderer_draw_mesh: pixel coordinates,
// scissor, straight alpha blend), and ImGui's textures (the font atlas, created and
// grown as glyphs are needed) are Njin textures. An ImTextureID is a
// njin::RenderTextureHandle id (imgui_texture()), so a tool shows a render target
// with ImGui::Image(imgui_texture(renderer_target_texture(ctx, target)), ...).
//
// A frame: imgui_begin() after Njin polled input (phase_pre_update or later), the
// tool's windows, imgui_end(), then imgui_draw() while Njin renders
// (phase_post_render, so ImGui is on top of the world and the game UI).
#include "imgui.h"
#include "njin.h"
#include "njin_renderer.h"

namespace njin_tools {

// Creates the ImGui context (and ImPlot's when `implot`) on the context's SDL window.
// False, with nothing created, when the context has no SDL3 window (legacy renderer).
bool imgui_init(njin::context &ctx, bool implot = false);
void imgui_shutdown(njin::context &ctx);
void imgui_begin(njin::context &ctx);
void imgui_end(njin::context &ctx);
void imgui_draw(njin::context &ctx);

inline ImTextureID imgui_texture(njin::RenderTextureHandle texture) { return (ImTextureID)texture.id; }

// Registers the three calls above as systems: imgui_begin in phase_pre_update, `ui`
// then imgui_end in phase_update, imgui_draw in phase_post_render.
void imgui_systems(njin::context &ctx, void (*ui)(njin::context &));

} // namespace njin_tools
