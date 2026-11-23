#pragma once
#include "njin_internal_only.h"

// Only legacy GL helpers use this loader. It never creates a context.
#if defined(__ANDROID__)
#include <EGL/egl.h>
#else
extern "C" void (*glfwGetProcAddress(const char *name))(void);
#endif

namespace njin::detail {
using gl_proc = void (*)(void);
inline gl_proc gl_proc_address(const char *name) {
#if defined(__ANDROID__)
  return reinterpret_cast<gl_proc>(eglGetProcAddress(name));
#else
  return glfwGetProcAddress(name);
#endif
}
}
