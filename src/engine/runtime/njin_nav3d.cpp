#include "njin_nav3d_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_ecs.h"
#include "njin_gizmo.h"
#include "njin_log.h"
#include "njin_physics3d.h"
#include "njin_world3d_impl.h"
#include <DetourCommon.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <raymath.h>

namespace njin {
namespace {
constexpr i32 max_path_polys = 512;
constexpr i32 max_nodes = 4096;
constexpr unsigned short walk_flag = 1;

bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool refuse(bool ok, const char *what) {
  if (ok)
    return false;
  static std::vector<const char *> told;
  if (std::find(told.begin(), told.end(), what) == told.end()) {
    told.push_back(what);
    NJIN_WARN("nav3d: %s given a value that is not finite (NaN or infinite): ignored", what);
  }
  return true;
}

nav3d_mesh_slot *mesh_of(nav3d_store &store, navmesh3d_handle h) {
  if (h.id == 0 || h.id > store.meshes.size())
    return nullptr;
  nav3d_mesh_slot &s = store.meshes[h.id - 1];
  return s.alive ? &s : nullptr;
}

const nav3d_mesh_slot *mesh_of(const nav3d_store &store, navmesh3d_handle h) {
  return mesh_of(const_cast<nav3d_store &>(store), h);
}

nav3d_agent_slot *agent_of(nav3d_store &store, nav3d_agent_handle h) {
  if (h.id == 0 || h.id > store.agents.size())
    return nullptr;
  nav3d_agent_slot &a = store.agents[h.id - 1];
  if (!a.alive || mesh_of(store, navmesh3d_handle{a.navmesh}) == nullptr)
    return nullptr;
  return &a;
}

const nav3d_agent_slot *agent_of(const nav3d_store &store, nav3d_agent_handle h) {
  return agent_of(const_cast<nav3d_store &>(store), h);
}

void free_mesh(nav3d_mesh_slot &s) {
  dtFreeCrowd(s.crowd);
  dtFreeNavMeshQuery(s.query);
  dtFreeNavMesh(s.mesh);
  s.crowd = nullptr;
  s.query = nullptr;
  s.mesh = nullptr;
  s.built = false;
}

// njin's rotation order (transform3d): z, then x, then y. raymath applies the
// left matrix of MatrixMultiply first.
Matrix place(vec3 position, vec3 rotation, vec3 scale) {
  const vec3 r = rotation * (pi / 180.0f);
  Matrix m = MatrixScale(scale.x, scale.y, scale.z);
  m = MatrixMultiply(m, MatrixRotateZ(r.z));
  m = MatrixMultiply(m, MatrixRotateX(r.x));
  m = MatrixMultiply(m, MatrixRotateY(r.y));
  return MatrixMultiply(m, MatrixTranslate(position.x, position.y, position.z));
}

vec3 xform(Matrix m, vec3 p) {
  const Vector3 q = Vector3Transform({p.x, p.y, p.z}, m);
  return {q.x, q.y, q.z};
}

// The triangles of every piece of geometry that reaches into [lo, hi] on x and
// z, in world space.
struct soup {
  std::vector<f32> verts; // x, y, z
  std::vector<i32> tris;
  f32 ymin = FLT_MAX, ymax = -FLT_MAX;
  f32 xmin = FLT_MAX, xmax = -FLT_MAX, zmin = FLT_MAX, zmax = -FLT_MAX;

  i32 add(vec3 p) {
    verts.push_back(p.x);
    verts.push_back(p.y);
    verts.push_back(p.z);
    ymin = std::min(ymin, p.y);
    ymax = std::max(ymax, p.y);
    xmin = std::min(xmin, p.x);
    xmax = std::max(xmax, p.x);
    zmin = std::min(zmin, p.z);
    zmax = std::max(zmax, p.z);
    return (i32)(verts.size() / 3 - 1);
  }
  void tri(vec3 a, vec3 b, vec3 c) {
    tris.push_back(add(a));
    tris.push_back(add(b));
    tris.push_back(add(c));
  }
};

bool overlaps(vec3 a, vec3 b, vec3 c, vec2 lo, vec2 hi) {
  const f32 x0 = std::min({a.x, b.x, c.x}), x1 = std::max({a.x, b.x, c.x});
  const f32 z0 = std::min({a.z, b.z, c.z}), z1 = std::max({a.z, b.z, c.z});
  return x1 >= lo.x && x0 <= hi.x && z1 >= lo.y && z0 <= hi.y;
}

void gather(const context &ctx, const nav3d_mesh_slot &s, vec2 lo, vec2 hi, soup &out) {
  for (usize t = 0; t + 2 < s.tris.size(); t += 3) {
    const vec3 a = s.verts[s.tris[t]], b = s.verts[s.tris[t + 1]], c = s.verts[s.tris[t + 2]];
    if (overlaps(a, b, c, lo, hi))
      out.tri(a, b, c);
  }
  for (const nav3d_model_ref &r : s.models) {
    const model_slot *m = model_slot_of(ctx.model, r.model);
    if (m == nullptr)
      continue;
    const Matrix xf = MatrixMultiply(m->model.transform, place(r.position, r.rotation, r.scale));
    for (i32 k = 0; k < m->model.meshCount; k++) {
      const Mesh &mesh = m->model.meshes[k];
      if (mesh.vertices == nullptr)
        continue;
      for (i32 t = 0; t < mesh.triangleCount; t++) {
        vec3 v[3];
        for (i32 j = 0; j < 3; j++) {
          const i32 i = mesh.indices != nullptr ? (i32)mesh.indices[t * 3 + j] : t * 3 + j;
          v[j] = xform(xf, {mesh.vertices[i * 3], mesh.vertices[i * 3 + 1], mesh.vertices[i * 3 + 2]});
        }
        if (overlaps(v[0], v[1], v[2], lo, hi))
          out.tri(v[0], v[1], v[2]);
      }
    }
  }
  for (terrain3d_handle th : s.terrains) {
    const terrain3d_slot *t = terrain_of(ctx, th);
    if (t == nullptr)
      continue;
    const vec3 o = t->desc.origin;
    // Clamped as floats first: the whole-map query passes infinite bounds.
    const f32 last = (f32)(t->res - 1);
    const i32 i0 = (i32)clamp(std::floor((lo.x - o.x) / t->spacing), 0.0f, last);
    const i32 j0 = (i32)clamp(std::floor((lo.y - o.z) / t->spacing), 0.0f, last);
    const i32 i1 = (i32)clamp(std::ceil((hi.x - o.x) / t->spacing), 0.0f, last);
    const i32 j1 = (i32)clamp(std::ceil((hi.y - o.z) / t->spacing), 0.0f, last);
    auto at = [&](i32 i, i32 j) {
      return vec3{o.x + (f32)i * t->spacing, t->heights[(usize)(j * t->res + i)], o.z + (f32)j * t->spacing};
    };
    // The same split of each square as the drawn mesh and the height field:
    // along the diagonal from (i, j) to (i + 1, j + 1), wound to face up.
    for (i32 j = j0; j < j1; j++)
      for (i32 i = i0; i < i1; i++) {
        const vec3 a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), d = at(i + 1, j + 1);
        out.tri(a, c, d);
        out.tri(a, d, b);
      }
  }
}

unsigned char *build_tile(const nav3d_mesh_slot &s, const soup &geo, const std::vector<i32> &tris, i32 tx, i32 tz,
                          i32 &size) {
  size = 0;
  const navmesh3d_desc &d = s.desc;
  rcConfig cfg{};
  cfg.cs = d.cell_size;
  cfg.ch = d.cell_height;
  cfg.walkableSlopeAngle = d.max_slope;
  cfg.walkableHeight = (i32)std::ceil(d.agent_height / cfg.ch);
  cfg.walkableClimb = (i32)std::floor(d.agent_climb / cfg.ch);
  cfg.walkableRadius = (i32)std::ceil(d.agent_radius / cfg.cs);
  cfg.maxEdgeLen = (i32)(12.0f / cfg.cs);
  cfg.maxSimplificationError = d.edge_max_error;
  cfg.minRegionArea = (i32)(d.region_min * d.region_min);
  cfg.mergeRegionArea = 20 * 20;
  cfg.maxVertsPerPoly = 6;
  cfg.tileSize = s.tile_cells;
  cfg.borderSize = cfg.walkableRadius + 3;
  cfg.width = cfg.tileSize + cfg.borderSize * 2;
  cfg.height = cfg.tileSize + cfg.borderSize * 2;
  // Recast's defaults: the detail mesh samples heights every 6 cells, so on
  // rolling ground points are a few tenths of a metre off (3 cells cost 4.5x the
  // build time for little gain).
  cfg.detailSampleDist = cfg.cs * 6.0f;
  cfg.detailSampleMaxError = cfg.ch;
  cfg.bmin[0] = s.bmin.x + (f32)tx * s.tile_world - (f32)cfg.borderSize * cfg.cs;
  cfg.bmin[1] = s.bmin.y;
  cfg.bmin[2] = s.bmin.z + (f32)tz * s.tile_world - (f32)cfg.borderSize * cfg.cs;
  cfg.bmax[0] = s.bmin.x + (f32)(tx + 1) * s.tile_world + (f32)cfg.borderSize * cfg.cs;
  cfg.bmax[1] = s.bmax.y;
  cfg.bmax[2] = s.bmin.z + (f32)(tz + 1) * s.tile_world + (f32)cfg.borderSize * cfg.cs;

  if (tris.empty())
    return nullptr;

  rcContext rc(false);
  const i32 nverts = (i32)(geo.verts.size() / 3), ntris = (i32)(tris.size() / 3);
  rcHeightfield *solid = rcAllocHeightfield();
  rcCompactHeightfield *chf = nullptr;
  rcContourSet *cset = nullptr;
  rcPolyMesh *pmesh = nullptr;
  rcPolyMeshDetail *dmesh = nullptr;
  unsigned char *data = nullptr;
  std::vector<unsigned char> areas((usize)ntris, 0);
  do {
    if (!rcCreateHeightfield(&rc, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
      break;
    rcMarkWalkableTriangles(&rc, cfg.walkableSlopeAngle, geo.verts.data(), nverts, tris.data(), ntris, areas.data());
    if (!rcRasterizeTriangles(&rc, geo.verts.data(), nverts, tris.data(), areas.data(), ntris, *solid,
                              cfg.walkableClimb))
      break;
    rcFilterLowHangingWalkableObstacles(&rc, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&rc, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&rc, cfg.walkableHeight, *solid);
    chf = rcAllocCompactHeightfield();
    if (!rcBuildCompactHeightfield(&rc, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
      break;
    if (!rcErodeWalkableArea(&rc, cfg.walkableRadius, *chf))
      break;
    if (!rcBuildDistanceField(&rc, *chf))
      break;
    if (!rcBuildRegions(&rc, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea))
      break;
    cset = rcAllocContourSet();
    if (!rcBuildContours(&rc, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
      break;
    if (cset->nconts == 0)
      break;
    pmesh = rcAllocPolyMesh();
    if (!rcBuildPolyMesh(&rc, *cset, cfg.maxVertsPerPoly, *pmesh))
      break;
    dmesh = rcAllocPolyMeshDetail();
    if (!rcBuildPolyMeshDetail(&rc, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh))
      break;
    if (pmesh->npolys == 0)
      break;
    for (i32 i = 0; i < pmesh->npolys; i++)
      pmesh->flags[i] = walk_flag;

    std::vector<f32> link_verts;
    std::vector<f32> link_rad;
    std::vector<unsigned char> link_dir, link_area;
    std::vector<unsigned short> link_flags;
    std::vector<unsigned int> link_id;
    for (usize k = 0; k < s.links.size(); k++) {
      const nav3d_link &l = s.links[k];
      link_verts.insert(link_verts.end(), {l.from.x, l.from.y, l.from.z, l.to.x, l.to.y, l.to.z});
      link_rad.push_back(l.radius);
      link_dir.push_back(l.both_ways ? DT_OFFMESH_CON_BIDIR : 0);
      link_area.push_back(RC_WALKABLE_AREA);
      link_flags.push_back(walk_flag);
      link_id.push_back((unsigned int)(1000 + k));
    }

    dtNavMeshCreateParams p{};
    p.verts = pmesh->verts;
    p.vertCount = pmesh->nverts;
    p.polys = pmesh->polys;
    p.polyAreas = pmesh->areas;
    p.polyFlags = pmesh->flags;
    p.polyCount = pmesh->npolys;
    p.nvp = pmesh->nvp;
    p.detailMeshes = dmesh->meshes;
    p.detailVerts = dmesh->verts;
    p.detailVertsCount = dmesh->nverts;
    p.detailTris = dmesh->tris;
    p.detailTriCount = dmesh->ntris;
    if (!s.links.empty()) {
      p.offMeshConVerts = link_verts.data();
      p.offMeshConRad = link_rad.data();
      p.offMeshConDir = link_dir.data();
      p.offMeshConAreas = link_area.data();
      p.offMeshConFlags = link_flags.data();
      p.offMeshConUserID = link_id.data();
      p.offMeshConCount = (i32)s.links.size();
    }
    p.walkableHeight = d.agent_height;
    p.walkableRadius = d.agent_radius;
    p.walkableClimb = d.agent_climb;
    p.tileX = tx;
    p.tileY = tz;
    p.tileLayer = 0;
    rcVcopy(p.bmin, pmesh->bmin);
    rcVcopy(p.bmax, pmesh->bmax);
    p.cs = cfg.cs;
    p.ch = cfg.ch;
    p.buildBvTree = true;
    if (!dtCreateNavMeshData(&p, &data, &size)) {
      data = nullptr;
      size = 0;
    }
  } while (false);
  rcFreeHeightField(solid);
  rcFreeCompactHeightfield(chf);
  rcFreeContourSet(cset);
  rcFreePolyMesh(pmesh);
  rcFreePolyMeshDetail(dmesh);
  return data;
}

// Rebuilds tiles [tx0, tx1] x [tz0, tz1]; returns how many hold a navmesh.
i32 build_tiles(const context &ctx, nav3d_mesh_slot &s, i32 tx0, i32 tz0, i32 tx1, i32 tz1) {
  tx0 = std::max(tx0, 0);
  tz0 = std::max(tz0, 0);
  tx1 = std::min(tx1, s.tiles_x - 1);
  tz1 = std::min(tz1, s.tiles_z - 1);
  if (tx0 > tx1 || tz0 > tz1)
    return 0;
  const f32 border = ((f32)std::ceil(s.desc.agent_radius / s.desc.cell_size) + 3.0f) * s.desc.cell_size;
  const vec2 lo{s.bmin.x + (f32)tx0 * s.tile_world - border, s.bmin.z + (f32)tz0 * s.tile_world - border};
  const vec2 hi{s.bmin.x + (f32)(tx1 + 1) * s.tile_world + border,
                s.bmin.z + (f32)(tz1 + 1) * s.tile_world + border};
  soup geo;
  gather(ctx, s, lo, hi, geo);
  if (geo.ymin <= geo.ymax) {
    s.bmin.y = std::min(s.bmin.y, geo.ymin - 1.0f);
    s.bmax.y = std::max(s.bmax.y, geo.ymax + s.desc.agent_height);
  }
  // Each triangle goes to every tile its box reaches, border included.
  const i32 nx = tx1 - tx0 + 1, nz = tz1 - tz0 + 1;
  std::vector<std::vector<i32>> buckets((usize)(nx * nz));
  for (usize t = 0; t + 2 < geo.tris.size(); t += 3) {
    const f32 *a = &geo.verts[(usize)geo.tris[t] * 3], *b = &geo.verts[(usize)geo.tris[t + 1] * 3],
              *c = &geo.verts[(usize)geo.tris[t + 2] * 3];
    const f32 x0 = std::min({a[0], b[0], c[0]}) - border, x1 = std::max({a[0], b[0], c[0]}) + border;
    const f32 z0 = std::min({a[2], b[2], c[2]}) - border, z1 = std::max({a[2], b[2], c[2]}) + border;
    const i32 bx0 = std::max(tx0, (i32)std::floor((x0 - s.bmin.x) / s.tile_world));
    const i32 bx1 = std::min(tx1, (i32)std::floor((x1 - s.bmin.x) / s.tile_world));
    const i32 bz0 = std::max(tz0, (i32)std::floor((z0 - s.bmin.z) / s.tile_world));
    const i32 bz1 = std::min(tz1, (i32)std::floor((z1 - s.bmin.z) / s.tile_world));
    for (i32 bz = bz0; bz <= bz1; bz++)
      for (i32 bx = bx0; bx <= bx1; bx++)
        buckets[(usize)((bz - tz0) * nx + (bx - tx0))].insert(
            buckets[(usize)((bz - tz0) * nx + (bx - tx0))].end(), {geo.tris[t], geo.tris[t + 1], geo.tris[t + 2]});
  }
  i32 made = 0;
  for (i32 tz = tz0; tz <= tz1; tz++)
    for (i32 tx = tx0; tx <= tx1; tx++) {
      s.mesh->removeTile(s.mesh->getTileRefAt(tx, tz, 0), nullptr, nullptr);
      i32 size = 0;
      unsigned char *data = build_tile(s, geo, buckets[(usize)((tz - tz0) * nx + (tx - tx0))], tx, tz, size);
      if (data == nullptr)
        continue;
      if (dtStatusFailed(s.mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(data);
        continue;
      }
      made++;
    }
  return made;
}

thread_local rng *random_source = nullptr;
float random01() { return random_source != nullptr ? random_source->unit() : 0.5f; }

const f32 *fv(const vec3 &v) { return &v.x; }
vec3 vf(const f32 *p) { return {p[0], p[1], p[2]}; }

bool nearest_ref(const nav3d_mesh_slot &s, vec3 p, vec3 extents, dtPolyRef &ref, vec3 &out) {
  dtQueryFilter filter;
  f32 q[3];
  ref = 0;
  if (dtStatusFailed(s.query->findNearestPoly(fv(p), fv(extents), &filter, &ref, q)) || ref == 0)
    return false;
  out = vf(q);
  return true;
}
} // namespace

nav3d_store::~nav3d_store() {
  for (nav3d_mesh_slot &s : meshes)
    free_mesh(s);
}

navmesh3d_handle navmesh3d_create(context &ctx, const navmesh3d_desc &desc) {
  const navmesh3d_desc &d = desc;
  if (refuse(finite3(d.bounds_min) && finite3(d.bounds_max), "navmesh3d_create"))
    return {};
  if (!(d.agent_radius >= 0.0f && d.agent_height > 0.0f && d.agent_climb >= 0.0f && d.cell_size > 0.001f &&
        d.cell_height > 0.001f && d.tile_size > d.cell_size * 8.0f && d.max_agents > 0)) {
    NJIN_WARN("navmesh3d_create: agent sizes must not be negative, cell sizes above 1 mm and tile_size at least "
              "8 cells");
    return {};
  }
  nav3d_mesh_slot slot{};
  slot.alive = true;
  slot.desc = desc;
  ctx.nav3d.meshes.push_back(std::move(slot));
  return navmesh3d_handle{(u32)ctx.nav3d.meshes.size()};
}

void navmesh3d_destroy(context &ctx, navmesh3d_handle handle) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr)
    return;
  for (nav3d_agent_slot &a : ctx.nav3d.agents)
    if (a.alive && a.navmesh == handle.id)
      a.alive = false;
  free_mesh(*s);
  *s = nav3d_mesh_slot{};
}

void navmesh3d_add_mesh(context &ctx, navmesh3d_handle handle, const vec3 *positions, u32 vertex_count,
                        const u32 *indices, u32 index_count) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || positions == nullptr || vertex_count == 0)
    return;
  for (u32 i = 0; i < vertex_count; i++)
    if (refuse(finite3(positions[i]), "navmesh3d_add_mesh"))
      return;
  const u32 base = (u32)s->verts.size();
  s->verts.insert(s->verts.end(), positions, positions + vertex_count);
  if (indices != nullptr) {
    for (u32 i = 0; i + 2 < index_count; i += 3) {
      if (indices[i] >= vertex_count || indices[i + 1] >= vertex_count || indices[i + 2] >= vertex_count)
        continue;
      s->tris.insert(s->tris.end(), {base + indices[i], base + indices[i + 1], base + indices[i + 2]});
    }
  } else {
    for (u32 i = 0; i + 2 < vertex_count; i += 3)
      s->tris.insert(s->tris.end(), {base + i, base + i + 1, base + i + 2});
  }
}

void navmesh3d_add_model(context &ctx, navmesh3d_handle handle, model_handle model, vec3 position, vec3 rotation,
                         vec3 scale) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || refuse(finite3(position) && finite3(rotation) && finite3(scale), "navmesh3d_add_model"))
    return;
  if (model_slot_of(ctx.model, model) == nullptr) {
    NJIN_WARN("navmesh3d_add_model: not a loaded model");
    return;
  }
  s->models.push_back({model, position, rotation, scale});
}

void navmesh3d_add_box(context &ctx, navmesh3d_handle handle, vec3 center, vec3 size, vec3 rotation) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || refuse(finite3(center) && finite3(size) && finite3(rotation), "navmesh3d_add_box"))
    return;
  const Matrix m = place(center, rotation, size * 0.5f);
  vec3 c[8];
  for (i32 i = 0; i < 8; i++)
    c[i] = xform(m, {(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f});
  // Six faces as corner quads; each triangle is wound to face out of the box.
  static const i32 faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
  const u32 base = (u32)s->verts.size();
  s->verts.insert(s->verts.end(), c, c + 8);
  for (const auto &f : faces) {
    const u32 q[2][3] = {{(u32)f[0], (u32)f[1], (u32)f[2]}, {(u32)f[0], (u32)f[2], (u32)f[3]}};
    for (const auto &t : q) {
      const vec3 a = c[t[0]], b = c[t[1]], d = c[t[2]];
      const vec3 n = cross(b - a, d - a);
      const bool out = dot(n, (a + b + d) * (1.0f / 3.0f) - center) >= 0.0f;
      s->tris.insert(s->tris.end(), {base + t[0], base + (out ? t[1] : t[2]), base + (out ? t[2] : t[1])});
    }
  }
}

void navmesh3d_add_terrain(context &ctx, navmesh3d_handle handle, terrain3d_handle terrain) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr)
    return;
  if (terrain_of(ctx, terrain) == nullptr) {
    NJIN_WARN("navmesh3d_add_terrain: not a live terrain");
    return;
  }
  s->terrains.push_back(terrain);
}

void navmesh3d_add_link(context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, bool both_ways, f32 radius) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || refuse(finite3(from) && finite3(to) && std::isfinite(radius), "navmesh3d_add_link"))
    return;
  s->links.push_back({from, to, both_ways, std::max(radius, 0.05f)});
}

void navmesh3d_clear_geometry(context &ctx, navmesh3d_handle handle) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr)
    return;
  s->verts.clear();
  s->tris.clear();
  s->models.clear();
  s->terrains.clear();
  s->links.clear();
}

bool navmesh3d_build(context &ctx, navmesh3d_handle handle) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr)
    return false;
  // Agents of the old navmesh go with its crowd.
  for (nav3d_agent_slot &a : ctx.nav3d.agents)
    if (a.alive && a.navmesh == handle.id)
      a.alive = false;
  free_mesh(*s);
  const navmesh3d_desc &d = s->desc;
  if (d.bounds_min != d.bounds_max) {
    s->bmin = {std::min(d.bounds_min.x, d.bounds_max.x), std::min(d.bounds_min.y, d.bounds_max.y),
               std::min(d.bounds_min.z, d.bounds_max.z)};
    s->bmax = {std::max(d.bounds_min.x, d.bounds_max.x), std::max(d.bounds_min.y, d.bounds_max.y),
               std::max(d.bounds_min.z, d.bounds_max.z)};
  } else {
    soup all;
    gather(ctx, *s, {-FLT_MAX, -FLT_MAX}, {FLT_MAX, FLT_MAX}, all);
    if (all.tris.empty()) {
      NJIN_WARN("navmesh3d_build: no geometry was added");
      return false;
    }
    s->bmin = {all.xmin, all.ymin - 1.0f, all.zmin};
    s->bmax = {all.xmax, all.ymax + d.agent_height, all.zmax};
  }
  s->tile_cells = std::max(8, (i32)(d.tile_size / d.cell_size));
  s->tile_world = (f32)s->tile_cells * d.cell_size;
  s->tiles_x = std::max(1, (i32)std::ceil((s->bmax.x - s->bmin.x) / s->tile_world));
  s->tiles_z = std::max(1, (i32)std::ceil((s->bmax.z - s->bmin.z) / s->tile_world));
  const u32 tiles = dtNextPow2((u32)(s->tiles_x * s->tiles_z));
  const i32 tile_bits = std::min((i32)dtIlog2(tiles), 14);
  const i32 poly_bits = 22 - tile_bits;
  dtNavMeshParams params{};
  rcVcopy(params.orig, fv(s->bmin));
  params.tileWidth = s->tile_world;
  params.tileHeight = s->tile_world;
  params.maxTiles = 1 << tile_bits;
  params.maxPolys = 1 << poly_bits;
  s->mesh = dtAllocNavMesh();
  s->query = dtAllocNavMeshQuery();
  s->crowd = dtAllocCrowd();
  if (s->mesh == nullptr || s->query == nullptr || s->crowd == nullptr ||
      dtStatusFailed(s->mesh->init(&params))) {
    NJIN_WARN("navmesh3d_build: out of memory, or the area needs more than 16384 tiles (raise tile_size)");
    free_mesh(*s);
    return false;
  }
  const i32 made = build_tiles(ctx, *s, 0, 0, s->tiles_x - 1, s->tiles_z - 1);
  if (dtStatusFailed(s->query->init(s->mesh, max_nodes)) ||
      !s->crowd->init(d.max_agents, std::max(1.0f, d.agent_radius * 3.0f), s->mesh)) {
    free_mesh(*s);
    return false;
  }
  // The crowd's avoidance at the quality of the Recast demo's "high" setting.
  dtObstacleAvoidanceParams avoid = *s->crowd->getObstacleAvoidanceParams(0);
  avoid.velBias = 0.5f;
  avoid.adaptiveDivs = 7;
  avoid.adaptiveRings = 3;
  avoid.adaptiveDepth = 3;
  s->crowd->setObstacleAvoidanceParams(3, &avoid);
  s->built = true;
  if (made == 0)
    NJIN_WARN("navmesh3d_build: no walkable area found (check agent sizes, max_slope and the geometry's winding)");
  return made > 0;
}

i32 navmesh3d_rebuild(context &ctx, navmesh3d_handle handle, vec3 min, vec3 max) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || !s->built || refuse(finite3(min) && finite3(max), "navmesh3d_rebuild"))
    return 0;
  const f32 x0 = std::min(min.x, max.x), x1 = std::max(min.x, max.x);
  const f32 z0 = std::min(min.z, max.z), z1 = std::max(min.z, max.z);
  return build_tiles(ctx, *s, (i32)std::floor((x0 - s->bmin.x) / s->tile_world),
                     (i32)std::floor((z0 - s->bmin.z) / s->tile_world),
                     (i32)std::floor((x1 - s->bmin.x) / s->tile_world),
                     (i32)std::floor((z1 - s->bmin.z) / s->tile_world));
}

bool navmesh3d_path(const context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, std::vector<vec3> &out) {
  out.clear();
  const nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || !s->built || refuse(finite3(from) && finite3(to), "navmesh3d_path"))
    return false;
  const vec3 ext{2.0f, 4.0f, 2.0f};
  dtPolyRef start = 0, end = 0;
  vec3 sp, ep;
  if (!nearest_ref(*s, from, ext, start, sp) || !nearest_ref(*s, to, ext, end, ep))
    return false;
  dtQueryFilter filter;
  dtPolyRef polys[max_path_polys];
  i32 npolys = 0;
  s->query->findPath(start, end, fv(sp), fv(ep), &filter, polys, &npolys, max_path_polys);
  if (npolys == 0)
    return false;
  if (polys[npolys - 1] != end) {
    f32 q[3];
    s->query->closestPointOnPoly(polys[npolys - 1], fv(ep), q, nullptr);
    ep = vf(q);
  }
  f32 straight[max_path_polys * 3];
  i32 nstraight = 0;
  s->query->findStraightPath(fv(sp), fv(ep), polys, npolys, straight, nullptr, nullptr, &nstraight, max_path_polys);
  for (i32 i = 0; i < nstraight; i++)
    out.push_back(vf(&straight[i * 3]));
  return !out.empty();
}

bool navmesh3d_nearest(const context &ctx, navmesh3d_handle handle, vec3 p, vec3 &out, vec3 extents) {
  const nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || !s->built || refuse(finite3(p) && finite3(extents), "navmesh3d_nearest"))
    return false;
  dtPolyRef ref = 0;
  return nearest_ref(*s, p, extents, ref, out);
}

nav3d_ray navmesh3d_raycast(const context &ctx, navmesh3d_handle handle, vec3 from, vec3 to) {
  nav3d_ray r{};
  r.point = to;
  const nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || !s->built || refuse(finite3(from) && finite3(to), "navmesh3d_raycast"))
    return r;
  dtPolyRef start = 0;
  vec3 sp;
  if (!nearest_ref(*s, from, {2.0f, 4.0f, 2.0f}, start, sp))
    return r;
  dtQueryFilter filter;
  f32 t = 0.0f, normal[3]{};
  dtPolyRef polys[max_path_polys];
  i32 npolys = 0;
  s->query->raycast(start, fv(sp), fv(to), &filter, &t, normal, polys, &npolys, max_path_polys);
  const dtPolyRef last = npolys > 0 ? polys[npolys - 1] : start;
  r.hit = t <= 1.0f;
  r.fraction = r.hit ? t : 1.0f;
  vec3 p = sp + (to - sp) * r.fraction;
  f32 q[3];
  if (dtStatusSucceed(s->query->closestPointOnPoly(last, fv(p), q, nullptr)))
    p = vf(q);
  r.point = p;
  r.normal = r.hit ? vf(normal) : vec3{};
  return r;
}

bool navmesh3d_random_point(context &ctx, navmesh3d_handle handle, vec3 &out) {
  const nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || !s->built)
    return false;
  dtQueryFilter filter;
  dtPolyRef ref = 0;
  f32 q[3];
  random_source = &ctx.random;
  const bool ok = dtStatusSucceed(s->query->findRandomPoint(&filter, random01, &ref, q)) && ref != 0;
  random_source = nullptr;
  if (ok)
    out = vf(q);
  return ok;
}

bool navmesh3d_random_point_near(context &ctx, navmesh3d_handle handle, vec3 center, f32 radius, vec3 &out) {
  const nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || !s->built ||
      refuse(finite3(center) && std::isfinite(radius), "navmesh3d_random_point_near"))
    return false;
  radius = std::max(radius, 0.0f);
  dtPolyRef start = 0;
  vec3 sp;
  if (!nearest_ref(*s, center, {2.0f, 4.0f, 2.0f}, start, sp))
    return false;
  dtQueryFilter filter;
  // The polygons reachable from the centre without walking further than
  // `radius`; Detour's own random point only bounds the polygons it visits, so
  // on big polygons it lands well outside the circle.
  constexpr i32 max_polys = 256;
  dtPolyRef reach[max_polys];
  i32 nreach = 0;
  s->query->findPolysAroundCircle(start, fv(sp), radius, &filter, reach, nullptr, nullptr, &nreach, max_polys);
  for (i32 attempt = 0; attempt < 32 && nreach > 0; attempt++) {
    const f32 a = ctx.random.range(0.0f, 2.0f * pi);
    const f32 r = radius * std::sqrt(ctx.random.unit());
    const vec3 p{sp.x + std::cos(a) * r, sp.y, sp.z + std::sin(a) * r};
    dtPolyRef ref = 0;
    f32 q[3];
    if (dtStatusFailed(s->query->findNearestPoly(fv(p), fv(vec3{0.05f, 2.0f, 0.05f}), &filter, &ref, q)) || ref == 0)
      continue;
    if (std::find(reach, reach + nreach, ref) == reach + nreach)
      continue;
    const f32 dx = q[0] - sp.x, dz = q[2] - sp.z;
    if (dx * dx + dz * dz > radius * radius)
      continue;
    out = vf(q);
    return true;
  }
  // Nothing landed inside (a thin ledge, a tiny radius): the centre itself.
  out = sp;
  return true;
}

void navmesh3d_draw_debug(context &ctx, navmesh3d_handle handle, rgba color) {
  const nav3d_mesh_slot *s = mesh_of(ctx.nav3d, handle);
  if (s == nullptr || !s->built)
    return;
  const dtNavMesh &mesh = *s->mesh;
  const rgba inner{color.r, color.g, color.b, color.a * 0.35f};
  const vec3 lift{0.0f, 0.05f, 0.0f};
  for (i32 i = 0; i < mesh.getMaxTiles(); i++) {
    const dtMeshTile *tile = mesh.getTile(i);
    if (tile == nullptr || tile->header == nullptr)
      continue;
    for (i32 p = 0; p < tile->header->polyCount; p++) {
      const dtPoly &poly = tile->polys[p];
      if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
        const vec3 a = vf(&tile->verts[poly.verts[0] * 3]), b = vf(&tile->verts[poly.verts[1] * 3]);
        gizmo_arrow3d(ctx, a + lift, b + lift, {1.0f, 0.4f, 0.8f, 1.0f});
        continue;
      }
      for (i32 j = 0; j < poly.vertCount; j++) {
        const vec3 a = vf(&tile->verts[poly.verts[j] * 3]);
        const vec3 b = vf(&tile->verts[poly.verts[(j + 1) % poly.vertCount] * 3]);
        const bool edge = poly.neis[j] == 0;
        gizmo_line3d(ctx, a + lift, b + lift, edge ? color : inner);
      }
    }
  }
}

nav3d_agent_handle nav3d_agent_add(context &ctx, navmesh3d_handle navmesh, const nav3d_agent_desc &desc) {
  nav3d_mesh_slot *s = mesh_of(ctx.nav3d, navmesh);
  if (s == nullptr || !s->built || refuse(finite3(desc.position), "nav3d_agent_add"))
    return {};
  vec3 at;
  dtPolyRef ref = 0;
  if (!nearest_ref(*s, desc.position, {2.0f, 4.0f, 2.0f}, ref, at)) {
    NJIN_WARN("nav3d_agent_add: the position is not near the navmesh");
    return {};
  }
  dtCrowdAgentParams p{};
  p.radius = std::max(desc.radius, 0.01f);
  p.height = std::max(desc.height, 0.01f);
  p.maxAcceleration = std::max(desc.max_accel, 0.0f);
  p.maxSpeed = std::max(desc.max_speed, 0.0f);
  p.collisionQueryRange = p.radius * 12.0f;
  p.pathOptimizationRange = p.radius * 30.0f;
  p.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
  if (desc.avoid)
    p.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
  if (desc.separation > 0.0f)
    p.updateFlags |= DT_CROWD_SEPARATION;
  p.obstacleAvoidanceType = 3;
  p.separationWeight = std::max(desc.separation, 0.0f);
  const i32 index = s->crowd->addAgent(fv(at), &p);
  if (index < 0) {
    NJIN_WARN("nav3d_agent_add: the navmesh already has max_agents agents");
    return {};
  }
  nav3d_agent_slot a{};
  a.alive = true;
  a.navmesh = navmesh.id;
  a.index = index;
  a.arrive = desc.arrive_distance > 0.0f ? desc.arrive_distance : p.radius;
  a.character = desc.character;
  ctx.nav3d.agents.push_back(a);
  return nav3d_agent_handle{(u32)ctx.nav3d.agents.size()};
}

void nav3d_agent_remove(context &ctx, nav3d_agent_handle agent) {
  nav3d_agent_slot *a = agent_of(ctx.nav3d, agent);
  if (a == nullptr)
    return;
  mesh_of(ctx.nav3d, navmesh3d_handle{a->navmesh})->crowd->removeAgent(a->index);
  a->alive = false;
}

bool nav3d_agent_set_target(context &ctx, nav3d_agent_handle agent, vec3 target) {
  nav3d_agent_slot *a = agent_of(ctx.nav3d, agent);
  if (a == nullptr || refuse(finite3(target), "nav3d_agent_set_target"))
    return false;
  const nav3d_mesh_slot &s = *mesh_of(ctx.nav3d, navmesh3d_handle{a->navmesh});
  dtPolyRef ref = 0;
  f32 q[3];
  if (dtStatusFailed(s.query->findNearestPoly(fv(target), s.crowd->getQueryHalfExtents(), s.crowd->getFilter(0), &ref,
                                              q)) ||
      ref == 0)
    return false;
  const vec3 at = vf(q);
  // A target that hardly moved keeps the path the crowd already has.
  if (a->has_target && length_sq(at - a->target) < 0.01f)
    return true;
  s.crowd->requestMoveTarget(a->index, ref, q);
  a->has_target = true;
  a->target = at;
  return true;
}

void nav3d_agent_stop(context &ctx, nav3d_agent_handle agent) {
  nav3d_agent_slot *a = agent_of(ctx.nav3d, agent);
  if (a == nullptr)
    return;
  mesh_of(ctx.nav3d, navmesh3d_handle{a->navmesh})->crowd->resetMoveTarget(a->index);
  a->has_target = false;
}

bool nav3d_agent_teleport(context &ctx, nav3d_agent_handle agent, vec3 position) {
  nav3d_agent_slot *a = agent_of(ctx.nav3d, agent);
  if (a == nullptr || refuse(finite3(position), "nav3d_agent_teleport"))
    return false;
  nav3d_mesh_slot &s = *mesh_of(ctx.nav3d, navmesh3d_handle{a->navmesh});
  vec3 at;
  dtPolyRef ref = 0;
  if (!nearest_ref(s, position, {2.0f, 4.0f, 2.0f}, ref, at))
    return false;
  const dtCrowdAgentParams p = s.crowd->getAgent(a->index)->params;
  s.crowd->removeAgent(a->index);
  const i32 index = s.crowd->addAgent(fv(at), &p);
  if (index < 0) {
    a->alive = false;
    return false;
  }
  a->index = index;
  a->has_target = false;
  a->vertical = 0.0f;
  if (a->character.id != 0)
    character3d_set_position(ctx, a->character, at);
  return true;
}

vec3 nav3d_agent_position(const context &ctx, nav3d_agent_handle agent) {
  const nav3d_agent_slot *a = agent_of(ctx.nav3d, agent);
  if (a == nullptr)
    return {};
  return vf(mesh_of(ctx.nav3d, navmesh3d_handle{a->navmesh})->crowd->getAgent(a->index)->npos);
}

vec3 nav3d_agent_velocity(const context &ctx, nav3d_agent_handle agent) {
  const nav3d_agent_slot *a = agent_of(ctx.nav3d, agent);
  if (a == nullptr)
    return {};
  return vf(mesh_of(ctx.nav3d, navmesh3d_handle{a->navmesh})->crowd->getAgent(a->index)->vel);
}

bool nav3d_agent_arrived(const context &ctx, nav3d_agent_handle agent) {
  const nav3d_agent_slot *a = agent_of(ctx.nav3d, agent);
  if (a == nullptr || !a->has_target)
    return false;
  const vec3 p = vf(mesh_of(ctx.nav3d, navmesh3d_handle{a->navmesh})->crowd->getAgent(a->index)->npos);
  const vec3 d = p - a->target;
  return d.x * d.x + d.z * d.z <= a->arrive * a->arrive && std::fabs(d.y) <= 2.0f;
}

i32 nav3d_agent_count(const context &ctx, navmesh3d_handle navmesh) {
  if (mesh_of(ctx.nav3d, navmesh) == nullptr)
    return 0;
  return (i32)std::count_if(ctx.nav3d.agents.begin(), ctx.nav3d.agents.end(),
                            [&](const nav3d_agent_slot &a) { return a.alive && a.navmesh == navmesh.id; });
}

namespace {
void update_crowds(context &ctx) {
  const f32 dt = delta(ctx);
  if (dt <= 0.0f)
    return;
  nav3d_store &store = ctx.nav3d;
  for (usize m = 0; m < store.meshes.size(); m++) {
    nav3d_mesh_slot &s = store.meshes[m];
    if (!s.alive || !s.built)
      continue;
    // A driven agent starts from where its character really is.
    for (nav3d_agent_slot &a : store.agents)
      if (a.alive && a.navmesh == m + 1 && a.character.id != 0 && character3d_active(ctx, a.character)) {
        dtCrowdAgent *ag = s.crowd->getEditableAgent(a.index);
        const vec3 p = character3d_position(ctx, a.character);
        dtVcopy(ag->npos, fv(p));
      }
    s.crowd->update(dt, nullptr);
    const vec3 gravity = physics3d_gravity(ctx);
    for (nav3d_agent_slot &a : store.agents) {
      if (!a.alive || a.navmesh != m + 1 || a.character.id == 0 || !character3d_active(ctx, a.character))
        continue;
      const dtCrowdAgent *ag = s.crowd->getAgent(a.index);
      a.vertical = character3d_grounded(ctx, a.character) ? -1.0f : a.vertical + gravity.y * dt;
      character3d_set_velocity(ctx, a.character, {ag->vel[0], a.vertical, ag->vel[2]});
    }
  }
}

void setup(context &ctx) { ecs_register(ctx, phase_post_update, update_crowds, "nav3d crowd"); }
} // namespace

mod_desc nav3d_module() { return mod_desc{.name = "njin.nav3d", .setup = setup}; }
} // namespace njin
