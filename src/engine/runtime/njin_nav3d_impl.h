#pragma once
#include "njin_internal_only.h"

#include "_mod.h"
#include "_types.h"
#include "njin_nav3d.h"
#include <utility>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;
class dtCrowd;

namespace njin {
// Navmeshes and crowd agents (njin_nav3d.h), on Recast and Detour. Same
// handle rules as the other stores: id N maps to slots[N - 1], id 0 is
// invalid, slots are never reused.

struct nav3d_model_ref {
  model_handle model{};
  vec3 position{}, rotation{}, scale{1.0f, 1.0f, 1.0f};
  u8 area = 0;
};

struct nav3d_link {
  vec3 from{}, to{};
  bool both_ways = true;
  f32 radius = 0.5f;
  u8 area = 0;
};

// A marked volume (navmesh3d_add_area) or an obstacle: a vertical prism over a
// rotated box, or a cylinder when radius > 0.
struct nav3d_volume {
  i32 id = 0;
  vec3 center{}, size{};
  f32 yaw = 0.0f;
  f32 radius = 0.0f;
  u8 area = 0;
};

struct nav3d_mesh_slot {
  bool alive = false;
  navmesh3d_desc desc{};
  // Geometry: meshes and boxes are copied as world triangles; models and
  // terrains are read again at each build.
  std::vector<vec3> verts;
  std::vector<u32> tris;
  std::vector<u8> tri_areas; // one per triangle of `tris`
  std::vector<nav3d_model_ref> models;
  std::vector<terrain3d_handle> terrains;
  std::vector<u8> terrain_areas; // one per terrain
  std::vector<nav3d_link> links;
  std::vector<nav3d_volume> areas;     // navmesh3d_add_area
  std::vector<nav3d_volume> obstacles; // navmesh3d_add_obstacle
  i32 next_volume = 1;
  nav3d_filter filters[16]{};
  std::vector<std::pair<i32, i32>> dirty; // tiles waiting for an obstacle rebuild
  dtNavMesh *mesh = nullptr;
  dtNavMeshQuery *query = nullptr;
  dtCrowd *crowd = nullptr;
  bool built = false;
  vec3 bmin{}, bmax{};     // of the whole navmesh
  i32 tile_cells = 0;      // cells per tile side
  f32 tile_world = 0.0f;   // metres per tile side
  i32 tiles_x = 0, tiles_z = 0;
};

struct nav3d_agent_slot {
  bool alive = false;
  u32 navmesh = 0; // navmesh id
  i32 index = -1;  // dtCrowd agent index
  bool has_target = false;
  vec3 target{};
  f32 arrive = 0.4f;
  character3d_handle character{};
  f32 vertical = 0.0f; // the driven character's fall speed
  i32 filter = 0;      // crowd query filter type
};

struct nav3d_store {
  std::vector<nav3d_mesh_slot> meshes;
  std::vector<nav3d_agent_slot> agents;

  nav3d_store() = default;
  ~nav3d_store();
  nav3d_store(const nav3d_store &) = delete;
  nav3d_store &operator=(const nav3d_store &) = delete;
};

// Core module: moves every crowd in phase_post_update.
mod_desc nav3d_module();
} // namespace njin
