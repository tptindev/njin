#include "njin_gpu_caps.h"
#include <algorithm>
#include <cctype>

// raylib compiles GLFW in but does not export its header. glGetString is the
// only OpenGL call njin makes itself: raylib logs the renderer name and hides
// it, and the name is how a software renderer gives itself away.
extern "C" void (*glfwGetProcAddress(const char *name))(void);

namespace njin {
namespace {
constexpr unsigned int gl_vendor = 0x1F00;
constexpr unsigned int gl_renderer = 0x1F01;

std::string gl_string(unsigned int name) {
  using get_string_fn = const unsigned char *(*)(unsigned int);
  const get_string_fn get_string =
      reinterpret_cast<get_string_fn>(glfwGetProcAddress("glGetString"));
  const unsigned char *text = get_string != nullptr ? get_string(name) : nullptr;
  std::string out = text != nullptr ? reinterpret_cast<const char *>(text) : "";
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

bool looks_like_software(const std::string &name) {
  for (const char *marker : {"llvmpipe", "softpipe", "swrast", "software", "swiftshader",
                             "basic render", "gdi generic", "offscreen"}) {
    if (name.find(marker) != std::string::npos)
      return true;
  }
  return false;
}
} // namespace

std::string gpu_renderer_name() { return gl_string(gl_renderer); }
std::string gpu_vendor_name() { return gl_string(gl_vendor); }

bool gpu_is_software() {
  static const bool software =
      looks_like_software(gpu_renderer_name()) || looks_like_software(gpu_vendor_name());
  return software;
}
} // namespace njin
