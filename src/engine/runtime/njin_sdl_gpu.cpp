#include "njin_sdl_gpu.h"
#if NJIN_SDL_GPU
#include <SDL3/SDL.h>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <map>
#include "njin_input_impl.h"
namespace njin {
extern const unsigned char slice_vert[], slice_frag[];
extern const unsigned long long slice_vert_size, slice_frag_size;
}

namespace njin::rhi {
namespace {
[[noreturn]] void fail(const char *operation) {
  throw std::runtime_error(std::string("njin SDL GPU: ") + operation + ": " + SDL_GetError());
}
}
struct SdlGpuDevice::State {
  struct Draw { std::vector<RenderVertex> vertices; std::vector<unsigned char> pixels; unsigned w,h; std::uint64_t texture=0; std::uint64_t mesh=0; std::array<float,16> transform; std::array<float,4> tint{1,1,1,1}; bool linear=false; std::vector<std::uint16_t> indices{}; unsigned vertex_count=0,index_count=0; bool depth=true; size_t pass=0; };
  std::vector<Draw> draws;
  struct Pass { std::uint64_t target=0; rgba clear{}; bool clear_enabled=true; };
  std::vector<Pass> passes;
  struct Target { RenderTextureHandle color; SDL_GPUTexture *depth; bool rendered=false; };
  std::unordered_map<std::uint64_t,Target> targets;
  std::uint64_t active_target=0;
  std::unordered_map<std::uint64_t,SDL_GPUBuffer*> meshes;
  std::unordered_map<std::uint64_t,SDL_GPUBuffer*> index_buffers;
  std::unordered_map<std::uint64_t,SDL_GPUTexture*> textures;
  std::uint64_t texture_upload_count=0;
  std::uint64_t mesh_upload_count=0;
  SDL_GPUGraphicsPipeline *pipeline = nullptr;
  SDL_GPUGraphicsPipeline *pipeline_2d = nullptr;
  SDL_GPUSampler *sampler = nullptr;
  SDL_GPUSampler *linear_sampler = nullptr;
  SDL_GPUTexture *depth = nullptr;
  unsigned depth_w = 0, depth_h = 0;
  SDL_Window *window = nullptr;
  SDL_GPUDevice *device = nullptr;
  bool initialized = false;
  bool claimed = false;
  rgba clear{};
  bool was_resized = false;
  bool synchronized = true;
  float wheel=0;
  std::vector<int> text;
  int backspaces=0;
  std::vector<touch_raw> touch_events;
  std::map<std::pair<SDL_TouchID,SDL_FingerID>,i64> fingers;
  i64 next_finger=1;
  bool gamepads_changed=true;
  std::array<SDL_Gamepad*,gamepad_max> gamepads{};
  ~State() {
    if (device) {
      SDL_WaitForGPUIdle(device);
      for(auto &[id,buffer]:meshes) SDL_ReleaseGPUBuffer(device,buffer);
      for(auto &[id,buffer]:index_buffers) SDL_ReleaseGPUBuffer(device,buffer);
      for(auto &[id,texture]:textures) SDL_ReleaseGPUTexture(device,texture);
      for(auto &[id,target]:targets) SDL_ReleaseGPUTexture(device,target.depth);
      if (depth) SDL_ReleaseGPUTexture(device, depth);
      if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
      if(pipeline_2d) SDL_ReleaseGPUGraphicsPipeline(device,pipeline_2d);
      if (sampler) SDL_ReleaseGPUSampler(device, sampler);
      if(linear_sampler) SDL_ReleaseGPUSampler(device,linear_sampler);
      if (claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
      SDL_DestroyGPUDevice(device);
    }
    for(auto *pad:gamepads) if(pad) SDL_CloseGamepad(pad);
    if (window) SDL_DestroyWindow(window);
    if (initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD);
  }
};
SdlGpuDevice::SdlGpuDevice(const config &cfg, bool validation)
    : state(std::make_unique<State>()) {
  if(!std::isfinite(cfg.width) || !std::isfinite(cfg.height) || cfg.width<1 || cfg.height<1 || cfg.width>16384 || cfg.height>16384)
    throw std::invalid_argument("njin SDL GPU: invalid window dimensions");
  if(cfg.transparent) throw std::invalid_argument("njin SDL GPU: transparent windows are not supported");
  const int width=(int)cfg.width,height=(int)cfg.height;
  const char *title=cfg.title ? cfg.title : "Njin";
  if (width <= 0 || height <= 0) throw std::invalid_argument("njin SDL GPU: invalid window dimensions");
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) fail("initialize video");
  state->initialized = true;
  state->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, validation, "vulkan");
  if (!state->device) fail("create Vulkan GPU device");
  SDL_Log("Njin GPU driver: %s",SDL_GetGPUDeviceDriver(state->device));
  const auto properties=SDL_GetGPUDeviceProperties(state->device);
  SDL_Log("Njin GPU device: %s; driver: %s",
    SDL_GetStringProperty(properties,SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"),
    SDL_GetStringProperty(properties,SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,"unknown"));
  SDL_WindowFlags flags=SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if(cfg.resizable) flags|=SDL_WINDOW_RESIZABLE;
  if(!cfg.decorated) flags|=SDL_WINDOW_BORDERLESS;
  state->window = SDL_CreateWindow(title, width, height, flags);
  if (!state->window) fail("create window");
  if (!SDL_ClaimWindowForGPUDevice(state->device, state->window)) fail("claim window");
  state->claimed = true;
  set_vsync(cfg.vsync);
  if(!SDL_StartTextInput(state->window)) fail("start text input");
  SDL_GPUShaderCreateInfo shader{};
  shader.format = SDL_GPU_SHADERFORMAT_SPIRV; shader.entrypoint = "main";
  shader.code = slice_vert; shader.code_size = slice_vert_size; shader.stage = SDL_GPU_SHADERSTAGE_VERTEX; shader.num_uniform_buffers=1;
  auto *vs = SDL_CreateGPUShader(state->device, &shader);
  if (!vs) fail("vertex shader");
  shader.code = slice_frag; shader.code_size = slice_frag_size; shader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT; shader.num_samplers = 1; shader.num_uniform_buffers=1;
  auto *fs = SDL_CreateGPUShader(state->device, &shader);
  if (!fs) { SDL_ReleaseGPUShader(state->device, vs); fail("fragment shader"); }
  SDL_GPUVertexBufferDescription buffer{0,sizeof(RenderVertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
  SDL_GPUVertexAttribute attributes[]{{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,12},{2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,20}};
  SDL_GPUColorTargetDescription color{};
  color.format = SDL_GetGPUSwapchainTextureFormat(state->device,state->window);
  color.blend_state.enable_blend = true;
  color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  pipe.vertex_shader=vs; pipe.fragment_shader=fs;
  pipe.vertex_input_state={&buffer,1,attributes,3};
  pipe.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pipe.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
  pipe.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
  pipe.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
  pipe.depth_stencil_state.enable_depth_test=true; pipe.depth_stencil_state.enable_depth_write=true;
  pipe.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  pipe.target_info.color_target_descriptions=&color;
  pipe.target_info.num_color_targets=1;
  pipe.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  pipe.target_info.has_depth_stencil_target=true;
  state->pipeline=SDL_CreateGPUGraphicsPipeline(state->device,&pipe);
  pipe.depth_stencil_state.enable_depth_test=false; pipe.depth_stencil_state.enable_depth_write=false;
  state->pipeline_2d=SDL_CreateGPUGraphicsPipeline(state->device,&pipe);
  SDL_ReleaseGPUShader(state->device,vs); SDL_ReleaseGPUShader(state->device,fs);
  if (!state->pipeline || !state->pipeline_2d) fail("create textured mesh pipeline");
  SDL_GPUSamplerCreateInfo sampler{};
  sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;
  sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  state->sampler=SDL_CreateGPUSampler(state->device,&sampler);
  if (!state->sampler) fail("create sampler");
  sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;
  state->linear_sampler=SDL_CreateGPUSampler(state->device,&sampler);
  if(!state->linear_sampler) fail("create linear sampler");
}
void SdlGpuDevice::draw(const MeshDraw &draw) {
  require_frame();
  if(const auto target=state->targets.find(draw.texture.id);target!=state->targets.end() && !target->second.rendered) {
    bool producer=false;
    for(const auto &pass:state->passes) if(pass.target==target->first) producer=true;
    if(!producer) throw std::logic_error("njin renderer: target has not been submitted; a minimized frame may have skipped it");
  }
  State::Draw copy{};
  copy.w=draw.image.width; copy.h=draw.image.height;
  copy.texture=draw.texture.id; copy.mesh=draw.mesh.id;
  copy.transform=draw.transform; copy.tint=draw.tint; copy.linear=draw.linear_sampling; copy.depth=draw.depth_test;
  copy.pass=state->passes.size()-1;
  copy.vertex_count=(unsigned)draw.vertices.size(); copy.index_count=(unsigned)draw.indices.size();
  if(!draw.mesh || !state->meshes.contains(draw.mesh.id))
    copy.vertices.assign(draw.vertices.begin(),draw.vertices.end());
  if(!draw.mesh || !state->index_buffers.contains(draw.mesh.id))
    copy.indices.assign(draw.indices.begin(),draw.indices.end());
  if(!draw.texture || !state->textures.contains(draw.texture.id))
    copy.pixels.assign(draw.image.rgba.begin(),draw.image.rgba.end());
  // Merge only consecutive compatible immediate draws. Never reorder alpha
  // geometry or merge persistent meshes whose buffers must remain immutable.
  if(!copy.mesh && copy.indices.empty() && copy.texture && !state->draws.empty()) {
    auto &last=state->draws.back();
    if(last.pass==copy.pass && !last.mesh && last.indices.empty() && last.texture==copy.texture && last.transform==copy.transform && last.tint==copy.tint && last.linear==copy.linear && last.depth==copy.depth &&
       last.vertices.size()+copy.vertices.size()<=10000000) {
      last.vertices.insert(last.vertices.end(),copy.vertices.begin(),copy.vertices.end());
      last.vertex_count+=copy.vertex_count;
      return;
    }
  }
  state->draws.push_back(std::move(copy));
}
void SdlGpuDevice::destroy_mesh(RenderMeshHandle handle) {
  for(const auto &draw:state->draws) if(draw.mesh==handle.id)
    throw std::logic_error("njin renderer: mesh is referenced by the current frame");
  const auto indices=state->index_buffers.find(handle.id);
  if(indices!=state->index_buffers.end()) { SDL_ReleaseGPUBuffer(state->device,indices->second); state->index_buffers.erase(indices); }
  const auto it=state->meshes.find(handle.id);
  if(it!=state->meshes.end()) { SDL_ReleaseGPUBuffer(state->device,it->second); state->meshes.erase(it); }
}
std::uint64_t SdlGpuDevice::texture_uploads() const { return state->texture_upload_count; }
std::uint64_t SdlGpuDevice::mesh_uploads() const { return state->mesh_upload_count; }
void SdlGpuDevice::destroy_texture(RenderTextureHandle handle) {
  for(const auto &draw:state->draws) if(draw.texture==handle.id)
    throw std::logic_error("njin renderer: cannot destroy a texture referenced by the current frame");
  const auto it=state->textures.find(handle.id);
  if(it!=state->textures.end()) { SDL_ReleaseGPUTexture(state->device,it->second); state->textures.erase(it); }
}
SdlGpuDevice::~SdlGpuDevice() = default;
void SdlGpuDevice::create_target(RenderTargetHandle handle,RenderTextureHandle color,unsigned width,unsigned height) {
  SDL_GPUTextureCreateInfo info{};
  info.type=SDL_GPU_TEXTURETYPE_2D; info.format=SDL_GetGPUSwapchainTextureFormat(state->device,state->window);
  info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width=width; info.height=height; info.layer_count_or_depth=1; info.num_levels=1;
  auto *texture=SDL_CreateGPUTexture(state->device,&info);
  if(!texture) fail("allocate render target color");
  info.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT; info.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  auto *depth=SDL_CreateGPUTexture(state->device,&info);
  if(!depth) { SDL_ReleaseGPUTexture(state->device,texture); fail("allocate render target depth"); }
  try {
    state->textures.emplace(color.id,texture);
    state->targets.emplace(handle.id,State::Target{color,depth});
  } catch(...) {
    state->textures.erase(color.id);
    SDL_ReleaseGPUTexture(state->device,texture); SDL_ReleaseGPUTexture(state->device,depth); throw;
  }
}
void SdlGpuDevice::destroy_target(RenderTargetHandle handle,RenderTextureHandle color) {
  for(const auto &pass:state->passes) if(pass.target==handle.id)
    throw std::logic_error("njin renderer: target is referenced by the current frame");
  destroy_texture(color);
  SDL_ReleaseGPUTexture(state->device,state->targets.at(handle.id).depth);
  state->targets.erase(handle.id);
}
void SdlGpuDevice::begin_target(RenderTargetHandle handle,rgba clear) {
  require_frame();
  if(state->active_target) throw std::logic_error("njin Vulkan: nested render targets are unsupported");
  state->passes.push_back({handle.id,clear,true}); state->active_target=handle.id;
}
void SdlGpuDevice::end_target() {
  require_frame();
  if(!state->active_target) throw std::logic_error("njin Vulkan: no active render target");
  state->passes.push_back({0,{},false}); state->active_target=0;
}
void SdlGpuDevice::begin_frame(rgba clear) {
  if(frame_active) throw std::logic_error("njin Vulkan: nested frame");
  state->clear=clear; frame_active=true;
  state->passes.clear(); state->passes.push_back({0,clear,true});
}
void SdlGpuDevice::end_frame(RenderImageAsset *capture) {
  if(state->active_target) throw std::logic_error("njin Vulkan: render target must end before the frame");
  require_frame(); frame_active=false;
  const auto c=state->clear; present_clear(c.r,c.g,c.b,c.a,capture);
}
void SdlGpuDevice::set_title(const char *title) { if(title && !SDL_SetWindowTitle(state->window,title)) fail("set title"); }
void SdlGpuDevice::set_position(vec2 pos) { if(!fullscreen() && !SDL_SetWindowPosition(state->window,(int)pos.x,(int)pos.y)) fail("set position"); }
vec2 SdlGpuDevice::position() const { int x,y; if(!SDL_GetWindowPosition(state->window,&x,&y)) fail("get position"); return {(float)x,(float)y}; }
rect SdlGpuDevice::monitor_bounds() const {
  const auto display=SDL_GetDisplayForWindow(state->window); SDL_Rect bounds;
  if(!display || !SDL_GetDisplayBounds(display,&bounds)) fail("get monitor bounds");
  return {{(float)bounds.x,(float)bounds.y},{(float)bounds.w,(float)bounds.h}};
}
void SdlGpuDevice::center() { if(!fullscreen() && !SDL_SetWindowPosition(state->window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED)) fail("center window"); }
void SdlGpuDevice::set_fullscreen(bool on) {
  if(!SDL_SetWindowFullscreenMode(state->window,nullptr) || !SDL_SetWindowFullscreen(state->window,on)) fail("set desktop fullscreen");
}
bool SdlGpuDevice::fullscreen() const { return (SDL_GetWindowFlags(state->window)&SDL_WINDOW_FULLSCREEN)!=0; }
void SdlGpuDevice::set_vsync(bool on) {
  const auto mode=on ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
  if(!SDL_WindowSupportsGPUPresentMode(state->device,state->window,mode)) throw std::runtime_error("njin SDL GPU: requested present mode is unsupported");
  if(!SDL_SetGPUSwapchainParameters(state->device,state->window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,mode)) fail("set present mode");
  state->synchronized=on;
}
bool SdlGpuDevice::vsync() const { return state->synchronized; }
bool SdlGpuDevice::resized() const { return state->was_resized; }
void SdlGpuDevice::show_cursor(bool visible) { if(!(visible ? SDL_ShowCursor():SDL_HideCursor())) fail("set cursor visibility"); }
void SdlGpuDevice::lock_cursor(bool locked) { if(!SDL_SetWindowRelativeMouseMode(state->window,locked)) fail("set relative mouse mode"); }
void *SdlGpuDevice::native_handle() const {
#ifdef _WIN32
  return SDL_GetPointerProperty(SDL_GetWindowProperties(state->window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
#else
  throw std::runtime_error("njin SDL GPU: native window handle adapter is currently Windows-only");
#endif
}
void SdlGpuDevice::set_size(int w,int h) { if (!SDL_SetWindowSize(state->window,w,h)) fail("resize window"); }
void SdlGpuDevice::size(float &w,float &h) const { int x,y; SDL_GetWindowSize(state->window,&x,&y); w=(float)x; h=(float)y; }
void SdlGpuDevice::poll_input(input_store &input) {
  input.prev=input.cur;
  input.cur={};
  std::fill(std::begin(input.key_hidden),std::end(input.key_hidden),false);
  std::fill(std::begin(input.mouse_hidden),std::end(input.mouse_hidden),false);
  input.text_count=(int)state->text.size(); input.backspaces=state->backspaces;
  std::copy(state->text.begin(),state->text.end(),input.text);
  input.cur.wheel=state->wheel;
  if(state->gamepads_changed) {
    for(auto &pad:state->gamepads) if(pad && !SDL_GamepadConnected(pad)) { SDL_CloseGamepad(pad); pad=nullptr; }
    int count=0;
    auto *ids=SDL_GetGamepads(&count);
    for(int n=0;ids && n<count;++n) {
      bool known=false;
      for(auto *pad:state->gamepads) if(pad && SDL_GetGamepadID(pad)==ids[n]) known=true;
      if(known) continue;
      for(auto &pad:state->gamepads) if(!pad) {
        pad=SDL_OpenGamepad(ids[n]);
        if(!pad) SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,"Njin cannot open gamepad: %s",SDL_GetError());
        break;
      }
    }
    SDL_free(ids); state->gamepads_changed=false;
  }
  const std::pair<gamepad_button,SDL_GamepadButton> pad_buttons[]{
    {pad_dpad_up,SDL_GAMEPAD_BUTTON_DPAD_UP},{pad_dpad_right,SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {pad_dpad_down,SDL_GAMEPAD_BUTTON_DPAD_DOWN},{pad_dpad_left,SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {pad_face_up,SDL_GAMEPAD_BUTTON_NORTH},{pad_face_right,SDL_GAMEPAD_BUTTON_EAST},
    {pad_face_down,SDL_GAMEPAD_BUTTON_SOUTH},{pad_face_left,SDL_GAMEPAD_BUTTON_WEST},
    {pad_l1,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},{pad_r1,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {pad_select,SDL_GAMEPAD_BUTTON_BACK},{pad_guide,SDL_GAMEPAD_BUTTON_GUIDE},{pad_start,SDL_GAMEPAD_BUTTON_START},
    {pad_left_thumb,SDL_GAMEPAD_BUTTON_LEFT_STICK},{pad_right_thumb,SDL_GAMEPAD_BUTTON_RIGHT_STICK}};
  const SDL_GamepadAxis axes[]{SDL_GAMEPAD_AXIS_LEFTX,SDL_GAMEPAD_AXIS_LEFTY,SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
  for(int i=0;i<gamepad_max;++i) {
    auto *pad=state->gamepads[i]; input.cur.pad_ready[i]=pad && SDL_GamepadConnected(pad);
    if(!input.cur.pad_ready[i]) continue;
    for(auto [button,native]:pad_buttons) input.cur.pad[i][button]=SDL_GetGamepadButton(pad,native);
    for(int axis=0;axis<gamepad_axis_count;++axis) {
      const int value=SDL_GetGamepadAxis(pad,axes[axis]);
      input.cur.pad_axis[i][axis]=axis>=pad_axis_left_trigger ? (value/32767.0f)*2-1 : value/(value<0 ? 32768.0f:32767.0f);
    }
    input.cur.pad[i][pad_l2]=input.cur.pad_axis[i][pad_axis_left_trigger]>0.1f;
    input.cur.pad[i][pad_r2]=input.cur.pad_axis[i][pad_axis_right_trigger]>0.1f;
  }
  touch_feed(input.touch,nullptr,state->touch_events,SDL_GetTicksNS()/1000000000.0);
  const bool *keys=SDL_GetKeyboardState(nullptr);
  input.cur.keys[key_a] = keys[SDL_SCANCODE_A];
  input.cur.keys[key_b] = keys[SDL_SCANCODE_B];
  input.cur.keys[key_c] = keys[SDL_SCANCODE_C];
  input.cur.keys[key_d] = keys[SDL_SCANCODE_D];
  input.cur.keys[key_e] = keys[SDL_SCANCODE_E];
  input.cur.keys[key_f] = keys[SDL_SCANCODE_F];
  input.cur.keys[key_g] = keys[SDL_SCANCODE_G];
  input.cur.keys[key_h] = keys[SDL_SCANCODE_H];
  input.cur.keys[key_i] = keys[SDL_SCANCODE_I];
  input.cur.keys[key_j] = keys[SDL_SCANCODE_J];
  input.cur.keys[key_k] = keys[SDL_SCANCODE_K];
  input.cur.keys[key_l] = keys[SDL_SCANCODE_L];
  input.cur.keys[key_m] = keys[SDL_SCANCODE_M];
  input.cur.keys[key_n] = keys[SDL_SCANCODE_N];
  input.cur.keys[key_o] = keys[SDL_SCANCODE_O];
  input.cur.keys[key_p] = keys[SDL_SCANCODE_P];
  input.cur.keys[key_q] = keys[SDL_SCANCODE_Q];
  input.cur.keys[key_r] = keys[SDL_SCANCODE_R];
  input.cur.keys[key_s] = keys[SDL_SCANCODE_S];
  input.cur.keys[key_t] = keys[SDL_SCANCODE_T];
  input.cur.keys[key_u] = keys[SDL_SCANCODE_U];
  input.cur.keys[key_v] = keys[SDL_SCANCODE_V];
  input.cur.keys[key_w] = keys[SDL_SCANCODE_W];
  input.cur.keys[key_x] = keys[SDL_SCANCODE_X];
  input.cur.keys[key_y] = keys[SDL_SCANCODE_Y];
  input.cur.keys[key_z] = keys[SDL_SCANCODE_Z];
  input.cur.keys[key_0] = keys[SDL_SCANCODE_0];
  input.cur.keys[key_1] = keys[SDL_SCANCODE_1];
  input.cur.keys[key_2] = keys[SDL_SCANCODE_2];
  input.cur.keys[key_3] = keys[SDL_SCANCODE_3];
  input.cur.keys[key_4] = keys[SDL_SCANCODE_4];
  input.cur.keys[key_5] = keys[SDL_SCANCODE_5];
  input.cur.keys[key_6] = keys[SDL_SCANCODE_6];
  input.cur.keys[key_7] = keys[SDL_SCANCODE_7];
  input.cur.keys[key_8] = keys[SDL_SCANCODE_8];
  input.cur.keys[key_9] = keys[SDL_SCANCODE_9];
  input.cur.keys[key_f1] = keys[SDL_SCANCODE_F1];
  input.cur.keys[key_f2] = keys[SDL_SCANCODE_F2];
  input.cur.keys[key_f3] = keys[SDL_SCANCODE_F3];
  input.cur.keys[key_f4] = keys[SDL_SCANCODE_F4];
  input.cur.keys[key_f5] = keys[SDL_SCANCODE_F5];
  input.cur.keys[key_f6] = keys[SDL_SCANCODE_F6];
  input.cur.keys[key_f7] = keys[SDL_SCANCODE_F7];
  input.cur.keys[key_f8] = keys[SDL_SCANCODE_F8];
  input.cur.keys[key_f9] = keys[SDL_SCANCODE_F9];
  input.cur.keys[key_f10] = keys[SDL_SCANCODE_F10];
  input.cur.keys[key_f11] = keys[SDL_SCANCODE_F11];
  input.cur.keys[key_f12] = keys[SDL_SCANCODE_F12];
  input.cur.keys[key_up] = keys[SDL_SCANCODE_UP];
  input.cur.keys[key_down] = keys[SDL_SCANCODE_DOWN];
  input.cur.keys[key_left] = keys[SDL_SCANCODE_LEFT];
  input.cur.keys[key_right] = keys[SDL_SCANCODE_RIGHT];
  input.cur.keys[key_home] = keys[SDL_SCANCODE_HOME];
  input.cur.keys[key_end] = keys[SDL_SCANCODE_END];
  input.cur.keys[key_page_up] = keys[SDL_SCANCODE_PAGEUP];
  input.cur.keys[key_page_down] = keys[SDL_SCANCODE_PAGEDOWN];
  input.cur.keys[key_insert] = keys[SDL_SCANCODE_INSERT];
  input.cur.keys[key_delete] = keys[SDL_SCANCODE_DELETE];
  input.cur.keys[key_space] = keys[SDL_SCANCODE_SPACE];
  input.cur.keys[key_enter] = keys[SDL_SCANCODE_RETURN];
  input.cur.keys[key_tab] = keys[SDL_SCANCODE_TAB];
  input.cur.keys[key_escape] = keys[SDL_SCANCODE_ESCAPE];
  input.cur.keys[key_backspace] = keys[SDL_SCANCODE_BACKSPACE];
  input.cur.keys[key_caps_lock] = keys[SDL_SCANCODE_CAPSLOCK];
  input.cur.keys[key_scroll_lock] = keys[SDL_SCANCODE_SCROLLLOCK];
  input.cur.keys[key_num_lock] = keys[SDL_SCANCODE_NUMLOCKCLEAR];
  input.cur.keys[key_print_screen] = keys[SDL_SCANCODE_PRINTSCREEN];
  input.cur.keys[key_pause] = keys[SDL_SCANCODE_PAUSE];
  input.cur.keys[key_apostrophe] = keys[SDL_SCANCODE_APOSTROPHE];
  input.cur.keys[key_comma] = keys[SDL_SCANCODE_COMMA];
  input.cur.keys[key_minus] = keys[SDL_SCANCODE_MINUS];
  input.cur.keys[key_period] = keys[SDL_SCANCODE_PERIOD];
  input.cur.keys[key_slash] = keys[SDL_SCANCODE_SLASH];
  input.cur.keys[key_semicolon] = keys[SDL_SCANCODE_SEMICOLON];
  input.cur.keys[key_equal] = keys[SDL_SCANCODE_EQUALS];
  input.cur.keys[key_left_bracket] = keys[SDL_SCANCODE_LEFTBRACKET];
  input.cur.keys[key_backslash] = keys[SDL_SCANCODE_BACKSLASH];
  input.cur.keys[key_right_bracket] = keys[SDL_SCANCODE_RIGHTBRACKET];
  input.cur.keys[key_grave] = keys[SDL_SCANCODE_GRAVE];
  input.cur.keys[key_left_shift] = keys[SDL_SCANCODE_LSHIFT];
  input.cur.keys[key_left_control] = keys[SDL_SCANCODE_LCTRL];
  input.cur.keys[key_left_alt] = keys[SDL_SCANCODE_LALT];
  input.cur.keys[key_left_super] = keys[SDL_SCANCODE_LGUI];
  input.cur.keys[key_right_shift] = keys[SDL_SCANCODE_RSHIFT];
  input.cur.keys[key_right_control] = keys[SDL_SCANCODE_RCTRL];
  input.cur.keys[key_right_alt] = keys[SDL_SCANCODE_RALT];
  input.cur.keys[key_right_super] = keys[SDL_SCANCODE_RGUI];
  input.cur.keys[key_kp_0] = keys[SDL_SCANCODE_KP_0];
  input.cur.keys[key_kp_1] = keys[SDL_SCANCODE_KP_1];
  input.cur.keys[key_kp_2] = keys[SDL_SCANCODE_KP_2];
  input.cur.keys[key_kp_3] = keys[SDL_SCANCODE_KP_3];
  input.cur.keys[key_kp_4] = keys[SDL_SCANCODE_KP_4];
  input.cur.keys[key_kp_5] = keys[SDL_SCANCODE_KP_5];
  input.cur.keys[key_kp_6] = keys[SDL_SCANCODE_KP_6];
  input.cur.keys[key_kp_7] = keys[SDL_SCANCODE_KP_7];
  input.cur.keys[key_kp_8] = keys[SDL_SCANCODE_KP_8];
  input.cur.keys[key_kp_9] = keys[SDL_SCANCODE_KP_9];
  input.cur.keys[key_kp_decimal] = keys[SDL_SCANCODE_KP_PERIOD];
  input.cur.keys[key_kp_divide] = keys[SDL_SCANCODE_KP_DIVIDE];
  input.cur.keys[key_kp_multiply] = keys[SDL_SCANCODE_KP_MULTIPLY];
  input.cur.keys[key_kp_subtract] = keys[SDL_SCANCODE_KP_MINUS];
  input.cur.keys[key_kp_add] = keys[SDL_SCANCODE_KP_PLUS];
  input.cur.keys[key_kp_enter] = keys[SDL_SCANCODE_KP_ENTER];
  input.cur.keys[key_kp_equal] = keys[SDL_SCANCODE_KP_EQUALS];
  float x,y;
  const auto buttons=SDL_GetMouseState(&x,&y);
  // SDL mouse coordinates are already in logical window units, matching
  // raylib screen/camera coordinates. Swapchain pixels may have higher density.
  input.cur.mouse_pos={x,y};
  if(SDL_GetWindowRelativeMouseMode(state->window)) {
    SDL_GetRelativeMouseState(&x,&y); input.cur.mouse_delta={x,y};
  } else input.cur.mouse_delta=input.cur.mouse_pos-input.prev.mouse_pos;
  input.cur.mouse[mouse_left]=(buttons&SDL_BUTTON_LMASK)!=0;
  input.cur.mouse[mouse_right]=(buttons&SDL_BUTTON_RMASK)!=0;
  input.cur.mouse[mouse_middle]=(buttons&SDL_BUTTON_MMASK)!=0;
  input.cur.mouse[mouse_side]=(buttons&SDL_BUTTON_X1MASK)!=0;
  input.cur.mouse[mouse_extra]=(buttons&SDL_BUTTON_X2MASK)!=0;
  input.cur.mouse[mouse_forward]=(buttons&SDL_BUTTON_MASK(6))!=0;
  input.cur.mouse[mouse_back]=(buttons&SDL_BUTTON_MASK(7))!=0;
}
bool SdlGpuDevice::poll_close() {
  SDL_Event event;
  bool close=false;
  state->was_resized=false; state->wheel=0; state->text.clear(); state->backspaces=0; state->touch_events.clear();
  const auto window=SDL_GetWindowID(state->window);
  while(SDL_PollEvent(&event)) {
    if((event.type==SDL_EVENT_FINGER_DOWN || event.type==SDL_EVENT_FINGER_MOTION || event.type==SDL_EVENT_FINGER_UP || event.type==SDL_EVENT_FINGER_CANCELED) && event.tfinger.windowID==window) {
      const auto key=std::pair{event.tfinger.touchID,event.tfinger.fingerID};
      const bool end=event.type==SDL_EVENT_FINGER_UP || event.type==SDL_EVENT_FINGER_CANCELED;
      auto it=state->fingers.find(key);
      if(it==state->fingers.end() && !end && state->fingers.size()<touch_max)
        it=state->fingers.emplace(key,state->next_finger++).first;
      if(it!=state->fingers.end()) {
        int w=0,h=0; SDL_GetWindowSize(state->window,&w,&h);
        const auto kind=event.type==SDL_EVENT_FINGER_DOWN ? touch_raw::down : event.type==SDL_EVENT_FINGER_UP ? touch_raw::up : event.type==SDL_EVENT_FINGER_CANCELED ? touch_raw::cancel : touch_raw::move;
        state->touch_events.push_back({it->second,event.tfinger.x*w,event.tfinger.y*h,kind});
        if(end) state->fingers.erase(it);
      }
    }
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID==window) {
      for(auto &[key,id]:state->fingers) state->touch_events.push_back({id,0,0,touch_raw::cancel});
      state->fingers.clear();
    }
    if(event.type==SDL_EVENT_GAMEPAD_ADDED || event.type==SDL_EVENT_GAMEPAD_REMOVED) state->gamepads_changed=true;
    if(event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID==window)) close=true;
    if((event.type==SDL_EVENT_WINDOW_RESIZED || event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) && event.window.windowID==window) state->was_resized=true;
    if(event.type==SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID==window)
      state->wheel+=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y:event.wheel.y;
    if(event.type==SDL_EVENT_KEY_DOWN && event.key.windowID==window && event.key.key==SDLK_BACKSPACE) ++state->backspaces;
    if(event.type==SDL_EVENT_TEXT_INPUT && event.text.windowID==window) {
      const char *text=event.text.text; size_t length=std::strlen(text);
      while(length && state->text.size()<input_text_max) state->text.push_back((int)SDL_StepUTF8(&text,&length));
    }
  }
  return close;
}
void SdlGpuDevice::present_clear(float r, float g, float b, float a, RenderImageAsset *capture) {
  struct CaptureResources {
    SDL_GPUDevice *device;
    SDL_GPUTexture *target=nullptr;
    SDL_GPUTransferBuffer *download=nullptr;
    SDL_GPUFence *fence=nullptr;
    ~CaptureResources() {
      if(fence) SDL_ReleaseGPUFence(device,fence);
      if(download) SDL_ReleaseGPUTransferBuffer(device,download);
      if(target) SDL_ReleaseGPUTexture(device,target);
    }
  } readback{state->device};
  if(capture) *capture={};
  auto *commands = SDL_AcquireGPUCommandBuffer(state->device);
  if (!commands) fail("acquire command buffer");
  SDL_GPUTexture *swapchain = nullptr;
  unsigned width=0,height=0;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, state->window, &swapchain, &width, &height)) {
    SDL_CancelGPUCommandBuffer(commands);
    fail("acquire swapchain");
  }
  struct PendingCommands {
    SDL_GPUCommandBuffer *commands;
    ~PendingCommands() { if(commands) SDL_SubmitGPUCommandBuffer(commands); }
  } submission{commands};
  // Minimized windows can return no image. Submit the command buffer anyway.
  if (swapchain) {
    if(capture) {
      if(width>16384 || height>16384) throw std::runtime_error("njin capture: drawable exceeds readback size limit");
      SDL_GPUTextureCreateInfo info{};
      info.type=SDL_GPU_TEXTURETYPE_2D;
      info.format=SDL_GetGPUSwapchainTextureFormat(state->device,state->window);
      if(info.format!=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM && info.format!=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) {
        throw std::runtime_error("njin capture: unsupported swapchain pixel format");
      }
      info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
      info.width=width; info.height=height; info.layer_count_or_depth=1; info.num_levels=1;
      readback.target=SDL_CreateGPUTexture(state->device,&info);
      SDL_GPUTransferBufferCreateInfo transfer{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,((width+63u)&~63u)*height*4,0};
      readback.download=SDL_CreateGPUTransferBuffer(state->device,&transfer);
      if(!readback.target || !readback.download) { fail("capture allocation"); }
    }
    if (!state->depth || width!=state->depth_w || height!=state->depth_h) {
      if (state->depth) SDL_ReleaseGPUTexture(state->device,state->depth);
      SDL_GPUTextureCreateInfo info{};
      info.type=SDL_GPU_TEXTURETYPE_2D; info.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
      info.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
      info.width=width; info.height=height; info.layer_count_or_depth=1; info.num_levels=1;
      state->depth=SDL_CreateGPUTexture(state->device,&info);
      state->depth_w=width; state->depth_h=height;
      if (!state->depth) { fail("depth target"); }
    }
    struct Uploaded {
      SDL_GPUBuffer *buffer, *indices;
      SDL_GPUTexture *texture;
      unsigned count;
      bool buffer_owned, indices_owned, texture_owned;
      std::array<float,16> transform;
      std::array<float,4> tint;
      bool linear,depth;
      size_t pass;
    };
    std::vector<Uploaded> uploaded;
    struct ReleaseUploads {
      SDL_GPUDevice *device;
      std::vector<Uploaded> &items;
      ~ReleaseUploads() {
        for(auto &u:items) {
          if(u.buffer_owned) SDL_ReleaseGPUBuffer(device,u.buffer);
          if(u.indices_owned && u.indices) SDL_ReleaseGPUBuffer(device,u.indices);
          if(u.texture_owned) SDL_ReleaseGPUTexture(device,u.texture);
        }
      }
    } release{state->device,uploaded};
    for(auto &draw:state->draws) {
      struct PendingUpload {
        SDL_GPUDevice *device;
        SDL_GPUBuffer *buffer=nullptr,*indices=nullptr;
        SDL_GPUTexture *texture=nullptr;
        SDL_GPUTransferBuffer *staging=nullptr;
        ~PendingUpload() {
          if(buffer) SDL_ReleaseGPUBuffer(device,buffer);
          if(indices) SDL_ReleaseGPUBuffer(device,indices);
          if(texture) SDL_ReleaseGPUTexture(device,texture);
          if(staging) SDL_ReleaseGPUTransferBuffer(device,staging);
        }
      } pending{state->device};
      const auto mesh=state->meshes.find(draw.mesh);
      const auto index=state->index_buffers.find(draw.mesh);
      const auto tex=state->textures.find(draw.texture);
      const bool reuse_mesh=draw.mesh && mesh!=state->meshes.end();
      const bool reuse_index=draw.mesh && index!=state->index_buffers.end();
      const bool reuse_texture=draw.texture && tex!=state->textures.end();
      const Uint32 vertex_size=(Uint32)(draw.vertex_count*sizeof(RenderVertex));
      const Uint32 index_size=((Uint32)draw.index_count*2+3)&~3u;
      SDL_GPUBufferCreateInfo buffer_info{SDL_GPU_BUFFERUSAGE_VERTEX,vertex_size,0};
      auto *buffer=reuse_mesh ? mesh->second : (pending.buffer=SDL_CreateGPUBuffer(state->device,&buffer_info));
      SDL_GPUBuffer *indices=nullptr;
      if(index_size) {
        buffer_info.usage=SDL_GPU_BUFFERUSAGE_INDEX; buffer_info.size=index_size;
        indices=reuse_index ? index->second : (pending.indices=SDL_CreateGPUBuffer(state->device,&buffer_info));
      }
      SDL_GPUTextureCreateInfo texture_info{};
      texture_info.type=SDL_GPU_TEXTURETYPE_2D; texture_info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      texture_info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER; texture_info.width=draw.w; texture_info.height=draw.h;
      texture_info.layer_count_or_depth=1; texture_info.num_levels=1;
      auto *texture=reuse_texture ? tex->second : (pending.texture=SDL_CreateGPUTexture(state->device,&texture_info));
      if(!buffer || !texture || (index_size && !indices)) fail("allocate mesh/texture upload");
      const Uint32 vertex_bytes=reuse_mesh ? 0:vertex_size;
      const Uint32 index_bytes=reuse_index ? 0:index_size;
      const Uint32 bytes=vertex_bytes+index_bytes+(reuse_texture ? 0:(Uint32)draw.pixels.size());
      if(bytes) {
        SDL_GPUTransferBufferCreateInfo transfer{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes,0};
        pending.staging=SDL_CreateGPUTransferBuffer(state->device,&transfer);
        if(!pending.staging) fail("allocate staging buffer");
        auto *data=(unsigned char*)SDL_MapGPUTransferBuffer(state->device,pending.staging,false);
        if(!data) fail("map upload");
        if(vertex_bytes) std::memcpy(data,draw.vertices.data(),vertex_bytes);
        if(index_bytes) {
          std::memset(data+vertex_bytes,0,index_bytes);
          std::memcpy(data+vertex_bytes,draw.indices.data(),draw.indices.size()*2);
        }
        if(!reuse_texture) std::memcpy(data+vertex_bytes+index_bytes,draw.pixels.data(),draw.pixels.size());
        SDL_UnmapGPUTransferBuffer(state->device,pending.staging);
        auto *copy=SDL_BeginGPUCopyPass(commands);
        if(vertex_bytes) {
          SDL_GPUTransferBufferLocation source{pending.staging,0}; SDL_GPUBufferRegion destination{buffer,0,vertex_size};
          SDL_UploadToGPUBuffer(copy,&source,&destination,false);
        }
        if(index_bytes) {
          SDL_GPUTransferBufferLocation source{pending.staging,vertex_bytes}; SDL_GPUBufferRegion destination{indices,0,index_size};
          SDL_UploadToGPUBuffer(copy,&source,&destination,false);
        }
        if(!reuse_texture) {
          SDL_GPUTextureTransferInfo source{pending.staging,vertex_bytes+index_bytes,draw.w,draw.h};
          SDL_GPUTextureRegion destination{texture,0,0,0,0,0,draw.w,draw.h,1};
          SDL_UploadToGPUTexture(copy,&source,&destination,false); ++state->texture_upload_count;
        }
        SDL_EndGPUCopyPass(copy);
      }
      uploaded.push_back({buffer,indices,texture,(draw.index_count ? draw.index_count:draw.vertex_count),
        !reuse_mesh,!reuse_index,!reuse_texture,draw.transform,draw.tint,draw.linear,draw.depth,draw.pass});
      pending.buffer=nullptr; pending.indices=nullptr; pending.texture=nullptr;
      auto &entry=uploaded.back();
      if(draw.mesh && !reuse_mesh) { state->meshes.emplace(draw.mesh,buffer); entry.buffer_owned=false; ++state->mesh_upload_count; }
      if(draw.mesh && indices && !reuse_index) { state->index_buffers.emplace(draw.mesh,indices); entry.indices_owned=false; }
      if(draw.texture && !reuse_texture) { state->textures.emplace(draw.texture,texture); entry.texture_owned=false; }
    }
    if(state->passes.empty()) state->passes.push_back({0,{r,g,b,a},true});
    for(size_t pass_index=0;pass_index<state->passes.size();++pass_index) {
    const auto &description=state->passes[pass_index];
    const auto *offscreen=description.target ? &state->targets.at(description.target):nullptr;
    SDL_GPUColorTargetInfo target{};
    target.texture = offscreen ? state->textures.at(offscreen->color.id) : (readback.target ? readback.target : swapchain);
    const auto clear=description.clear;
    target.clear_color = {clear.r,clear.g,clear.b,clear.a};
    target.load_op = description.clear_enabled ? SDL_GPU_LOADOP_CLEAR:SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture=offscreen ? offscreen->depth:state->depth; depth.clear_depth=1;
    depth.load_op=description.clear_enabled ? SDL_GPU_LOADOP_CLEAR:SDL_GPU_LOADOP_LOAD;
    depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE; depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto *pass = SDL_BeginGPURenderPass(commands, &target, 1, &depth);
    if (!pass) {
      const std::string error = SDL_GetError();
      throw std::runtime_error("njin SDL GPU: begin render pass: " + error);
    }
    for (auto &u:uploaded) {
      if(u.pass!=pass_index) continue;
      SDL_BindGPUGraphicsPipeline(pass,u.depth ? state->pipeline:state->pipeline_2d);
      SDL_PushGPUVertexUniformData(commands,0,u.transform.data(),64);
      SDL_PushGPUFragmentUniformData(commands,0,u.tint.data(),16);
      SDL_GPUBufferBinding binding{u.buffer,0}; SDL_BindGPUVertexBuffers(pass,0,&binding,1);
      SDL_GPUTextureSamplerBinding sampler{u.texture,u.linear ? state->linear_sampler : state->sampler}; SDL_BindGPUFragmentSamplers(pass,0,&sampler,1);
      if(u.indices) {
        SDL_GPUBufferBinding index_binding{u.indices,0};
        SDL_BindGPUIndexBuffer(pass,&index_binding,SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(pass,u.count,1,0,0,0);
      } else SDL_DrawGPUPrimitives(pass,u.count,1,0,0);
    }
    SDL_EndGPURenderPass(pass);
    if(description.target) state->targets.at(description.target).rendered=true;
    }
    if(readback.target) {
      SDL_GPUBlitInfo blit{};
      blit.source={readback.target,0,0,0,0,width,height};
      blit.destination={swapchain,0,0,0,0,width,height};
      blit.load_op=SDL_GPU_LOADOP_DONT_CARE; blit.filter=SDL_GPU_FILTER_NEAREST;
      SDL_BlitGPUTexture(commands,&blit);
      auto *copy=SDL_BeginGPUCopyPass(commands);
      SDL_GPUTextureRegion source{readback.target,0,0,0,0,0,width,height,1};
      SDL_GPUTextureTransferInfo destination{readback.download,0,(width+63u)&~63u,height};
      SDL_DownloadFromGPUTexture(copy,&source,&destination);
      SDL_EndGPUCopyPass(copy);
    }
  }
  state->draws.clear();
  state->passes.clear();
  if(readback.target) {
    submission.commands=nullptr;
    readback.fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if(!readback.fence) fail("submit capture");
    if(!SDL_WaitForGPUFences(state->device,true,&readback.fence,1)) fail("wait capture");
    const auto *data=(const unsigned char*)SDL_MapGPUTransferBuffer(state->device,readback.download,false);
    if(!data) fail("map capture");
    try {
      capture->width=width; capture->height=height; capture->pixels.resize(size_t(width)*height*4);
      for(unsigned y=0;y<height;++y) std::memcpy(capture->pixels.data()+size_t(y)*width*4,data+size_t(y)*((width+63u)&~63u)*4,size_t(width)*4);
      if(SDL_GetGPUSwapchainTextureFormat(state->device,state->window)==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)
        for(size_t i=0;i<capture->pixels.size();i+=4) std::swap(capture->pixels[i],capture->pixels[i+2]);
    } catch(...) { SDL_UnmapGPUTransferBuffer(state->device,readback.download); throw; }
    SDL_UnmapGPUTransferBuffer(state->device,readback.download);
  } else {
    submission.commands=nullptr;
    if(!SDL_SubmitGPUCommandBuffer(commands)) fail("submit frame");
  }
}
}
#else
#include <stdexcept>
namespace njin::rhi {
struct SdlGpuDevice::State {};
SdlGpuDevice::SdlGpuDevice(const config&,bool) { throw std::runtime_error("SDL GPU not built"); }
SdlGpuDevice::~SdlGpuDevice()=default;
void SdlGpuDevice::begin_frame(rgba) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::end_frame(RenderImageAsset*) { throw std::runtime_error("SDL GPU not built"); }
bool SdlGpuDevice::poll_close() { return true; }
void SdlGpuDevice::present_clear(float,float,float,float,RenderImageAsset*) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::destroy_mesh(RenderMeshHandle) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::create_target(RenderTargetHandle,RenderTextureHandle,unsigned,unsigned) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::destroy_target(RenderTargetHandle,RenderTextureHandle) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::begin_target(RenderTargetHandle,rgba) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::end_target() { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::draw(const MeshDraw &) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::destroy_texture(RenderTextureHandle) { throw std::runtime_error("SDL GPU not built"); }
std::uint64_t SdlGpuDevice::texture_uploads() const { return 0; }
std::uint64_t SdlGpuDevice::mesh_uploads() const { return 0; }
void SdlGpuDevice::poll_input(input_store &) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::set_size(int,int) { throw std::runtime_error("SDL GPU not built"); }
void SdlGpuDevice::size(float &,float &) const { throw std::runtime_error("SDL GPU not built"); }
}
#endif
