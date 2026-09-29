#pragma once
#include "dna.h"
#include <njin.h>
#include <string>
#include <vector>

namespace crowd {
using njin::f32;
using njin::i32;
using njin::vec2;

// ---- sprite sheets (sheet.cpp) ----------------------------------------------

// Directions as numbered in the reference sample.
enum dir8 : u8 { dir_s, dir_se, dir_e, dir_ne, dir_n, dir_nw, dir_w, dir_sw };
inline constexpr u32 base_dirs = 5; // S, SE, E, NE, N; W, NW, SW are their mirrors

// Poses baked into the body sheet. bake.fs uses the same numbers.
enum pose_id : u8 {
  pose_idle,
  pose_walk,
  pose_run,
  pose_jump,
  pose_sit,
  pose_lie,
  pose_wave,   // greeting
  pose_shake,  // handshake, side view only
  pose_hold_l, // hand in hand: hand out to the left neighbour (on screen)
  pose_hold_r, // ... to the right neighbour
  pose_hold_b, // ... to both
  pose_punch,  // guard, jab (far fist), guard, cross (near fist)
  pose_kick,   // guard, knee up, near leg out, knee up
  pose_leap_punch, // crouch, take off, punch in the air, land (sim.cpp lifts it)
  pose_leap_kick,  // crouch, take off, flying kick, land
  pose_count,
};

struct pose_def {
  const char *name;
  u8 frames;
  u8 dirs; // bit per base direction: 1 S, 2 SE, 4 E, 8 NE, 16 N
};
const pose_def &pose_desc(pose_id pose);

// Shape space of the reference figure: y up, ground at -0.080. A body cell
// spans 0.144 units from y = -0.088.
inline constexpr u32 cell_w = 48, cell_h = 64;
inline constexpr f32 shape_scale = (f32)cell_h / 0.144f; // sheet pixels per unit
inline constexpr f32 shape_bottom = -0.088f;
inline constexpr f32 world_per_px = 0.5f; // a cell is 24 x 32 world units
inline constexpr f32 shape_to_world = shape_scale * world_per_px;
// Where hands meet, from bake.fs (HOLD_REACH, SHAKE_REACH): keep in step.
inline constexpr f32 hold_spacing = 2.0f * 0.026f * shape_to_world;
inline constexpr f32 shake_distance = 2.0f * 0.022f * shape_to_world;
inline constexpr f32 greet_distance = 26.0f;
// Sparring: a cross just reaches the other chest (punch reach in bake.fs).
inline constexpr f32 spar_distance = 2.0f * 0.023f * shape_to_world;

// 8 floats, read by crowd.vs as instance0 and instance1.
struct instance {
  f32 x, y;     // feet, world
  f32 cell;     // body cell + 1, negative when mirrored
  f32 flags;    // head direction, + 8 if selected
  f32 dna0, dna1;
  f32 lift;     // world units off the ground
  f32 rotation; // radians, lying down
};

// Body cell for a pose seen from a direction. A direction the pose was not
// baked for falls back to the nearest one that was.
struct cell_ref {
  f32 cell;
  u8 head_dir;
};
cell_ref cell_for(pose_id pose, u8 dir, u32 frame);

// false when the GPU cannot draw instanced (the game shows a message instead).
bool sheet_load(njin::context &ctx);
void sheet_bake(njin::context &ctx); // once, in the first frame
bool sheet_ready();
void sheet_draw(njin::context &ctx, const std::vector<instance> &instances);
bool sheet_save(njin::context &ctx, std::string &folder);

struct sheet_stats {
  u32 body_cells, body_w, body_h, head_w, head_h;
  u32 bytes; // VRAM of all three render textures, RGBA8
};
sheet_stats sheet_get_stats();

// ---- simulation (sim.cpp) --------------------------------------------------

inline constexpr vec2 world_size{4000.0f, 2600.0f};

enum activity : u8 {
  act_idle,
  act_walk,
  act_run,
  act_jump,
  act_sit,
  act_lie,
  act_gather, // walking to a slot in a group
  act_wait,   // at the slot, waiting for the rest of the group
  act_greet,
  act_shake,
  act_chain, // hand in hand
  act_spar,  // taking turns to punch and kick
  act_pinned, // gallery: one pose forever
};
const char *activity_name(u8 act);

// ---- components ------------------------------------------------------------
// A person is an entity with njin::transform (feet position), person and dna;
// member while in a group, gallery_pin for the gallery figures. A group is an
// entity with group.

struct person {
  vec2 vel{};
  f32 timer = 0.0f; // seconds left in the activity
  f32 phase = 0.0f; // animation cycle, 0..1
  f32 lie_rot = 0.0f;
  u8 dir = dir_s;
  u8 act = act_idle;
  pose_id pose = pose_idle; // gallery and sparring only
};

// In a group, at a slot (0 is the leftmost).
struct member {
  entt::entity group = entt::null;
  u32 slot = 0;
};

// One pose forever, in the gallery.
struct gallery_pin {};

enum group_kind : u8 { grp_greet, grp_shake, grp_chain, grp_spar, group_kinds };
const char *group_kind_name(group_kind kind);

struct group {
  group_kind kind = grp_greet;
  bool acting = false; // false while members gather
  f32 timer = 0.0f;
  f32 phase = 0.0f;
  vec2 anchor{};
  vec2 vel{};
  u8 dir = dir_s; // chain facing
  u8 turn = 0;    // sparring: slot of the one attacking
  pose_id move = pose_punch;
  std::vector<entt::entity> members;
};

struct sim_state {
  u32 max_chain = 8; // hand-in-hand chains of 2 .. max_chain people
  bool collide = true;      // people push each other apart
  f32 radius = 5.0f;        // of a normal build; slim and stocky scale it
  u32 neighbours = 6;       // k: each person is pushed by its k nearest overlaps at most
  njin::spatial_kind index = njin::spatial_grid; // grid or quadtree: same result, different speed
  u32 contacts = 0;         // pushes in the last collision step (a pair of movers counts twice)
};
extern sim_state sim;

// Shows the components above in njin_inspector.
void sim_debug_components(njin::context &ctx);

// Where the gallery is, left of the world.
inline constexpr vec2 gallery_origin{-660.0f, 40.0f};
struct label {
  vec2 pos;
  std::string text;
};
const std::vector<label> &gallery_labels();

// Replaces every person and group: the gallery, then `crowd` random people.
void sim_populate(njin::context &ctx, u32 crowd);
// 24 children of `parent` and a random partner, around the parent.
void sim_spawn_family(njin::context &ctx, entt::entity parent);
void sim_update(njin::context &ctx, f32 dt);
void sim_instance(const entt::registry &reg, entt::entity e, bool selected, instance &out);
u32 sim_people(const entt::registry &reg); // not counting the gallery
u32 sim_acting_groups(const entt::registry &reg, group_kind kind);
} // namespace crowd
