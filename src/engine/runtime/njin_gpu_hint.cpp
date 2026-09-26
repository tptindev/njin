#include "njin_gpu_hint.h"

// Set by the NJIN_PREFER_DISCRETE_GPU option in CMake (on by default). Windows
// is not handled here but by njin::gpu: see src/engine/gpu/gpu_preference.cpp.
#ifndef NJIN_PREFER_DISCRETE_GPU
#define NJIN_PREFER_DISCRETE_GPU 1
#endif

#if defined(__linux__) && NJIN_PREFER_DISCRETE_GPU
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace njin {
#if defined(__linux__) && NJIN_PREFER_DISCRETE_GPU
namespace {
// A laptop, tablet or other portable, by the SMBIOS chassis type, or failing
// that by having a battery. Only there is the GPU that drives the screen the
// integrated one. On a desktop the monitor is usually on the discrete card, so
// "the other GPU" would be the integrated one: the offload variables below
// would make the game slower, not faster.
bool is_portable() {
  if (FILE *f = std::fopen("/sys/class/dmi/id/chassis_type", "r")) {
    int type = 0;
    const bool read = std::fscanf(f, "%d", &type) == 1;
    std::fclose(f);
    if (read) // 8 portable, 9 laptop, 10 notebook, 11 handheld, 14 sub notebook, 30 tablet, 31 convertible, 32 detachable
      return type == 8 || type == 9 || type == 10 || type == 11 || type == 14 || (type >= 30 && type <= 32);
  }
  bool battery = false;
  if (DIR *dir = opendir("/sys/class/power_supply")) {
    while (const dirent *entry = readdir(dir))
      battery = battery || (entry->d_name[0] == 'B' && entry->d_name[1] == 'A' && entry->d_name[2] == 'T');
    closedir(dir);
  }
  return battery;
}
} // namespace
#endif

void prefer_discrete_gpu() {
#if defined(__linux__) && NJIN_PREFER_DISCRETE_GPU
  if (!is_portable())
    return;
  // Every variable is set only if the user has not: an explicit DRI_PRIME=0
  // (or any other value) wins over this default.
  //
  // Mesa (Intel, AMD, nouveau): DRI_PRIME=1 renders on the other GPU. On a
  // machine with one GPU, or with the NVIDIA driver, it changes nothing.
  setenv("DRI_PRIME", "1", 0);
  // NVIDIA's own driver offloads with two more variables. They are set only
  // when the driver is loaded and its GLX library can be found: forcing the
  // vendor to a library that is not installed would leave the game with no
  // OpenGL at all.
  if (access("/proc/driver/nvidia/version", F_OK) == 0) {
    if (void *lib = dlopen("libGLX_nvidia.so.0", RTLD_LAZY | RTLD_LOCAL)) {
      dlclose(lib);
      setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);
      setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
    }
  }
#endif
}
} // namespace njin
