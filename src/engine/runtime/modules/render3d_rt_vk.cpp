// The Vulkan side of ray tracing; see render3d_rt_vk.h. Built only with
// NJIN_RAYTRACING (CMake), on Windows.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include "render3d_rt_vk.h"
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

// NVRHI built as a static library leaves the Vulkan-Hpp dispatcher to the
// program: it lives here. NVRHI and this file must agree on NDEBUG (the
// dispatcher's layout changes with it), which one CMake build ensures.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace njin {
extern const unsigned char rt_shadow_spv[];
extern const unsigned long long rt_shadow_spv_size;
extern const unsigned char rt_denoise_spv[];
extern const unsigned long long rt_denoise_spv_size;
extern const unsigned char rt_reflect_spv[];
extern const unsigned long long rt_reflect_spv_size;
extern const unsigned char rt_gi_spv[];
extern const unsigned long long rt_gi_spv_size;
void rt_log(int level, const char *text); // render3d_rt.cpp: through njin's log
} // namespace njin

namespace njin::rtvk {
namespace {
// GL_EXT_memory_object(_win32), GL_EXT_semaphore(_win32).
typedef void(APIENTRY *PFN_CreateMemoryObjects)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_DeleteMemoryObjects)(GLsizei, const GLuint *);
typedef void(APIENTRY *PFN_ImportMemoryWin32Handle)(GLuint, unsigned long long, GLenum, void *);
typedef void(APIENTRY *PFN_TexStorageMem2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, unsigned long long);
typedef void(APIENTRY *PFN_MemoryObjectParameteriv)(GLuint, GLenum, const GLint *);
typedef void(APIENTRY *PFN_GenSemaphores)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_DeleteSemaphores)(GLsizei, const GLuint *);
typedef void(APIENTRY *PFN_ImportSemaphoreWin32Handle)(GLuint, GLenum, void *);
typedef void(APIENTRY *PFN_SignalSemaphore)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *);
typedef void(APIENTRY *PFN_WaitSemaphore)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *);
constexpr GLenum GL_HANDLE_TYPE_OPAQUE_WIN32_ = 0x9587, GL_TEXTURE_TILING_ = 0x9580, GL_OPTIMAL_TILING_ = 0x9584,
                 GL_DEDICATED_MEMORY_OBJECT_ = 0x9581, GL_RGBA8_ = 0x8058, GL_R32F_ = 0x822E,
                 GL_RGBA16F_ = 0x881A, GL_RGBA32F_ = 0x8814,
                 GL_LAYOUT_GENERAL_ = 0x958D;

struct gl_fns {
  PFN_CreateMemoryObjects create_memory = nullptr;
  PFN_DeleteMemoryObjects delete_memory = nullptr;
  PFN_ImportMemoryWin32Handle import_memory = nullptr;
  PFN_TexStorageMem2D tex_storage = nullptr;
  PFN_MemoryObjectParameteriv memory_parameter = nullptr;
  PFN_GenSemaphores gen_semaphores = nullptr;
  PFN_DeleteSemaphores delete_semaphores = nullptr;
  PFN_ImportSemaphoreWin32Handle import_semaphore = nullptr;
  PFN_SignalSemaphore signal = nullptr;
  PFN_WaitSemaphore wait = nullptr;
};

struct mesh_entry {
  nvrhi::rt::AccelStructHandle as;
  nvrhi::BufferHandle vb, ib;
  uint32_t kind = 0; // indices: 0 none, 1 16-bit, 2 32-bit
  uint32_t version = 0;
  uint32_t used = 0; // trace count when last used
};

// One image both sides use: the Vulkan texture, its memory as a GL texture.
struct shared_image {
  nvrhi::TextureHandle tex;
  GLuint gl = 0, mem = 0;
};

// rt/common.glsl's Params, the same order.
struct params_t {
  float inv_view_proj[16];
  float eye[4];
  float sun[4];
  float size[4];
  float lamps[3][4];
  float lamp_info[4];
  float sun_color[4];
  float ambient[4];
  float env_sh[9][4];
  float light_pos[8][4];
  float light_color[8][4];
  float light_spot[8][4];
  float light_info[4];
  float grid_min[4];
  float grid_size[4];
  float grid_count[4];
};

// rt/common.glsl's inst_t: a mesh's buffers by address, colour and glow.
struct inst_t {
  uint64_t vb, ib;
  uint32_t kind[4];
  float albedo[4];
  float emission[4];
};
static_assert(sizeof(inst_t) == 64);

struct log_cb : nvrhi::IMessageCallback {
  void message(nvrhi::MessageSeverity s, const char *text) override {
    rt_log(s == nvrhi::MessageSeverity::Error || s == nvrhi::MessageSeverity::Fatal ? 2 : 1, text);
  }
};

struct state {
  bool tried = false, ok = false;
  HMODULE lib = nullptr;
  vk::Instance instance;
  vk::PhysicalDevice gpu;
  vk::Device device;
  int queue_family = -1;
  nvrhi::DeviceHandle nv;
  nvrhi::CommandListHandle build_cmd, cmd;
  bool building = false; // build_cmd is open
  gl_fns gl;
  bool gl_semaphores = false;
  vk::Semaphore to_vk, to_gl; // GL signals to_vk; Vulkan signals to_gl
  GLuint gl_to_vk = 0, gl_to_gl = 0;
  std::unordered_map<uint64_t, mesh_entry> meshes;
  std::unordered_map<uint64_t, const mesh_entry *> by_as; // BLAS id -> its entry
  // Replaced or dropped meshes, kept a few traces: the shaders read their
  // buffers by address, which NVRHI does not track, so a trace still running
  // on the GPU may read them.
  std::vector<std::pair<uint32_t, mesh_entry>> retired;
  uint32_t traces = 0;
  int w = 0, h = 0;
  shared_image depth, mask, refl, grid, grid_depth;
  int refl_w = 0, refl_h = 0, grid_points = 0;
  bool grid_clear = false; // the grid was made: clear it before use
  nvrhi::TextureHandle raw, no_grid, no_depth; // a black grid and depth for reflections without a grid
  nvrhi::BufferHandle inst_buf;
  size_t inst_capacity = 0;
  std::vector<inst_t> inst_data;
  nvrhi::BufferHandle params;
  nvrhi::rt::AccelStructHandle tlas;
  size_t tlas_capacity = 0;
  nvrhi::ShaderHandle shadow_cs, denoise_cs, reflect_cs, gi_cs;
  nvrhi::BindingLayoutHandle shadow_layout, denoise_layout, reflect_layout, gi_layout;
  nvrhi::BindingSetHandle shadow_set, denoise_set, reflect_set, gi_set;
  nvrhi::ComputePipelineHandle shadow_pipe, denoise_pipe, reflect_pipe, gi_pipe;
  // What the binding sets were made for; they are made again when it changes.
  const void *sets_key[7]{};
  bool no_grid_clear = true;
  std::vector<nvrhi::rt::InstanceDesc> instances;
};

// Outside `state`, which is reset by assignment on shutdown.
log_cb &logger() {
  static log_cb l;
  return l;
}

state &st() {
  static state s;
  return s;
}

bool fail(const char *why) {
  rt_log(1, why);
  return false;
}

template <typename T> T gl_proc(const char *name) { return (T)(void *)wglGetProcAddress(name); }

bool load_gl(gl_fns &g, bool &semaphores) {
  g.create_memory = gl_proc<PFN_CreateMemoryObjects>("glCreateMemoryObjectsEXT");
  g.delete_memory = gl_proc<PFN_DeleteMemoryObjects>("glDeleteMemoryObjectsEXT");
  g.import_memory = gl_proc<PFN_ImportMemoryWin32Handle>("glImportMemoryWin32HandleEXT");
  g.tex_storage = gl_proc<PFN_TexStorageMem2D>("glTexStorageMem2DEXT");
  g.memory_parameter = gl_proc<PFN_MemoryObjectParameteriv>("glMemoryObjectParameterivEXT");
  g.gen_semaphores = gl_proc<PFN_GenSemaphores>("glGenSemaphoresEXT");
  g.delete_semaphores = gl_proc<PFN_DeleteSemaphores>("glDeleteSemaphoresEXT");
  g.import_semaphore = gl_proc<PFN_ImportSemaphoreWin32Handle>("glImportSemaphoreWin32HandleEXT");
  g.signal = gl_proc<PFN_SignalSemaphore>("glSignalSemaphoreEXT");
  g.wait = gl_proc<PFN_WaitSemaphore>("glWaitSemaphoreEXT");
  semaphores = g.gen_semaphores && g.import_semaphore && g.signal && g.wait && g.delete_semaphores;
  return g.create_memory && g.import_memory && g.tex_storage && g.memory_parameter && g.delete_memory;
}

// The ray tracing pipeline extension is not used by the shaders, but NVRHI's
// buffers and barriers name its stages and usages.
const char *const device_exts[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                   VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                                   VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                                   VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                                   VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};

// The GPU OpenGL runs on (by name), if it has ray queries: the images can only
// be shared on one GPU. `other` is set when another GPU has them.
vk::PhysicalDevice pick_gpu(vk::Instance instance, bool &other) {
  const char *gl_name = (const char *)glGetString(GL_RENDERER);
  other = false;
  for (vk::PhysicalDevice d : instance.enumeratePhysicalDevices()) {
    const auto have = d.enumerateDeviceExtensionProperties();
    bool all = true;
    for (const char *e : device_exts) {
      bool found = false;
      for (const auto &p : have)
        found = found || std::strcmp(p.extensionName, e) == 0;
      all = all && found;
    }
    if (!all)
      continue;
    const vk::PhysicalDeviceProperties props = d.getProperties();
    const char *name = props.deviceName.data();
    if (gl_name != nullptr && std::strncmp(gl_name, name, std::strlen(name)) == 0)
      return d;
    other = true;
  }
  return {};
}

std::vector<uint32_t> words(const unsigned char *bytes, unsigned long long size) {
  std::vector<uint32_t> out((size_t)(size / 4));
  std::memcpy(out.data(), bytes, out.size() * 4);
  return out;
}

bool make_pipelines(state &s) {
  const std::vector<uint32_t> shadow = words(rt_shadow_spv, rt_shadow_spv_size);
  const std::vector<uint32_t> denoise = words(rt_denoise_spv, rt_denoise_spv_size);
  const std::vector<uint32_t> reflect = words(rt_reflect_spv, rt_reflect_spv_size);
  const std::vector<uint32_t> gi = words(rt_gi_spv, rt_gi_spv_size);
  s.shadow_cs = s.nv->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute), shadow.data(),
                                   shadow.size() * 4);
  s.denoise_cs = s.nv->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute), denoise.data(),
                                    denoise.size() * 4);
  s.shadow_layout = s.nv->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                  .setVisibility(nvrhi::ShaderType::Compute)
                                                  .addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0))
                                                  .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                                                  .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
                                                  .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1)));
  s.denoise_layout = s.nv->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                   .setVisibility(nvrhi::ShaderType::Compute)
                                                   .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                                                   .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
                                                   .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1))
                                                   .addItem(nvrhi::BindingLayoutItem::Texture_UAV(2)));
  s.reflect_cs = s.nv->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute), reflect.data(),
                                    reflect.size() * 4);
  s.gi_cs = s.nv->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute), gi.data(), gi.size() * 4);
  s.reflect_layout = s.nv->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                   .setVisibility(nvrhi::ShaderType::Compute)
                                                   .addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0))
                                                   .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))
                                                   .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                                                   .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
                                                   .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1))
                                                   .addItem(nvrhi::BindingLayoutItem::Texture_UAV(2))
                                                   .addItem(nvrhi::BindingLayoutItem::Texture_UAV(3)));
  s.gi_layout = s.nv->createBindingLayout(nvrhi::BindingLayoutDesc()
                                              .setVisibility(nvrhi::ShaderType::Compute)
                                              .addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0))
                                              .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))
                                              .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                                              .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
                                              .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1)));
  if (!s.shadow_cs || !s.denoise_cs || !s.reflect_cs || !s.gi_cs || !s.shadow_layout || !s.denoise_layout ||
      !s.reflect_layout || !s.gi_layout)
    return false;
  s.reflect_pipe = s.nv->createComputePipeline(
      nvrhi::ComputePipelineDesc().setComputeShader(s.reflect_cs).addBindingLayout(s.reflect_layout));
  s.gi_pipe =
      s.nv->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(s.gi_cs).addBindingLayout(s.gi_layout));
  s.no_grid = s.nv->createTexture(nvrhi::TextureDesc()
                                      .setWidth(1)
                                      .setHeight(6)
                                      .setFormat(nvrhi::Format::RGBA32_FLOAT)
                                      .setIsUAV(true)
                                      .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                      .setKeepInitialState(true)
                                      .setDebugName("njin rt no grid"));
  s.no_depth = s.nv->createTexture(nvrhi::TextureDesc()
                                       .setWidth(16)
                                       .setHeight(16)
                                       .setFormat(nvrhi::Format::RGBA32_FLOAT)
                                       .setIsUAV(true)
                                       .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                       .setKeepInitialState(true)
                                       .setDebugName("njin rt no grid depth"));
  s.shadow_pipe = s.nv->createComputePipeline(
      nvrhi::ComputePipelineDesc().setComputeShader(s.shadow_cs).addBindingLayout(s.shadow_layout));
  s.denoise_pipe = s.nv->createComputePipeline(
      nvrhi::ComputePipelineDesc().setComputeShader(s.denoise_cs).addBindingLayout(s.denoise_layout));
  s.params = s.nv->createBuffer(nvrhi::BufferDesc()
                                    .setByteSize(sizeof(params_t))
                                    .setIsConstantBuffer(true)
                                    .setIsVolatile(true)
                                    .setMaxVersions(64)
                                    .setDebugName("njin rt params"));
  return s.shadow_pipe && s.denoise_pipe && s.reflect_pipe && s.gi_pipe && s.no_grid && s.no_depth && s.params;
}

// Drops the GL errors left by earlier calls, so the checks below see their own.
void clear_gl_errors() {
  for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {
  }
}

bool make_semaphores(state &s) try {
  clear_gl_errors();
  for (vk::Semaphore *sem : {&s.to_vk, &s.to_gl}) {
    vk::ExportSemaphoreCreateInfo exp(vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32);
    vk::SemaphoreCreateInfo ci({}, &exp);
    *sem = s.device.createSemaphore(ci);
  }
  for (int i = 0; i < 2; i++) {
    vk::Semaphore sem = i == 0 ? s.to_vk : s.to_gl;
    HANDLE h = s.device.getSemaphoreWin32HandleKHR(
        vk::SemaphoreGetWin32HandleInfoKHR(sem, vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32));
    GLuint &g = i == 0 ? s.gl_to_vk : s.gl_to_gl;
    s.gl.gen_semaphores(1, &g);
    s.gl.import_semaphore(g, GL_HANDLE_TYPE_OPAQUE_WIN32_, h);
  }
  return glGetError() == GL_NO_ERROR;
} catch (const std::exception &) {
  return false;
}

bool start(state &s) {
  if (!load_gl(s.gl, s.gl_semaphores))
    return fail("rt: the OpenGL driver has no GL_EXT_memory_object_win32; ray tracing is off");
  s.lib = LoadLibraryA("vulkan-1.dll");
  if (s.lib == nullptr)
    return fail("rt: no Vulkan driver (vulkan-1.dll); ray tracing is off");
  auto gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(s.lib, "vkGetInstanceProcAddr");
  if (gipa == nullptr)
    return fail("rt: vulkan-1.dll has no vkGetInstanceProcAddr; ray tracing is off");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(gipa);
  vk::ApplicationInfo app("njin", 1, "njin", 1, VK_API_VERSION_1_3);
  s.instance = vk::createInstance(vk::InstanceCreateInfo({}, &app));
  VULKAN_HPP_DEFAULT_DISPATCHER.init(s.instance);
  bool other = false;
  s.gpu = pick_gpu(s.instance, other);
  if (!s.gpu && other)
    return fail("rt: OpenGL runs on a GPU without ray tracing while another GPU has it; link njin::gpu "
                "into the game (target_link_libraries(<game> PRIVATE njin::gpu)) so it runs on that one; "
                "ray tracing is off");
  if (!s.gpu)
    return fail("rt: no GPU with Vulkan ray queries; ray tracing is off");
  const auto families = s.gpu.getQueueFamilyProperties();
  for (size_t i = 0; i < families.size() && s.queue_family < 0; i++)
    if (families[i].queueFlags & vk::QueueFlagBits::eGraphics)
      s.queue_family = (int)i;
  const float prio = 1.0f;
  vk::DeviceQueueCreateInfo qci({}, (uint32_t)s.queue_family, 1, &prio);
  vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtp;
  rtp.rayTracingPipeline = true;
  vk::PhysicalDeviceRayQueryFeaturesKHR rq;
  rq.rayQuery = true;
  rq.pNext = &rtp;
  vk::PhysicalDeviceAccelerationStructureFeaturesKHR asf;
  asf.accelerationStructure = true;
  asf.pNext = &rq;
  vk::PhysicalDeviceVulkan13Features v13;
  v13.synchronization2 = true;
  v13.dynamicRendering = true;
  v13.pNext = &asf;
  vk::PhysicalDeviceVulkan12Features v12;
  v12.timelineSemaphore = true;
  v12.bufferDeviceAddress = true;
  v12.descriptorIndexing = true;
  v12.runtimeDescriptorArray = true;
  v12.pNext = &v13;
  vk::PhysicalDeviceFeatures2 f2;
  f2.features.shaderInt64 = true;
  f2.pNext = &v12;
  vk::DeviceCreateInfo dci({}, qci, {}, device_exts);
  dci.pNext = &f2;
  s.device = s.gpu.createDevice(dci);
  VULKAN_HPP_DEFAULT_DISPATCHER.init(s.device);

  // Value-initialised: the queue members have no defaults.
  nvrhi::vulkan::DeviceDesc desc{};
  desc.errorCB = &logger();
  desc.instance = s.instance;
  desc.physicalDevice = s.gpu;
  desc.device = s.device;
  desc.graphicsQueue = s.device.getQueue((uint32_t)s.queue_family, 0);
  desc.graphicsQueueIndex = s.queue_family;
  desc.transferQueue = VK_NULL_HANDLE;
  desc.computeQueue = VK_NULL_HANDLE;
  desc.deviceExtensions = (const char **)device_exts;
  desc.numDeviceExtensions = sizeof(device_exts) / sizeof(device_exts[0]);
  desc.bufferDeviceAddressSupported = true;
  s.nv = nvrhi::vulkan::createDevice(desc);
  if (!s.nv || !s.nv->queryFeatureSupport(nvrhi::Feature::RayQuery))
    return fail("rt: NVRHI could not start on Vulkan with ray queries; ray tracing is off");
  s.build_cmd = s.nv->createCommandList();
  s.cmd = s.nv->createCommandList();
  if (!make_pipelines(s))
    return fail("rt: the ray tracing shaders failed to load; ray tracing is off");
  if (s.gl_semaphores && !make_semaphores(s)) {
    rt_log(1, "rt: shared semaphores failed; the GPU sides wait for each other whole (slower)");
    s.gl_semaphores = false;
  }
  char line[256];
  const vk::PhysicalDeviceProperties props = s.gpu.getProperties();
  std::snprintf(line, sizeof(line), "rt: Vulkan ray queries on %s%s", props.deviceName.data(),
                s.gl_semaphores ? "" : " (no shared semaphores)");
  rt_log(0, line);
  return true;
}

void free_image(state &s, shared_image &img) {
  if (img.gl != 0)
    glDeleteTextures(1, &img.gl);
  if (img.mem != 0)
    s.gl.delete_memory(1, &img.mem);
  img = shared_image{};
}

// A Vulkan image of `format` with its memory exported, imported as a GL texture.
bool make_image(state &s, shared_image &img, int w, int h, nvrhi::Format format, GLenum gl_format, const char *name) {
  img.tex = s.nv->createTexture(nvrhi::TextureDesc()
                                    .setWidth((uint32_t)w)
                                    .setHeight((uint32_t)h)
                                    .setFormat(format)
                                    .setIsUAV(true)
                                    .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                    .setKeepInitialState(true)
                                    .setSharedResourceFlags(nvrhi::SharedResourceFlags::Shared)
                                    .setDebugName(name));
  if (!img.tex)
    return false;
  clear_gl_errors();
  void *handle = img.tex->getNativeObject(nvrhi::ObjectTypes::SharedHandle);
  const VkImage image = (VkImage)img.tex->getNativeObject(nvrhi::ObjectTypes::VK_Image).pointer;
  const vk::MemoryRequirements req = s.device.getImageMemoryRequirements(vk::Image(image));
  s.gl.create_memory(1, &img.mem);
  const GLint dedicated = GL_TRUE;
  s.gl.memory_parameter(img.mem, GL_DEDICATED_MEMORY_OBJECT_, &dedicated);
  s.gl.import_memory(img.mem, req.size, GL_HANDLE_TYPE_OPAQUE_WIN32_, handle);
  glGenTextures(1, &img.gl);
  glBindTexture(GL_TEXTURE_2D, img.gl);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_, GL_OPTIMAL_TILING_);
  s.gl.tex_storage(GL_TEXTURE_2D, 1, gl_format, w, h, img.mem, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
  return glGetError() == GL_NO_ERROR;
}

// The binding sets of what this trace runs; without a grid, a black one.
void make_sets(state &s, bool depth, bool reflect, bool gi) {
  nvrhi::ITexture *grid = s.grid.tex ? s.grid.tex.Get() : s.no_grid.Get();
  nvrhi::ITexture *grid_depth = s.grid_depth.tex ? s.grid_depth.tex.Get() : s.no_depth.Get();
  s.shadow_set = s.denoise_set = s.reflect_set = s.gi_set = nullptr;
  if (depth) {
    s.shadow_set = s.nv->createBindingSet(nvrhi::BindingSetDesc()
                                              .addItem(nvrhi::BindingSetItem::RayTracingAccelStruct(0, s.tlas))
                                              .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, s.params))
                                              .addItem(nvrhi::BindingSetItem::Texture_UAV(0, s.depth.tex))
                                              .addItem(nvrhi::BindingSetItem::Texture_UAV(1, s.raw)),
                                          s.shadow_layout);
    s.denoise_set = s.nv->createBindingSet(nvrhi::BindingSetDesc()
                                               .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, s.params))
                                               .addItem(nvrhi::BindingSetItem::Texture_UAV(0, s.depth.tex))
                                               .addItem(nvrhi::BindingSetItem::Texture_UAV(1, s.raw))
                                               .addItem(nvrhi::BindingSetItem::Texture_UAV(2, s.mask.tex)),
                                           s.denoise_layout);
  }
  if (depth && reflect && s.refl.tex)
    s.reflect_set = s.nv->createBindingSet(nvrhi::BindingSetDesc()
                                               .addItem(nvrhi::BindingSetItem::RayTracingAccelStruct(0, s.tlas))
                                               .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, s.inst_buf))
                                               .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, s.params))
                                               .addItem(nvrhi::BindingSetItem::Texture_UAV(0, s.depth.tex))
                                               .addItem(nvrhi::BindingSetItem::Texture_UAV(1, s.refl.tex))
                                               .addItem(nvrhi::BindingSetItem::Texture_UAV(2, grid))
                                               .addItem(nvrhi::BindingSetItem::Texture_UAV(3, grid_depth)),
                                           s.reflect_layout);
  if (gi && s.grid.tex)
    s.gi_set = s.nv->createBindingSet(nvrhi::BindingSetDesc()
                                          .addItem(nvrhi::BindingSetItem::RayTracingAccelStruct(0, s.tlas))
                                          .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, s.inst_buf))
                                          .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, s.params))
                                          .addItem(nvrhi::BindingSetItem::Texture_UAV(0, s.grid.tex))
                                          .addItem(nvrhi::BindingSetItem::Texture_UAV(1, s.grid_depth.tex)),
                                      s.gi_layout);
}

nvrhi::BufferHandle upload(state &s, const void *data, size_t bytes) {
  nvrhi::BufferHandle b = s.nv->createBuffer(nvrhi::BufferDesc()
                                                 .setByteSize(bytes)
                                                 .setIsAccelStructBuildInput(true)
                                                 .setInitialState(nvrhi::ResourceStates::AccelStructBuildInput)
                                                 .setKeepInitialState(true));
  s.build_cmd->writeBuffer(b, data, bytes);
  return b;
}
} // namespace

bool init() {
  state &s = st();
  if (!s.tried) {
    s.tried = true;
    // Vulkan-Hpp reports failures as exceptions.
    try {
      s.ok = start(s);
    } catch (const std::exception &e) {
      char line[256];
      std::snprintf(line, sizeof(line), "rt: Vulkan failed (%s); ray tracing is off", e.what());
      rt_log(1, line);
      s.ok = false;
    }
    if (!s.ok)
      shutdown();
    s.tried = true;
  }
  return s.ok;
}

bool ready() { return st().ok; }

void shutdown() {
  state &s = st();
  if (s.nv)
    s.nv->waitForIdle();
  if (s.building && s.build_cmd)
    s.build_cmd->close();
  s.building = false;
  free_image(s, s.depth);
  free_image(s, s.mask);
  free_image(s, s.refl);
  free_image(s, s.grid);
  free_image(s, s.grid_depth);
  if (s.gl.delete_semaphores != nullptr) {
    for (GLuint *g : {&s.gl_to_vk, &s.gl_to_gl})
      if (*g != 0)
        s.gl.delete_semaphores(1, g);
  }
  s.gl_to_vk = s.gl_to_gl = 0;
  s.by_as.clear();
  s.retired.clear();
  s.meshes.clear();
  s.instances.clear();
  s.shadow_set = s.denoise_set = s.reflect_set = s.gi_set = nullptr;
  s.shadow_pipe = s.denoise_pipe = s.reflect_pipe = s.gi_pipe = nullptr;
  s.shadow_layout = s.denoise_layout = s.reflect_layout = s.gi_layout = nullptr;
  s.shadow_cs = s.denoise_cs = s.reflect_cs = s.gi_cs = nullptr;
  s.raw = s.no_grid = s.no_depth = nullptr;
  s.inst_buf = nullptr;
  s.params = nullptr;
  s.tlas = nullptr;
  s.build_cmd = s.cmd = nullptr;
  if (s.nv)
    s.nv->waitForIdle();
  s.nv = nullptr;
  if (s.device) {
    for (vk::Semaphore *sem : {&s.to_vk, &s.to_gl})
      if (*sem)
        s.device.destroySemaphore(*sem);
    s.device.destroy();
  }
  if (s.instance)
    s.instance.destroy();
  const bool tried = s.tried;
  HMODULE lib = s.lib;
  s = state{};
  s.tried = tried;
  if (lib != nullptr)
    FreeLibrary(lib);
}

uint64_t mesh(uint64_t key, uint32_t version, const mesh_in &m) {
  state &s = st();
  if (!s.ok || m.pos == nullptr || m.vertex_count < 3)
    return 0;
  auto it = s.meshes.find(key);
  if (it != s.meshes.end() && it->second.version == version) {
    it->second.used = s.traces;
    return (uint64_t)(uintptr_t)it->second.as.Get();
  }
  if (!s.building) {
    s.build_cmd->open();
    s.building = true;
  }
  mesh_entry e;
  e.version = version;
  e.used = s.traces;
  e.vb = upload(s, m.pos, (size_t)m.vertex_count * 12);
  auto tri = nvrhi::rt::GeometryTriangles()
                 .setVertexBuffer(e.vb)
                 .setVertexFormat(nvrhi::Format::RGB32_FLOAT)
                 .setVertexStride(12)
                 .setVertexCount(m.vertex_count);
  if (m.index_count > 0 && (m.idx16 != nullptr || m.idx32 != nullptr)) {
    const bool wide = m.idx32 != nullptr;
    e.kind = wide ? 2 : 1;
    if (wide) {
      e.ib = upload(s, m.idx32, (size_t)m.index_count * 4);
    } else {
      // Whole 32-bit words, as the shaders read them.
      std::vector<uint16_t> padded(m.idx16, m.idx16 + m.index_count);
      if (padded.size() % 2 != 0)
        padded.push_back(0);
      e.ib = upload(s, padded.data(), padded.size() * 2);
    }
    tri.setIndexBuffer(e.ib).setIndexFormat(wide ? nvrhi::Format::R32_UINT : nvrhi::Format::R16_UINT).setIndexCount(m.index_count);
  }
  auto geo = nvrhi::rt::GeometryDesc().setTriangles(tri).setFlags(nvrhi::rt::GeometryFlags::Opaque);
  e.as = s.nv->createAccelStruct(nvrhi::rt::AccelStructDesc()
                                     .addBottomLevelGeometry(geo)
                                     .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace));
  if (!e.as)
    return 0;
  s.build_cmd->buildBottomLevelAccelStruct(e.as, &geo, 1);
  const uint64_t id = (uint64_t)(uintptr_t)e.as.Get();
  if (it != s.meshes.end()) {
    s.by_as.erase((uint64_t)(uintptr_t)it->second.as.Get());
    s.retired.push_back({s.traces, it->second});
  }
  mesh_entry &kept = s.meshes[key];
  kept = e;
  s.by_as[id] = &kept;
  return id;
}

bool targets(int w, int h, unsigned &gl_depth, unsigned &gl_mask) {
  state &s = st();
  if (!s.ok || w <= 0 || h <= 0)
    return false;
  if (s.w != w || s.h != h || !s.depth.tex) {
    s.nv->waitForIdle();
    free_image(s, s.depth);
    free_image(s, s.mask);
    s.raw = s.nv->createTexture(nvrhi::TextureDesc()
                                    .setWidth((uint32_t)w)
                                    .setHeight((uint32_t)h)
                                    .setFormat(nvrhi::Format::RGBA8_UNORM)
                                    .setIsUAV(true)
                                    .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                    .setKeepInitialState(true)
                                    .setDebugName("njin rt raw shadows"));
    if (!s.raw || !make_image(s, s.depth, w, h, nvrhi::Format::R32_FLOAT, GL_R32F_, "njin rt depth") ||
        !make_image(s, s.mask, w, h, nvrhi::Format::RGBA8_UNORM, GL_RGBA8_, "njin rt shadows")) {
      rt_log(1, "rt: could not share the images with OpenGL; ray tracing is off");
      s.ok = false;
      return false;
    }
    s.w = w;
    s.h = h;
  }
  gl_depth = s.depth.gl;
  gl_mask = s.mask.gl;
  return true;
}

bool reflect_target(int w, int h, unsigned &gl_refl) {
  state &s = st();
  if (!s.ok || w <= 0 || h <= 0)
    return false;
  if (s.refl_w != w || s.refl_h != h || !s.refl.tex) {
    s.nv->waitForIdle();
    free_image(s, s.refl);
    s.refl_w = s.refl_h = 0;
    if (!make_image(s, s.refl, w, h, nvrhi::Format::RGBA16_FLOAT, GL_RGBA16F_, "njin rt reflections")) {
      free_image(s, s.refl);
      rt_log(1, "rt: could not share the reflections' image with OpenGL; ray traced reflections are off");
      return false;
    }
    s.refl_w = w;
    s.refl_h = h;
  }
  gl_refl = s.refl.gl;
  return true;
}

bool grid_target(int points, unsigned &gl_grid, unsigned &gl_depth, bool &fresh) {
  state &s = st();
  fresh = false;
  if (!s.ok || points <= 0)
    return false;
  if (s.grid_points != points || !s.grid.tex) {
    s.nv->waitForIdle();
    free_image(s, s.grid);
    free_image(s, s.grid_depth);
    s.grid_points = 0;
    if (!make_image(s, s.grid, points, 6, nvrhi::Format::RGBA32_FLOAT, GL_RGBA32F_, "njin rt irradiance grid") ||
        !make_image(s, s.grid_depth, points * 16, 16, nvrhi::Format::RGBA32_FLOAT, GL_RGBA32F_,
                    "njin rt irradiance grid depth")) {
      free_image(s, s.grid);
      free_image(s, s.grid_depth);
      rt_log(1, "rt: could not share the irradiance grid with OpenGL; the ray traced grid is off");
      return false;
    }
    s.grid_points = points;
    s.grid_clear = true;
    fresh = true;
  }
  gl_grid = s.grid.gl;
  gl_depth = s.grid_depth.gl;
  return true;
}

void grid_reset() { st().grid_clear = true; }

bool trace(const instance_in *inst, uint32_t count, const trace_in &t) {
  state &s = st();
  // The shadows and reflections start from the pass's depth.
  const bool depth = t.shadows || t.reflect;
  const bool reflect = t.reflect && s.refl.tex && s.refl_w == t.width && s.refl_h == t.height;
  const bool gi = t.gi && s.grid.tex && s.grid_points == (int)(t.grid_count[0] * t.grid_count[1] * t.grid_count[2]);
  if (!s.ok || (depth && (!s.depth.tex || t.width != s.w || t.height != s.h)) || (!depth && !gi))
    return false;
  s.traces++;
  // The meshes built this pass go first, on the same queue.
  if (s.building) {
    s.build_cmd->close();
    s.nv->executeCommandList(s.build_cmd);
    s.building = false;
  }
  // The scene and its instances' data: grown, never shrunk.
  const size_t want = count > 0 ? count : 1;
  if (!s.tlas || s.tlas_capacity < want) {
    s.tlas_capacity = std::max<size_t>(want + want / 2, 64);
    s.tlas = s.nv->createAccelStruct(nvrhi::rt::AccelStructDesc()
                                         .setIsTopLevel(true)
                                         .setTopLevelMaxInstances(s.tlas_capacity)
                                         .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
                                         .setDebugName("njin rt scene"));
    if (!s.tlas)
      return false;
  }
  if (!s.inst_buf || s.inst_capacity < s.tlas_capacity) {
    s.inst_capacity = s.tlas_capacity;
    s.inst_buf = s.nv->createBuffer(nvrhi::BufferDesc()
                                        .setByteSize(s.inst_capacity * sizeof(inst_t))
                                        .setStructStride(sizeof(inst_t))
                                        .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                        .setKeepInitialState(true)
                                        .setDebugName("njin rt instances"));
    if (!s.inst_buf)
      return false;
  }
  const void *key[7] = {s.tlas.Get(), s.inst_buf.Get(), depth ? s.depth.tex.Get() : nullptr,
                        reflect ? s.refl.tex.Get() : nullptr, s.grid.tex.Get(), gi ? s.grid_depth.tex.Get() : nullptr,
                        depth ? s.mask.tex.Get() : nullptr};
  if (std::memcmp(key, s.sets_key, sizeof(key)) != 0) {
    make_sets(s, depth, reflect, gi);
    std::memcpy(s.sets_key, key, sizeof(key));
  }
  s.instances.resize(count);
  s.inst_data.assign(want, inst_t{});
  uint32_t n = 0;
  for (uint32_t i = 0; i < count; i++) {
    const auto found = s.by_as.find(inst[i].blas);
    if (found == s.by_as.end())
      continue;
    const mesh_entry &e = *found->second;
    nvrhi::rt::AffineTransform m;
    std::memcpy(&m, inst[i].m, sizeof(m));
    s.instances[n] = nvrhi::rt::InstanceDesc();
    s.instances[n]
        .setBLAS((nvrhi::rt::IAccelStruct *)(uintptr_t)inst[i].blas)
        .setInstanceID(n)
        .setInstanceMask(0xFF)
        // With the default winding, a face OpenGL draws as front (counter-
        // clockwise) is met as front from outside: the grid tells inside from
        // outside by it (tested with closed boxes).
        .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
        .setTransform(m);
    inst_t &d = s.inst_data[n];
    d.vb = e.vb->getGpuVirtualAddress();
    d.ib = e.ib ? e.ib->getGpuVirtualAddress() : 0;
    d.kind[0] = e.kind;
    d.kind[1] = inst[i].two_sided ? 1u : 0u;
    std::memcpy(d.albedo, inst[i].albedo, sizeof(d.albedo));
    std::memcpy(d.emission, inst[i].emission, sizeof(d.emission));
    n++;
  }
  s.instances.resize(n);
  params_t p{};
  std::memcpy(p.inv_view_proj, t.inv_view_proj, sizeof(p.inv_view_proj));
  p.eye[0] = t.eye[0], p.eye[1] = t.eye[1], p.eye[2] = t.eye[2];
  std::memcpy(p.sun, t.sun, sizeof(p.sun));
  p.size[0] = (float)t.width, p.size[1] = (float)t.height, p.size[2] = (float)t.rays, p.size[3] = (float)(t.frame % 4096u);
  std::memcpy(p.lamps, t.lamps, sizeof(p.lamps));
  p.lamp_info[0] = (float)t.lamp_count;
  p.lamp_info[1] = (float)t.blur;
  p.lamp_info[2] = t.sun_shadows ? 1.0f : 0.0f;
  std::memcpy(p.sun_color, t.sun_color, sizeof(p.sun_color));
  std::memcpy(p.ambient, t.ambient, sizeof(p.ambient));
  std::memcpy(p.env_sh, t.env_sh, sizeof(p.env_sh));
  std::memcpy(p.light_pos, t.light_pos, sizeof(p.light_pos));
  std::memcpy(p.light_color, t.light_color, sizeof(p.light_color));
  std::memcpy(p.light_spot, t.light_spot, sizeof(p.light_spot));
  p.light_info[0] = (float)t.light_count;
  p.light_info[1] = t.grid_lights && s.grid.tex ? 1.0f : 0.0f;
  std::memcpy(p.grid_min, t.grid_min, sizeof(p.grid_min));
  std::memcpy(p.grid_size, t.grid_size, sizeof(p.grid_size));
  std::memcpy(p.grid_count, t.grid_count, sizeof(p.grid_count));
  s.cmd->open();
  if (s.no_grid_clear) {
    s.cmd->clearTextureFloat(s.no_grid, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    s.cmd->clearTextureFloat(s.no_depth, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    s.no_grid_clear = false;
  }
  if (s.grid_clear && s.grid.tex) {
    s.cmd->clearTextureFloat(s.grid.tex, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    s.cmd->clearTextureFloat(s.grid_depth.tex, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    s.grid_clear = false;
  }
  s.cmd->buildTopLevelAccelStruct(s.tlas, s.instances.data(), s.instances.size());
  s.cmd->writeBuffer(s.inst_buf, s.inst_data.data(), s.inst_data.size() * sizeof(inst_t));
  s.cmd->writeBuffer(s.params, &p, sizeof(p));
  const uint32_t gx = (uint32_t)(t.width + 7) / 8, gy = (uint32_t)(t.height + 7) / 8;
  if (depth && t.shadows) {
    s.cmd->setComputeState(nvrhi::ComputeState().setPipeline(s.shadow_pipe).addBindingSet(s.shadow_set));
    s.cmd->dispatch(gx, gy);
    s.cmd->setComputeState(nvrhi::ComputeState().setPipeline(s.denoise_pipe).addBindingSet(s.denoise_set));
    s.cmd->dispatch(gx, gy);
  }
  // The grid first: the reflections light what they meet with it.
  if (gi) {
    s.cmd->setComputeState(nvrhi::ComputeState().setPipeline(s.gi_pipe).addBindingSet(s.gi_set));
    s.cmd->dispatch((uint32_t)s.grid_points);
  }
  if (reflect) {
    s.cmd->setComputeState(nvrhi::ComputeState().setPipeline(s.reflect_pipe).addBindingSet(s.reflect_set));
    s.cmd->dispatch(gx, gy);
  }
  s.cmd->close();
  // Every image the GL side shares this trace.
  GLuint textures[5];
  GLenum layouts[5];
  GLuint shared = 0;
  for (const shared_image *img : {depth ? &s.depth : nullptr, depth ? &s.mask : nullptr, reflect ? &s.refl : nullptr,
                                  s.grid.tex ? &s.grid : nullptr, s.grid_depth.tex ? &s.grid_depth : nullptr})
    if (img != nullptr && img->gl != 0) {
      textures[shared] = img->gl;
      layouts[shared++] = GL_LAYOUT_GENERAL_;
    }
  auto vk_queue = static_cast<nvrhi::vulkan::IDevice *>(s.nv.Get());
  if (s.gl_semaphores) {
    // GL's depth is written: Vulkan waits for it, then GL waits for the results.
    s.gl.signal(s.gl_to_vk, 0, nullptr, shared, textures, layouts);
    glFlush();
    vk_queue->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, s.to_vk, 0);
    vk_queue->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, s.to_gl, 0);
    s.nv->executeCommandList(s.cmd);
    s.gl.wait(s.gl_to_gl, 0, nullptr, shared, textures, layouts);
  } else {
    glFinish();
    s.nv->executeCommandList(s.cmd);
    s.nv->waitForIdle();
  }
  // Three traces on, the GPU is done with what was replaced before them.
  std::erase_if(s.retired, [&](const auto &r) { return s.traces - r.first > 3; });
  s.nv->runGarbageCollection();
  return true;
}

bool gl_read_buffer(unsigned buffer, size_t bytes, void *out) {
  typedef void(APIENTRY * PFN_BindBuffer)(GLenum, GLuint);
  typedef void(APIENTRY * PFN_GetBufferSubData)(GLenum, ptrdiff_t, ptrdiff_t, void *);
  static const auto bind = gl_proc<PFN_BindBuffer>("glBindBuffer");
  static const auto get = gl_proc<PFN_GetBufferSubData>("glGetBufferSubData");
  constexpr GLenum copy_read = 0x8F36; // GL_COPY_READ_BUFFER: leaves the vertex array state alone
  if (bind == nullptr || get == nullptr || buffer == 0)
    return false;
  bind(copy_read, buffer);
  get(copy_read, 0, (ptrdiff_t)bytes, out);
  bind(copy_read, 0);
  return glGetError() == GL_NO_ERROR;
}

void collect(uint32_t frames) {
  state &s = st();
  for (auto it = s.meshes.begin(); it != s.meshes.end();) {
    if (s.traces - it->second.used > frames) {
      s.by_as.erase((uint64_t)(uintptr_t)it->second.as.Get());
      s.retired.push_back({s.traces, it->second});
      it = s.meshes.erase(it);
    } else {
      ++it;
    }
  }
}
} // namespace njin::rtvk
