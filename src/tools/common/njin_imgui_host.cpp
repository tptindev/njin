// See njin_imgui_host.h. Texture handling and the draw loop follow the renderer
// backends of the pinned ImGui (v1.92.9-docking: backends/imgui_impl_sdlgpu3.cpp,
// ImGui_ImplSDLGPU3_UpdateTexture / RenderDrawData), translated to Njin's renderer API.
#include "njin_imgui_host.h"
#include "imgui_impl_sdl3.h"
#include "implot.h"
#include "njin_sdl_platform.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace njin_tools {

namespace {

struct host_state {
  njin::context *ctx = nullptr;
  bool implot = false;
  bool frame = false; // between imgui_begin and imgui_end
  std::vector<njin::RenderVertex> vertices;
  std::vector<std::uint16_t> indices;
  void (*ui)(njin::context &) = nullptr;
};
host_state g_host;

njin::SdlPlatform *platform_of(njin::context &ctx) { return njin::sdl_platform(ctx); }

// ImGui's texture requests (ImTextureData::Status) carried out with Njin textures. The
// whole image is uploaded on each update: renderer_update_texture replaces a texture,
// and the atlas changes only when new glyphs or sizes are first drawn.
void update_texture(njin::context &ctx, ImTextureData *tex) {
  if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
    IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
    const auto *pixels = static_cast<const std::uint8_t *>(tex->GetPixels());
    const njin::RenderImage image{(unsigned)tex->Width, (unsigned)tex->Height,
                                  {pixels, (size_t)tex->Width * (size_t)tex->Height * 4}};
    if (tex->Status == ImTextureStatus_WantCreate)
      tex->SetTexID((ImTextureID)njin::renderer_create_texture(ctx, image).id);
    else
      njin::renderer_update_texture(ctx, njin::RenderTextureHandle{(std::uint64_t)tex->TexID}, image);
    tex->SetStatus(ImTextureStatus_OK);
  } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
    if (tex->TexID != ImTextureID_Invalid)
      njin::renderer_destroy_texture(ctx, njin::RenderTextureHandle{(std::uint64_t)tex->TexID});
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
  }
}

// One ImGui command as one Njin draw. Njin takes 16-bit indices, so the command's
// vertices are rebased to the lowest index it uses; a command spanning more than
// 65536 vertices (the inspector's world view with thousands of entities) is cut into
// runs of whole triangles that each fit.
void draw_command(njin::context &ctx, const ImDrawList &list, const ImDrawCmd &cmd, const njin::RenderScissor &clip,
                  ImVec2 off, ImVec2 display) {
  const ImDrawIdx *idx = list.IdxBuffer.Data + cmd.IdxOffset;
  const ImDrawVert *vtx = list.VtxBuffer.Data + cmd.VtxOffset;
  njin::MeshDraw draw{};
  draw.texture = njin::RenderTextureHandle{(std::uint64_t)cmd.GetTexID()};
  draw.linear_sampling = true;
  draw.depth_test = false;
  draw.blend = njin::RenderBlend::Alpha;
  draw.scissor_enabled = true;
  draw.scissor = clip;
  // Pixels (top-left origin, ImGui's DisplayPos at 0) to clip space, as Njin's own 2D
  // draws do (njin_draw_portable.cpp).
  const float sx = 2.0f / display.x, sy = -2.0f / display.y;
  draw.transform = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 1, 0, -1 - off.x * sx, 1 - off.y * sy, 0, 1};
  unsigned first = 0;
  while (first < cmd.ElemCount) {
    // The run: as many triangles as keep its index range under 65536.
    unsigned lo = UINT32_MAX, hi = 0, end = first;
    while (end + 3 <= cmd.ElemCount) {
      unsigned tlo = lo, thi = hi;
      for (unsigned k = 0; k < 3; ++k) {
        tlo = std::min<unsigned>(tlo, (unsigned)idx[end + k]);
        thi = std::max<unsigned>(thi, (unsigned)idx[end + k]);
      }
      if (thi - tlo >= 65536u && end > first)
        break;
      lo = tlo;
      hi = thi;
      end += 3;
    }
    if (end == first)
      break; // malformed command (fewer than 3 indices left)
    auto &vertices = g_host.vertices;
    auto &indices = g_host.indices;
    vertices.resize(hi - lo + 1);
    for (unsigned i = lo; i <= hi; ++i) {
      const ImDrawVert &v = vtx[i];
      njin::RenderVertex &o = vertices[i - lo];
      o = njin::RenderVertex{v.pos.x, v.pos.y, 0, v.uv.x, v.uv.y};
      o.color = {(float)((v.col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f, (float)((v.col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                 (float)((v.col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f, (float)((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f};
    }
    indices.resize(end - first);
    for (unsigned i = first; i < end; ++i)
      indices[i - first] = (std::uint16_t)((unsigned)idx[i] - lo);
    draw.vertices = vertices;
    draw.indices = indices;
    njin::renderer_draw_mesh(ctx, draw);
    first = end;
  }
}

} // namespace

bool imgui_init(njin::context &ctx, bool implot) {
  njin::SdlPlatform *platform = platform_of(ctx);
  if (platform == nullptr)
    return false;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  if (implot)
    ImPlot::CreateContext();
  if (!ImGui_ImplSDL3_InitForOther(platform->window())) {
    if (implot)
      ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return false;
  }
  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = "njin_renderer";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  platform->set_event_hook([](const SDL_Event &event) { ImGui_ImplSDL3_ProcessEvent(&event); });
  g_host.ctx = &ctx;
  g_host.implot = implot;
  return true;
}

void imgui_shutdown(njin::context &ctx) {
  if (g_host.ctx != &ctx)
    return;
  if (njin::SdlPlatform *platform = platform_of(ctx))
    platform->set_event_hook({});
  for (ImTextureData *tex : ImGui::GetPlatformIO().Textures)
    if (tex->RefCount == 1 && tex->TexID != ImTextureID_Invalid) {
      njin::renderer_destroy_texture(ctx, njin::RenderTextureHandle{(std::uint64_t)tex->TexID});
      tex->SetTexID(ImTextureID_Invalid);
      tex->SetStatus(ImTextureStatus_Destroyed);
    }
  ImGui_ImplSDL3_Shutdown();
  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = nullptr;
  io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
  if (g_host.implot)
    ImPlot::DestroyContext();
  ImGui::DestroyContext();
  g_host = host_state{};
}

void imgui_begin(njin::context &) {
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  g_host.frame = true;
}

void imgui_end(njin::context &) {
  if (!g_host.frame)
    return;
  ImGui::Render();
  g_host.frame = false;
}

void imgui_draw(njin::context &ctx) {
  ImDrawData *data = ImGui::GetDrawData();
  if (data == nullptr || !data->Valid)
    return;
  if (data->Textures != nullptr)
    for (ImTextureData *tex : *data->Textures)
      if (tex->Status != ImTextureStatus_OK)
        update_texture(ctx, tex);
  // Njin draws in the window's logical pixels, as ImGui lays out (DisplaySize is the
  // window size): no framebuffer scale here, Njin applies the window's own.
  const ImVec2 off = data->DisplayPos;
  for (const ImDrawList *list : data->CmdLists) {
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr) {
        if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
          cmd.UserCallback(list, &cmd);
        continue;
      }
      const float x0 = std::max(cmd.ClipRect.x - off.x, 0.0f), y0 = std::max(cmd.ClipRect.y - off.y, 0.0f);
      const float x1 = std::min(cmd.ClipRect.z - off.x, data->DisplaySize.x);
      const float y1 = std::min(cmd.ClipRect.w - off.y, data->DisplaySize.y);
      if (x1 <= x0 || y1 <= y0 || cmd.ElemCount == 0)
        continue;
      const njin::RenderScissor clip{(int)std::floor(x0), (int)std::floor(y0), (unsigned)std::ceil(x1 - std::floor(x0)),
                                     (unsigned)std::ceil(y1 - std::floor(y0))};
      draw_command(ctx, *list, cmd, clip, off, data->DisplaySize);
    }
  }
}

void imgui_systems(njin::context &ctx, void (*ui)(njin::context &)) {
  g_host.ui = ui;
  njin::mod_register(ctx, njin::mod_desc{"imgui", [](njin::context &c) {
                                           njin::ecs_register(c, njin::phase_pre_update, [](njin::context &x) { imgui_begin(x); },
                                                              "imgui_begin");
                                           njin::ecs_register(
                                               c, njin::phase_update,
                                               [](njin::context &x) {
                                                 if (g_host.ui)
                                                   g_host.ui(x);
                                                 imgui_end(x);
                                               },
                                               "imgui_ui");
                                           njin::ecs_register(c, njin::phase_post_render, [](njin::context &x) { imgui_draw(x); },
                                                              "imgui_draw");
                                         }});
}

} // namespace njin_tools
