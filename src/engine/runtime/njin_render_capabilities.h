#pragma once
#include "njin_internal_only.h"
#include "njin_cfg.h"
#include "njin_ctx_get.h"
#include <stdexcept>
#include <string>
namespace njin {
inline void require_opengl_backend(const context &ctx,const char *feature) {
  if(ctx_ref<config>(ctx).graphics_backend!=GraphicsBackend::OpenGL)
    throw std::runtime_error(std::string("njin bgfx: ")+feature+" is not ported");
}
}
