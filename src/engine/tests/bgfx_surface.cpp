#include "njin_sdl_platform.h"
#include "njin_bgfx.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <stdexcept>

// Native-window regression: shut down while minimized, and also resume the same
// device. Vulkan validation output is checked by check_rhi_smoke.cmake.
int main() {
  try {
    njin::config cfg{.title="Njin Vulkan surface lifecycle",.width=160,.height=120,.target_fps=0};
    cfg.audio=false; cfg.resizable=true;
    cfg.graphics_backend=njin::GraphicsBackend::Vulkan; cfg.graphics_validation=true;
    for(int attempt=0;attempt<2;++attempt) {
      njin::SdlPlatform platform(cfg);
      auto device=njin::rhi::make_bgfx_device(platform,cfg);
      const auto frames=[&] {
        for(int i=0;i<30;++i) {
          platform.poll_close();
          device->begin_frame({0.1f,0.2f,0.3f,1}); device->end_frame();
          SDL_Delay(2);
        }
      };
      const auto state=[&](bool minimized) {
        for(int i=0;i<100;++i) {
          platform.poll_close();
          if(bool(SDL_GetWindowFlags(platform.window())&SDL_WINDOW_MINIMIZED)==minimized) return;
          SDL_Delay(10);
        }
        throw std::runtime_error("SDL minimize/restore acknowledgement timed out");
      };
      frames();
      if(!SDL_MinimizeWindow(platform.window())) throw std::runtime_error(SDL_GetError());
      state(true); frames();
      if(attempt==0) {
        if(!SDL_RestoreWindow(platform.window())) throw std::runtime_error(SDL_GetError());
        state(false); frames();
        njin::RenderImageAsset capture;
        device->begin_frame({0,1,0,1}); device->end_frame(&capture);
        const auto offset=(size_t(capture.height/2)*capture.width+capture.width/2)*4;
        if(capture.pixels.size()<offset+4 || capture.pixels[offset]!=0 ||
           capture.pixels[offset+1]!=255 || capture.pixels[offset+2]!=0)
          throw std::runtime_error("Vulkan did not resume rendering after minimize");
      }
      // attempt 1 intentionally destroys the device before restoring its window.
    }
    return 0;
  } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
