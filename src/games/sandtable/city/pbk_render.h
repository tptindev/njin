#pragma once

// Drawing a building of the kit, and its doors and shutters (pbk.h has the data).
//
// Every module is loaded by its manifest ID through the engine's glTF loader,
// which keeps each material with its colour, its embedded texture (the wood's
// grain), UVs, normals and tangents, and draws glass with
// KHR_materials_transmission see-through. The nodes' transforms are applied
// as the file has them; the instance scales by the manifest's
// engine_render_scale once and turns only about +Y: no axis swap.
//
// An animated module (a door, a window with wooden shutters, the corner
// window with two rigs) is drawn in two parts: its still part in the instance
// batches, and each leaf on its own, moved by its bone of the clip at the
// door's time (pbk::module_rig: every skin, every bone the clip moves).
//
// A door is shut until something opens it: opening plays the clip's opening
// stretch and holds there, closing plays its closing stretch. The leaf's
// collision follows the bone.

#include "pbk.h"
#include "render_common.h"

namespace sandtable::city::pbk {

struct door_state {
  enum phase_t : u8 { shut, opening, open, closing };
  phase_t phase = shut;
  f32 t = 0.0f;       // the clip's time
  bool locked = false;
  f32 angle = 0.0f;   // the leaf's turn from shut, degrees (from the bone, or the leaf's timeline)
};

// A building of the kit on the table.
struct building3d {
  plan p;
  assembly as;
  placement at;
  std::vector<door_state> doors;
  std::vector<door_state> shutters; // one per assembly::shutters
  std::vector<body3d_handle> bodies; // static: walls, slabs, stair, props
  std::vector<body3d_handle> leaves; // kinematic, one per door
  std::vector<std::vector<body3d_handle>> shutter_leaves; // kinematic, per ground-floor shutter, per leaf
  i32 city_building = -1;
};

// Puts a plan on the table at `center`/`angle` (table units, degrees) and
// assembles it. Shutters start open or shut by the plan's seed.
building3d make_building(const plan &p, vec2 center, f32 angle);

// The whole file of a module ID (every node, the skin and the clip), loaded
// on first use and kept; an invalid handle when the file is missing.
model_handle module_model(context &ctx, const std::string &id);
// The model of an instance batch's key: a module ID for its still part (an
// animated module without its leaves), "ID#d" for its dressing only (no
// substrate, no floor band: a wall the town lays as one welded ring per
// storey, as the kit's generator does), meshes merged by material.
model_handle batch_model(context &ctx, const std::string &key);
// Leaf `k` of an animated module (module_rig_of(id).leaves[k]), merged.
model_handle leaf_model(context &ctx, const std::string &id, i32 k);
void models_unload(context &ctx);

// Where leaf `k` of module `m` stands at clip time `t`, as an instance
// (render units, degrees about x, y, z; uniform scale).
struct leaf_pose {
  vec3 pos{}, rot{};
  f32 scale = 1.0f;
};
leaf_pose leaf_pose_at(const module_rig &rig, i32 k, f32 t, vec3 origin, f32 yaw, f32 scale);

// What the static part of some buildings is drawn from: one instance batch
// per module ID and the runtime boxes, filled by add_static().
struct static_batch {
  std::vector<std::pair<std::string, instances>> modules;
  instances boxes;
  std::vector<std::pair<std::string, instances>> props;
  void clear();
  void upload(context &ctx);
  void draw(context &ctx);
  void destroy(context &ctx);
};

// How much of a building to draw: its floors below `floors` whole; with
// `cut`, floor `floors` is drawn open (its walls cut low, no ceiling) and
// nothing above it.
struct view_cut {
  i32 floors = 99;
  bool cut = false;
  f32 cut_height = 1.5f; // metres the open floor's walls stand
  // Only what shows from outside: the modules and the roof. A shut house in
  // the town sends nothing of its inside to the GPU.
  bool shell = false;
};

void add_static(context &ctx, const building3d &b, const view_cut &v, static_batch &out);

// The animated part: the leaves of doors and shutters at their clip's time,
// the leaves between rooms.
void draw_doors(context &ctx, const building3d &b, const view_cut &v);

// --- Doors and shutters ------------------------------------------------------------------

door_clip clip_of(const building3d &b, i32 door);
// Opens a shut door, shuts an open one (or turns one back half way); false
// when it is locked.
bool door_toggle(building3d &b, i32 door);
// The same for shutter `i` (assembly::shutters).
bool shutter_toggle(building3d &b, i32 i);
// Advances every door's and shutter's clip and moves its leaves' bodies.
void doors_update(context &ctx, building3d &b, f32 dt);
// The door whose leaf or opening `ray` meets first, or -1.
i32 door_pick(const building3d &b, const ray3d &ray, f32 *distance = nullptr);
// A door's middle, table units, and its floor's height (world units).
vec2 door_center(const building3d &b, i32 door);

// --- Physics -------------------------------------------------------------------------------

// Static bodies for the walls (with their real openings), slabs (with their
// voids), stair, rails and props; a kinematic body for each door's leaf and
// each ground-floor shutter's leaves. Without `inside`, only the shell, the
// street door and the shutters: what people out on the street run into.
void physics_add(context &ctx, building3d &b, bool inside = true);
void physics_remove(context &ctx, building3d &b);

} // namespace sandtable::city::pbk
