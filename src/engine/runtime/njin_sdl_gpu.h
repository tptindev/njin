#pragma once
#include "njin_internal_only.h"
#include <memory>
#include <string>
#include "njin_renderer.h"
#include "njin_cfg.h"
#include "_math.h"
#include "njin_graphics_device.h"
namespace njin { struct input_store; }

namespace njin::rhi {
// Private platform/device boundary. Native resources never enter gameplay APIs.
class SdlGpuDevice final : public GraphicsDevice {
public:
  explicit SdlGpuDevice(const config &, bool validation);
  ~SdlGpuDevice() override;
  SdlGpuDevice(const SdlGpuDevice &) = delete;
  SdlGpuDevice &operator=(const SdlGpuDevice &) = delete;
  bool poll_close();
  void begin_frame(rgba) override;
  void end_frame(RenderImageAsset *capture=nullptr) override;
  void present_clear(float r, float g, float b, float a, RenderImageAsset *capture=nullptr);
  void draw(const MeshDraw &) override;
  void destroy_texture(RenderTextureHandle) override;
  void destroy_mesh(RenderMeshHandle) override;
  void create_target(RenderTargetHandle, RenderTextureHandle, unsigned, unsigned) override;
  void destroy_target(RenderTargetHandle, RenderTextureHandle) override;
  void begin_target(RenderTargetHandle, rgba) override;
  void end_target() override;
  std::uint64_t texture_uploads() const override;
  std::uint64_t mesh_uploads() const override;
  void poll_input(input_store &);
  void set_size(int width, int height);
  void size(float &width, float &height) const;
  void set_title(const char *);
  void set_position(vec2);
  vec2 position() const;
  rect monitor_bounds() const;
  void center();
  void set_fullscreen(bool);
  bool fullscreen() const;
  void set_vsync(bool);
  bool vsync() const;
  bool resized() const;
  void show_cursor(bool);
  void lock_cursor(bool);
  void *native_handle() const;
private:
  struct State;
  std::unique_ptr<State> state;
};
}
