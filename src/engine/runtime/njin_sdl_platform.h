#pragma once
#include "njin_internal_only.h"
#include "njin_cfg.h"
#include "_math.h"
#include <memory>
struct SDL_Window;
namespace njin {
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
private:
  struct State;
  std::unique_ptr<State> state;
};
}
