#pragma once
#include "njin_internal_only.h"
#include "njin_cfg.h"
#include "_math.h"
#include <functional>
#include <memory>
struct SDL_Window;
union SDL_Event;
namespace njin {
struct context;
struct input_store;
struct view_state;
class SdlPlatform {
public:
  explicit SdlPlatform(const config &);
  ~SdlPlatform();
  SdlPlatform(const SdlPlatform &)=delete;
  SdlPlatform &operator=(const SdlPlatform &)=delete;
  bool poll_close();
  void poll_input(input_store &,const view_state *view=nullptr);
  void set_size(int,int);
  void size(float &,float &) const;
  void drawable_size(unsigned &,unsigned &) const;
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
  // Rumble on an open gamepad: motors 0..1, for `seconds`. False when `pad` is not open.
  bool rumble(int pad,float low,float high,float seconds);
  void *native_handle() const;
  void *native_display() const;
  bool wayland() const;
  SDL_Window *window() const;
  // Engine tools (Dear ImGui's SDL3 backend): sees every SDL event poll_close takes,
  // before Njin's own handling. One hook; an empty function removes it.
  void set_event_hook(std::function<void(const SDL_Event &)> hook);
  // Engine tools (the UI editor's Viewport): runs at the end of poll_input, before any
  // system sees the frame's input, and may rewrite it. An empty function removes it.
  void set_input_hook(std::function<void(input_store &)> hook);
  // Engine features (rmlui): more SDL event listeners, after the hook above. The id
  // removes it again.
  int add_event_listener(std::function<void(const SDL_Event &)> listener);
  void remove_event_listener(int id);
private:
  struct State;
  std::unique_ptr<State> state;
};
// The context's SDL window, or null on the legacy renderer (no SDL window).
SdlPlatform *sdl_platform(context &ctx);
}
