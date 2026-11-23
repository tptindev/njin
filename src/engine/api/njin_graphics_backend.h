#pragma once
#include <string_view>

namespace njin {
// OpenGL retains the raylib legacy meaning for source compatibility.
enum class GraphicsBackend { OpenGL, Vulkan, Auto, BgfxOpenGL, Direct3D12, Metal, OpenGLES, WebGL2 };
constexpr std::string_view graphics_backend_name(GraphicsBackend backend) noexcept {
  switch(backend) {
    case GraphicsBackend::OpenGL: return "legacy-opengl";
    case GraphicsBackend::Vulkan: return "vulkan";
    case GraphicsBackend::Auto: return "auto";
    case GraphicsBackend::BgfxOpenGL: return "bgfx-opengl";
    case GraphicsBackend::Direct3D12: return "d3d12";
    case GraphicsBackend::Metal: return "metal";
    case GraphicsBackend::OpenGLES: return "gles";
    case GraphicsBackend::WebGL2: return "webgl2";
    default: return "invalid";
  }
}

// Invalid input leaves the caller's selection unchanged. No implicit fallback.
constexpr bool parse_graphics_backend(std::string_view name, GraphicsBackend &out) noexcept {
  if (name == "opengl" || name == "legacy-opengl") { out = GraphicsBackend::OpenGL; return true; }
  if (name == "vulkan") { out = GraphicsBackend::Vulkan; return true; }
  if (name == "auto") { out = GraphicsBackend::Auto; return true; }
  if (name == "bgfx-opengl") { out = GraphicsBackend::BgfxOpenGL; return true; }
  if (name == "d3d12") { out = GraphicsBackend::Direct3D12; return true; }
  if (name == "metal") { out = GraphicsBackend::Metal; return true; }
  if (name == "gles") { out = GraphicsBackend::OpenGLES; return true; }
  if (name == "webgl2") { out = GraphicsBackend::WebGL2; return true; }
  return false;
}
}
