#include "njin_sdl_platform.h"
#include "njin_input_impl.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool condition,const char *message) {
  if(!condition) throw std::runtime_error(std::string(message)+": "+SDL_GetError());
}
struct VirtualPad {
  SDL_JoystickID id=0;
  SDL_Joystick *joystick=nullptr;
  ~VirtualPad() { if(joystick) SDL_CloseJoystick(joystick); if(id) SDL_DetachVirtualJoystick(id); }
};
}
int main() {
  try {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    njin::config cfg{.title="Njin SDL platform test",.width=160,.height=120,.target_fps=0};
    cfg.audio=false;
    njin::SdlPlatform device(cfg);
    njin::input_store input;
    device.poll_close(); device.poll_input(input);
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.name="Njin virtual gamepad";
    VirtualPad pad;
    pad.id=SDL_AttachVirtualJoystick(&desc);
    require(pad.id!=0 && SDL_IsGamepad(pad.id),"attach recognized gamepad");
    pad.joystick=SDL_OpenJoystick(pad.id);
    require(pad.joystick!=nullptr,"open virtual joystick");
    require(SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_LEFTX,10000),"set positive stick");
    require(SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_JOYSTICK_AXIS_MIN),"set resting trigger");
    require(SDL_SetJoystickVirtualButton(pad.joystick,SDL_GAMEPAD_BUTTON_SOUTH,true),"press face button");
    SDL_UpdateJoysticks();
    int count=0;
    SDL_Window **windows=SDL_GetWindows(&count);
    require(windows && count==1,"find platform window");
    const auto window=SDL_GetWindowID(windows[0]); SDL_free(windows);
    const auto touch=[&](SDL_EventType type,SDL_TouchID device,float x) {
      SDL_Event event{}; event.type=type; event.tfinger.windowID=window;
      event.tfinger.touchID=device; event.tfinger.fingerID=7; event.tfinger.x=x; event.tfinger.y=0.5f;
      require(SDL_PushEvent(&event),"push touch event");
    };
    touch(SDL_EVENT_FINGER_DOWN,101,0.25f);
    touch(SDL_EVENT_FINGER_DOWN,202,0.75f); // same finger id on another device
    SDL_Event text{}; text.type=SDL_EVENT_TEXT_INPUT; text.text.windowID=window; text.text.text="\xc3\xa9";
    require(SDL_PushEvent(&text),"push UTF-8 event");
    SDL_Event wheel{}; wheel.type=SDL_EVENT_MOUSE_WHEEL; wheel.wheel.windowID=window; wheel.wheel.y=2.5f;
    require(SDL_PushEvent(&wheel),"push wheel event");
    device.poll_close(); device.poll_input(input);
    require(input.text_count==1 && input.text[0]==0xe9,"decode UTF-8");
    require(input.cur.wheel==2.5f,"preserve wheel precision");
    float width,height; device.size(width,height);
    require(input.touch.points.size()==2 && input.touch.gestures.fingers==2,"track multitouch");
    require(input.touch.points[0].pub.id!=input.touch.points[1].pub.id,"keep device/finger identities distinct");
    require(input.touch.points[0].pub.pressed && input.touch.points[0].pub.pos.x==width*0.25f && input.touch.points[0].pub.pos.y==height*0.5f,"map touch coordinates to window units");
    int index=-1;
    for(int i=0;i<njin::gamepad_max;++i)
      if(input.cur.pad_ready[i] && std::abs(input.cur.pad_axis[i][njin::pad_axis_left_x]-10000/32767.0f)<0.0001f) index=i;
    require(index>=0,"gamepad hotplug and stick mapping");
    require(njin::edge_match(njin::edge::pressed,input.prev.pad[index][njin::pad_face_down],input.cur.pad[index][njin::pad_face_down]),"gamepad pressed edge");
    require(input.cur.pad_axis[index][njin::pad_axis_left_trigger]==-1 && !input.cur.pad[index][njin::pad_l2],"trigger resting range");
    require(SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_LEFTX,SDL_JOYSTICK_AXIS_MIN),"set negative stick");
    require(SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_JOYSTICK_AXIS_MAX),"set full trigger");
    require(SDL_SetJoystickVirtualButton(pad.joystick,SDL_GAMEPAD_BUTTON_SOUTH,false),"release button");
    touch(SDL_EVENT_FINGER_MOTION,101,0.2f); touch(SDL_EVENT_FINGER_MOTION,202,0.8f);
    SDL_UpdateJoysticks(); device.poll_close(); device.poll_input(input);
    require(std::abs(input.touch.gestures.pinch-1.2f)<0.001f,"reuse pinch gesture calculation");
    require(input.cur.pad_axis[index][njin::pad_axis_left_x]==-1 && input.cur.pad_axis[index][njin::pad_axis_left_trigger]==1 && input.cur.pad[index][njin::pad_l2],"negative stick and trigger normalization");
    require(njin::edge_match(njin::edge::released,input.prev.pad[index][njin::pad_face_down],input.cur.pad[index][njin::pad_face_down]),"gamepad released edge");
    require(input.text_count==0 && input.cur.wheel==0,"reset per-frame input");
    SDL_CloseJoystick(pad.joystick); pad.joystick=nullptr;
    require(SDL_DetachVirtualJoystick(pad.id),"detach virtual gamepad"); pad.id=0;
    device.poll_close(); device.poll_input(input);
    require(!input.cur.pad_ready[index],"gamepad disconnect");
    touch(SDL_EVENT_FINGER_CANCELED,101,0.2f); touch(SDL_EVENT_FINGER_CANCELED,202,0.8f);
    device.poll_close(); device.poll_input(input);
    require(input.touch.gestures.fingers==0 && !input.touch.gestures.tap && input.touch.points.size()==2 && input.touch.points[0].pub.released,"cancel touch without generating a tap");
    touch(SDL_EVENT_FINGER_DOWN,101,0.5f); device.poll_close(); device.poll_input(input);
    require(input.touch.points.size()==1 && input.touch.points[0].pub.pressed,"drop previous released contacts");
    touch(SDL_EVENT_FINGER_UP,101,0.5f); device.poll_close(); device.poll_input(input);
    require(input.touch.gestures.tap && input.touch.points[0].pub.released,"normal touch release produces tap");
    device.poll_close(); device.poll_input(input);
    require(input.touch.points.empty(),"expire released touches");
    return 0;
  } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
