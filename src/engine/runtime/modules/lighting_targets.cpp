#include "lighting_internal.h"
#include "njin_log.h"
#include <raylib.h>
#include <rlgl.h>

namespace njin::light_impl {
// (Re)creates `target` at `w`x`h` when its size differs.
bool ensure_target(RenderTexture2D &target, i32 w, i32 h, bool bilinear) {
  if (IsRenderTextureValid(target) && target.texture.width == w && target.texture.height == h)
    return true;
  if (IsRenderTextureValid(target))
    UnloadRenderTexture(target);
  target = LoadRenderTexture(w, h);
  if (!IsRenderTextureValid(target))
    return false;
  SetTextureFilter(target.texture, bilinear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
  return true;
}

// Like ensure_target(), but the colour buffer is 16-bit float, so light can go past 1. Falls back to an
// ordinary image (and says so once) where that is not possible.
bool ensure_float_target(RenderTexture2D &t, i32 w, i32 h, bool &float_ok, bool full_precision) {
  const int format = full_precision ? RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32 : RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
  if (IsRenderTextureValid(t) && t.texture.width == w && t.texture.height == h)
    return true;
  if (IsRenderTextureValid(t))
    UnloadRenderTexture(t);
  t = RenderTexture2D{};
  if (float_ok) {
    t.id = rlLoadFramebuffer();
    if (t.id != 0) {
      rlEnableFramebuffer(t.id);
      t.texture.id = rlLoadTexture(nullptr, w, h, format, 1);
      t.texture.width = w, t.texture.height = h, t.texture.mipmaps = 1;
      t.texture.format = format;
      t.depth.id = rlLoadTextureDepth(w, h, true);
      t.depth.width = w, t.depth.height = h, t.depth.mipmaps = 1, t.depth.format = 19;
      rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
      rlFramebufferAttach(t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
      const bool complete = rlFramebufferComplete(t.id);
      rlDisableFramebuffer();
      if (!complete || t.texture.id == 0) {
        UnloadRenderTexture(t);
        t = RenderTexture2D{};
      }
    }
    if (!IsRenderTextureValid(t)) {
      NJIN_WARN("lighting: this GPU cannot render to a float texture, light is limited to 1 (no HDR)");
      float_ok = false;
    }
  }
  if (!float_ok)
    return ensure_target(t, w, h, true);
  SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
  return true;
}
} // namespace njin::light_impl
