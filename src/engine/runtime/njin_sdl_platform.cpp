#include "njin_sdl_platform.h"
#if NJIN_BGFX
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include "njin_input_impl.h"
#include "njin_view.h"
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>
#include <vector>
namespace njin {
namespace { [[noreturn]] void fail(const char *op) { throw std::runtime_error(std::string("njin SDL platform: ")+op+": "+SDL_GetError()); } }
struct SdlPlatform::State {
  SDL_Window *window=nullptr;
  SDL_MetalView metal=nullptr;
  bool initialized=false,was_resized=false,synchronized=false,gamepads_changed=true;
  float wheel=0;
  vec2 raw_mouse{};
  bool mouse_initialized=false;
  int backspaces=0;
  std::vector<int> text;
  std::vector<touch_raw> touch_events;
  std::map<std::pair<SDL_TouchID,SDL_FingerID>,i64> fingers;
  i64 next_finger=1;
  std::array<SDL_Gamepad*,gamepad_max> gamepads{};
  std::function<void(const SDL_Event &)> event_hook;
  std::function<void(input_store &)> input_hook;
  std::vector<std::pair<int,std::function<void(const SDL_Event &)>>> listeners;
  int next_listener=1;
  ~State() {
    for(auto *pad:gamepads) if(pad) SDL_CloseGamepad(pad);
    if(metal) SDL_Metal_DestroyView(metal);
    if(window) SDL_DestroyWindow(window);
    if(initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD);
  }
};
SdlPlatform::SdlPlatform(const config &cfg):state(std::make_unique<State>()) {
  if(!std::isfinite(cfg.width) || !std::isfinite(cfg.height) || cfg.width<1 || cfg.height<1 || cfg.width>16384 || cfg.height>16384)
    throw std::invalid_argument("njin SDL platform: invalid dimensions");
  if(cfg.transparent) throw std::invalid_argument("njin SDL platform: transparent windows are not supported");
  if(!SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) fail("initialize video");
  state->initialized=true;
  SDL_WindowFlags flags=SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if(cfg.resizable) flags|=SDL_WINDOW_RESIZABLE;
  if(!cfg.decorated) flags|=SDL_WINDOW_BORDERLESS;
#ifdef __APPLE__
  flags|=SDL_WINDOW_METAL;
#endif
  struct WindowProperties {
    SDL_PropertiesID id=SDL_CreateProperties();
    ~WindowProperties() { if(id) SDL_DestroyProperties(id); }
  } properties;
  if(!properties.id ||
     !SDL_SetStringProperty(properties.id,SDL_PROP_WINDOW_CREATE_TITLE_STRING,cfg.title ? cfg.title : "Njin") ||
     !SDL_SetNumberProperty(properties.id,SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,(int)cfg.width) ||
     !SDL_SetNumberProperty(properties.id,SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,(int)cfg.height) ||
     !SDL_SetNumberProperty(properties.id,SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,(Sint64)flags) ||
     !SDL_SetBooleanProperty(properties.id,SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN,true))
    fail("configure externally managed graphics window");
  state->window=SDL_CreateWindowWithProperties(properties.id);
  if(!state->window) fail("create window");
#ifdef __APPLE__
  state->metal=SDL_Metal_CreateView(state->window);
  if(!state->metal) fail("create Metal view");
#endif
  state->synchronized=cfg.vsync;
  if(!SDL_StartTextInput(state->window)) fail("start text input");
}
SdlPlatform::~SdlPlatform()=default;
void SdlPlatform::set_title(const char *title) { if(title && !SDL_SetWindowTitle(state->window,title)) fail("set title"); }
void SdlPlatform::set_position(vec2 pos) { if(!fullscreen() && !SDL_SetWindowPosition(state->window,(int)pos.x,(int)pos.y)) fail("set position"); }
vec2 SdlPlatform::position() const { int x,y; if(!SDL_GetWindowPosition(state->window,&x,&y)) fail("get position"); return {(float)x,(float)y}; }
rect SdlPlatform::monitor_bounds() const {
  const auto display=SDL_GetDisplayForWindow(state->window); SDL_Rect bounds;
  if(!display || !SDL_GetDisplayBounds(display,&bounds)) fail("get monitor bounds");
  return {{(float)bounds.x,(float)bounds.y},{(float)bounds.w,(float)bounds.h}};
}
void SdlPlatform::center() { if(!fullscreen() && !SDL_SetWindowPosition(state->window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED)) fail("center window"); }
void SdlPlatform::set_fullscreen(bool on) {
  if(!SDL_SetWindowFullscreenMode(state->window,nullptr) || !SDL_SetWindowFullscreen(state->window,on)) fail("set desktop fullscreen");
}
bool SdlPlatform::fullscreen() const { return (SDL_GetWindowFlags(state->window)&SDL_WINDOW_FULLSCREEN)!=0; }
void SdlPlatform::set_vsync(bool on) { state->synchronized=on; }
bool SdlPlatform::vsync() const { return state->synchronized; }
bool SdlPlatform::resized() const { return state->was_resized; }
void SdlPlatform::show_cursor(bool visible) { if(!(visible ? SDL_ShowCursor():SDL_HideCursor())) fail("set cursor visibility"); }
void SdlPlatform::lock_cursor(bool locked) { if(!SDL_SetWindowRelativeMouseMode(state->window,locked)) fail("set relative mouse mode"); }
void *SdlPlatform::native_handle() const {
  const auto p=SDL_GetWindowProperties(state->window);
#ifdef _WIN32
  return SDL_GetPointerProperty(p,SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
#elif defined(__ANDROID__)
  return SDL_GetPointerProperty(p,SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,nullptr);
#elif defined(__APPLE__)
  return SDL_Metal_GetLayer(state->metal);
#elif defined(__linux__)
  if(wayland()) return SDL_GetPointerProperty(p,SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,nullptr);
  return reinterpret_cast<void*>(static_cast<uintptr_t>(SDL_GetNumberProperty(p,SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0)));
#else
  throw std::runtime_error("njin SDL platform: native window bridge is not implemented on this platform");
#endif
}
bool SdlPlatform::wayland() const {
  return std::strcmp(SDL_GetCurrentVideoDriver(),"wayland")==0;
}
void *SdlPlatform::native_display() const {
#ifdef __linux__
  return SDL_GetPointerProperty(SDL_GetWindowProperties(state->window),wayland() ? SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER:SDL_PROP_WINDOW_X11_DISPLAY_POINTER,nullptr);
#else
  return nullptr;
#endif
}
SDL_Window *SdlPlatform::window() const { return state->window; }
int SdlPlatform::add_event_listener(std::function<void(const SDL_Event &)> listener) {
  state->listeners.emplace_back(state->next_listener,std::move(listener));
  return state->next_listener++;
}
void SdlPlatform::remove_event_listener(int id) {
  std::erase_if(state->listeners,[id](const auto &l) { return l.first==id; });
}
void SdlPlatform::set_input_hook(std::function<void(input_store &)> hook) { state->input_hook=std::move(hook); }
void SdlPlatform::set_event_hook(std::function<void(const SDL_Event &)> hook) { state->event_hook=std::move(hook); }
bool SdlPlatform::rumble(int index,float low,float high,float seconds) {
  if(index<0 || index>=gamepad_max) return false;
  auto *pad=state->gamepads[index];
  if(!pad || !SDL_GamepadConnected(pad)) return false;
  const auto motor=[](float v) { return (Uint16)(std::clamp(v,0.0f,1.0f)*65535.0f+0.5f); };
  return SDL_RumbleGamepad(pad,motor(low),motor(high),(Uint32)(std::max(seconds,0.0f)*1000.0f+0.5f));
}
void SdlPlatform::drawable_size(unsigned &w,unsigned &h) const {
  int x=0,y=0; if(!SDL_GetWindowSizeInPixels(state->window,&x,&y)) fail("query drawable size");
  w=(unsigned)std::max(x,0); h=(unsigned)std::max(y,0);
}
void SdlPlatform::set_size(int w,int h) {
  if(!SDL_SetWindowSize(state->window,w,h)) fail("resize window");
  // X11/Wayland can acknowledge a programmatic resize asynchronously. Finish
  // it before the same frame queries drawable pixels and resets the swapchain.
  if(!SDL_SyncWindow(state->window)) fail("synchronize window resize");
}
void SdlPlatform::size(float &w,float &h) const { int x,y; SDL_GetWindowSize(state->window,&x,&y); w=(float)x; h=(float)y; }
void SdlPlatform::poll_input(input_store &input,const view_state *view) {
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
  touch_feed(input.touch,view,state->touch_events,SDL_GetTicksNS()/1000000000.0);
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
  } else input.cur.mouse_delta=state->mouse_initialized?input.cur.mouse_pos-state->raw_mouse:vec2{};
  state->raw_mouse=input.cur.mouse_pos;state->mouse_initialized=true;
  input.cur.mouse[mouse_left]=(buttons&SDL_BUTTON_LMASK)!=0;
  input.cur.mouse[mouse_right]=(buttons&SDL_BUTTON_RMASK)!=0;
  input.cur.mouse[mouse_middle]=(buttons&SDL_BUTTON_MMASK)!=0;
  input.cur.mouse[mouse_side]=(buttons&SDL_BUTTON_X1MASK)!=0;
  input.cur.mouse[mouse_extra]=(buttons&SDL_BUTTON_X2MASK)!=0;
  input.cur.mouse[mouse_forward]=(buttons&SDL_BUTTON_MASK(6))!=0;
  input.cur.mouse[mouse_back]=(buttons&SDL_BUTTON_MASK(7))!=0;
  if(state->input_hook) state->input_hook(input);
}
bool SdlPlatform::poll_close() {
  SDL_Event event;
  bool close=false;
  state->was_resized=false; state->wheel=0; state->text.clear(); state->backspaces=0; state->touch_events.clear();
  const auto window=SDL_GetWindowID(state->window);
  while(SDL_PollEvent(&event)) {
    if(state->event_hook) state->event_hook(event);
    for(auto &[id,listener]:state->listeners) listener(event);
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

}
#else
namespace njin { struct SdlPlatform::State {}; SdlPlatform::~SdlPlatform()=default; }
#endif
