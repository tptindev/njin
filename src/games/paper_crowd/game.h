#pragma once
// Paper Crowd: a sheet of watercolour paper seen from above, and a crowd of
// tiny painted people on it, each one deciding for itself what to do next:
// stroll, run, wave, jump, dance, cartwheel, lie in the sun, walk with a
// friend, chase someone, or hold hands in a ring. A few dogs tag along.
//
//   game.cpp   the state, input (spawn, call, cheer, reset), the module
//   bake.cpp   drawing every pose once into a sprite sheet, and finding a frame in it
//   crowd.cpp  spawning, and the AI that picks and runs each activity
//   camera.cpp picking a person, following them up close, easing back out
//   draw.cpp   the paper, the figures, the hint
#include <njin.h>

#include <array>
#include <string>
#include <vector>

namespace paper_crowd {
using namespace njin;

inline constexpr f32 world_w = 1280.0f;
inline constexpr f32 world_h = 720.0f;
// The figures are a few pixels of ink: the scene renders at this multiple and
// is scaled down, so thin limbs stay smooth.
inline constexpr i32 render_scale = 2;
inline constexpr f32 margin = 34.0f; // people keep this far from the paper's edge
inline constexpr i32 start_people = 1000;
inline constexpr i32 start_pets = 9;
inline constexpr i32 max_people = 420;

// Keeps a walker on the sheet. The AI already aims inside the margin; this is
// only the hard stop for a runner or cartwheel that overshoots.
inline vec2 clamp_to_paper(vec2 p) { return clamp(p, {8.0f, 26.0f}, {world_w - 8.0f, world_h - 6.0f}); }

// What a person is doing. Each one has its own pose in draw.cpp and its own
// steering in crowd.cpp.
enum activity : u8 {
  act_idle,
  act_stroll,
  act_run,
  act_wave,
  act_jump,
  act_jacks,
  act_dance,
  act_cartwheel,
  act_handstand,
  act_lie,
  act_sit,
  act_follow, // walk beside a friend
  act_chase,  // run after someone
  act_flee,   // run away from whoever chases
  act_ring,   // hold hands in a ring
  act_come,   // walk to where the player called, then wave
  act_count
};

constexpr rgba rgb(i32 r, i32 g, i32 b) { return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}; }

// Watercolour pigments, picked off the painting: warm reds and pinks, yolk
// yellow, teal, grass green, cornflower and a few muted ones.
inline constexpr std::array<rgba, 16> cloth_colors{{
    rgb(214, 62, 64),  rgb(232, 106, 124), rgb(240, 150, 186), rgb(247, 205, 62),
    rgb(242, 140, 52), rgb(66, 176, 170),  rgb(110, 184, 88),  rgb(176, 202, 78),
    rgb(78, 116, 202), rgb(52, 64, 124),   rgb(140, 100, 180), rgb(102, 190, 226),
    rgb(120, 200, 170), rgb(196, 80, 150), rgb(90, 90, 96),    rgb(226, 90, 44),
}};
struct person {
  activity act = act_idle;
  f32 timer = 0.0f;     // seconds left in the current activity
  f32 clock = 0.0f;     // seconds since the activity started, drives its pose
  f32 walk = 0.0f;      // walk cycle, advanced by distance travelled
  vec2 target{};        // where stroll, run, come and ring go
  entt::entity other = entt::null; // friend to follow, or who to chase
  i32 ring = -1;        // index into game_state::rings
  f32 face = 1.0f;      // +1 looks right, -1 left: which way sideways poses lean
  f32 spin = 1.0f;      // cartwheel and dance direction

  // Looks, fixed at spawn: a colour (an index into cloth_colors, which the
  // person shader gets once as a uniform) and a size, small for children.
  u8 cloth = 0;
  f32 size = 1.0f;
};

struct pet {
  entt::entity owner = entt::null;
  vec2 offset{};  // where it likes to be, relative to the owner
  f32 timer = 0.0f;
  f32 walk = 0.0f;
  bool sniffing = false;
  rgba coat{};
  bool cat = false;
};

struct ring {
  vec2 center{};
  f32 angle = 0.0f;
  f32 turn = 30.0f;      // degrees per second, sign is the direction
  f32 timer = 0.0f;      // seconds left before it breaks up
  i32 capacity = 6;
  std::vector<entt::entity> members;
  bool started = false;  // enough hands joined and it began to turn
  bool alive = false;
};

struct game_state {
  shader_handle paper{};
  shader_handle person{};             // draws every person, see draw.cpp
  instance_buffer_handle instances{}; // their quads, one instance each
  render_texture_handle sheet{};      // the baked sprite sheet, see bake.cpp
  bool use_baked = true;              // B toggles: sprite sheet, or live SDF for comparison
  bool baked_now = false;             // what the last frame drew with
  bool export_requested = false;      // E: write the sheet out, at the next post_update
  std::string toast;                  // the last export's result, shown for a few seconds
  f32 toast_timer = 0.0f;
  std::vector<ring> rings;
  vec2 call_pos{};
  f32 call_flash = 0.0f; // fades the ripple drawn where the player called
  bool show_hint = true;

  // Camera: the whole sheet at zoom 1, or following `focus` up close.
  entt::entity camera = entt::null;
  entt::entity focus = entt::null; // the person being followed, or null
  f32 focus_zoom = 4.0f;           // zoom while following; the wheel changes it
};

extern game_state g;

// crowd.cpp
entt::entity spawn_person(njin_ctx &ctx, vec2 pos);
entt::entity spawn_pet(njin_ctx &ctx, vec2 pos, entt::entity owner);
void spawn_crowd(njin_ctx &ctx);
void clear_crowd(njin_ctx &ctx);
void call_people(njin_ctx &ctx, vec2 pos, f32 radius);
void cheer_all(njin_ctx &ctx);
void drive_people(njin_ctx &ctx);
void drive_pets(njin_ctx &ctx);
void drive_rings(njin_ctx &ctx);

// camera.cpp
inline constexpr f32 focus_zoom_min = 1.6f; // wheel below this lets go of the focus
inline constexpr f32 focus_zoom_max = 9.0f;
void spawn_camera(njin_ctx &ctx);
entt::entity person_at(njin_ctx &ctx, vec2 pos, f32 radius);
void focus_on(entt::entity e);
void unfocus();
void drive_camera(njin_ctx &ctx);

// draw.cpp
void draw_paper(njin_ctx &ctx);
void draw_crowd(njin_ctx &ctx);
void draw_hint(njin_ctx &ctx);

mod_desc module();
} // namespace paper_crowd
