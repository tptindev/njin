#include "pbk_render.h"

#include "../physics.h"
#include "interior.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace sandtable::city::pbk {

namespace {

constexpr f32 deg = pi / 180.0f;
// Render units a metre: the plan's metres as the table draws them. The same
// number as the manifest's engine_render_scale, which the modules use.
constexpr f32 render_per_metre = units_per_metre * unit3d;

// Every cache is keyed by the kit's revision and the module ID, so a model
// of another edition of the kit is never taken for this one's.
std::map<std::string, model_handle> models;      // the whole file
std::map<std::string, model_handle> batches;     // batch keys: still part, dressing, leaves
std::map<std::string, model_handle> prop_models; // the old interior kit's, by name

std::string rev_key(const std::string &k) { return load_manifest().kit_revision + "|" + k; }

vec2 along_of(f32 yaw) { return {std::cos(yaw * deg), -std::sin(yaw * deg)}; }

f32 render_scale() { return load_manifest().render_scale; }

model_handle prop_model(context &ctx, const std::string &name) {
  auto it = prop_models.find(name);
  if (it != prop_models.end())
    return it->second;
  const std::string path = "assets/models/interior/" + name + ".glb";
  const model_handle m = model_load(ctx, path.c_str());
  prop_models[name] = m;
  return m;
}

// The middle of a prop's model in its own file units (x, z), for putting its
// origin where the footprint's centre should be.
vec2 prop_center(const std::string &name) {
  static std::map<std::string, vec2> cache;
  auto it = cache.find(name);
  if (it != cache.end())
    return it->second;
  const glb_summary g = read_glb("assets/models/interior/" + name + ".glb");
  const vec2 c = g.ok ? vec2{(g.lo.x + g.hi.x) * 0.5f, (g.lo.z + g.hi.z) * 0.5f} : vec2{};
  cache[name] = c;
  return c;
}

instances &batch_of(std::vector<std::pair<std::string, instances>> &list, const std::string &id) {
  for (auto &[k, v] : list)
    if (k == id)
      return v;
  list.push_back({id, instances{}});
  return list.back().second;
}

f32 elev(i32 f) { return static_cast<f32>(f) * storey; }

// Which floors show: whole below `open`, `open` itself cut low when `cut`.
struct shown {
  i32 open = 0;
  bool cut = false;
  i32 storeys = 0;
  bool whole(i32 f) const { return f < open && f < storeys; }
  bool any(i32 f) const { return whole(f) || (cut && f == open); }
  bool roof() const { return !cut && open >= storeys; }
};

shown shown_of(const building3d &b, const view_cut &v) {
  return {std::min(v.floors, b.p.storeys()), v.cut && v.floors < b.p.storeys(), b.p.storeys()};
}

// A solid as a box on the table: centre, height and turn from the plan's frame.
void box_of(instances &out, const building3d &b, const solid &s, f32 z1, rgba col) {
  out.box(b.at.to_table(s.c), s.z0 * units_per_metre,
          {s.half.x * 2.0f * units_per_metre, (z1 - s.z0) * units_per_metre, s.half.y * 2.0f * units_per_metre},
          b.at.angle + s.angle, col);
}

instances leaf_boxes;

// The file's colours are linear (glTF): as sRGB, the colours the rest of the
// game draws in. Clay is rough; glass keeps the loader's see-through finish.
void finish_materials(context &ctx, model_handle m) {
  for (i32 k = 0; m.id != 0 && k < model_material_count(ctx, m); ++k) {
    model_material mm = model_material_get(ctx, m, k);
    mm.color = linear_to_srgb(mm.color);
    if (mm.color.a >= 1.0f) {
      mm.surface.specular = 0.06f;
      mm.surface.shininess = 10.0f;
    }
    model_material_set(ctx, m, k, mm);
  }
}

// Part of a module's file: without its leaves (an animated module's still
// part), without its substrate and floor band too (`dressing`), or one leaf
// (`only`). Meshes merged by material: one draw a material per batch.
model_handle load_part(context &ctx, const module_info &mi, bool dressing, const std::string *only) {
  std::vector<std::string> skip;
  if (dressing) {
    skip.push_back("substrate");
    skip.push_back("floor_band");
  }
  if (!only && mi.animated)
    for (const rig_leaf &l : module_rig_of(mi.id).leaves)
      skip.push_back(l.node);
  // Nothing left (a plain wall's dressing): drawn as nothing, quietly.
  const glb_summary g = read_glb(kit_path(mi.path));
  bool any = false;
  for (const std::string &n : g.mesh_nodes) {
    const bool skipped =
        std::any_of(skip.begin(), skip.end(), [&](const std::string &s) { return n.find(s) != std::string::npos; });
    any = any || (only ? n.find(*only) != std::string::npos : !skipped);
  }
  if (!any)
    return {};
  std::vector<const char *> skip_c;
  for (const std::string &s : skip)
    skip_c.push_back(s.c_str());
  const std::string path = kit_path(mi.path);
  const char *only_c = only ? only->c_str() : nullptr;
  model_load_desc d{.path = path.c_str(), .merge = true};
  if (only) {
    d.only_nodes = &only_c;
    d.only_count = 1;
  } else {
    d.skip_nodes = skip_c.data();
    d.skip_count = static_cast<u32>(skip_c.size());
  }
  const model_handle m = model_load(ctx, d);
  if (m.id == 0)
    NJIN_WARN("pbk: %s (%s) did not load", mi.id.c_str(), path.c_str());
  finish_materials(ctx, m);
  return m;
}

// The full matrix of leaf `k` at `t`, render units, for a module at `origin`
// turned `yaw` and scaled `scale`.
mat4 leaf_matrix(const module_rig &rig, i32 k, f32 t, vec3 origin, f32 yaw, f32 scale) {
  return mat4_move(origin) * mat4_yaw(yaw) * mat4_scale(scale) * rig.leaf_at(k, t);
}

// A placed module's origin and turn in render terms.
struct module_xf {
  vec3 origin;
  f32 yaw;
};
module_xf xf_of(const building3d &b, const module_place &m) { return {b.at.to_render(m.pos), b.at.yaw_of(m.yaw)}; }

// The clip time an animated module stands at.
f32 clip_time(const building3d &b, const module_place &m) {
  if (m.door >= 0 && static_cast<size_t>(m.door) < b.doors.size())
    return b.doors[static_cast<size_t>(m.door)].t;
  if (m.shutter >= 0 && static_cast<size_t>(m.shutter) < b.shutters.size())
    return b.shutters[static_cast<size_t>(m.shutter)].t;
  return clip_of(load_manifest().find(m.id)).shut_pose;
}

} // namespace

leaf_pose leaf_pose_at(const module_rig &rig, i32 k, f32 t, vec3 origin, f32 yaw, f32 scale) {
  const mat4 m = leaf_matrix(rig, k, t, origin, yaw, scale);
  return {{m.m[12], m.m[13], m.m[14]}, mat4_euler(m), scale};
}

building3d make_building(const plan &p, vec2 center, f32 angle) {
  building3d b;
  b.p = p;
  b.at = {center, angle, p.width, p.depth};
  b.as = assemble(p);
  const manifest &M = load_manifest();
  b.doors.assign(b.as.doors.size(), door_state{});
  for (size_t i = 0; i < b.as.doors.size(); ++i)
    if (b.as.doors[i].rigged)
      b.doors[i].t = clip_of(M.find(b.as.doors[i].module_id)).shut_pose;
  // Shutters open by day more often than not: half of them start open.
  b.shutters.assign(b.as.shutters.size(), door_state{});
  for (size_t i = 0; i < b.as.shutters.size(); ++i) {
    const door_clip c = clip_of(M.find(b.as.shutters[i].module_id));
    const bool open = sub_seed(p.seed, 701u + static_cast<u32>(i)) % 100u < 55u;
    b.shutters[i].phase = open ? door_state::open : door_state::shut;
    b.shutters[i].t = open ? c.open_pose : c.shut_pose;
  }
  return b;
}

model_handle module_model(context &ctx, const std::string &id) {
  const std::string key = rev_key(id);
  auto it = models.find(key);
  if (it != models.end())
    return it->second;
  model_handle m{};
  if (const module_info *mi = load_manifest().find(id)) {
    const std::string path = kit_path(mi->path);
    m = model_load(ctx, path.c_str());
    if (m.id == 0)
      NJIN_WARN("pbk: %s (%s) did not load", id.c_str(), path.c_str());
    finish_materials(ctx, m);
  } else {
    NJIN_WARN("pbk: module %s is not in the manifest", id.c_str());
  }
  models[key] = m;
  return m;
}

model_handle batch_model(context &ctx, const std::string &key) {
  const std::string rk = rev_key(key);
  auto it = batches.find(rk);
  if (it != batches.end())
    return it->second;
  model_handle m{};
  const size_t hash = key.find('#');
  const std::string id = key.substr(0, hash), tag = hash == std::string::npos ? "" : key.substr(hash + 1);
  if (const module_info *mi = load_manifest().find(id)) {
    if (tag.rfind("leaf", 0) == 0) {
      const module_rig &rig = module_rig_of(id);
      const i32 k = std::atoi(tag.c_str() + 4);
      if (k >= 0 && k < static_cast<i32>(rig.leaves.size()))
        m = load_part(ctx, *mi, false, &rig.leaves[static_cast<size_t>(k)].node);
    } else {
      m = load_part(ctx, *mi, tag == "d", nullptr);
    }
  } else {
    NJIN_WARN("pbk: module %s is not in the manifest", id.c_str());
  }
  batches[rk] = m;
  return m;
}

model_handle leaf_model(context &ctx, const std::string &id, i32 k) {
  return batch_model(ctx, id + "#leaf" + std::to_string(k));
}

void models_unload(context &ctx) {
  for (std::map<std::string, model_handle> *list : {&batches, &models, &prop_models})
    for (auto &[k, m] : *list)
      if (m.id != 0)
        model_unload(ctx, m);
  batches.clear();
  models.clear();
  prop_models.clear();
  leaf_boxes.destroy(ctx);
}

// --- Static geometry ------------------------------------------------------------------

void static_batch::clear() {
  for (auto &[k, v] : modules)
    v.clear();
  for (auto &[k, v] : props)
    v.clear();
  boxes.clear();
}

void static_batch::upload(context &ctx) {
  for (auto &[k, v] : modules)
    v.upload(ctx);
  for (auto &[k, v] : props)
    v.upload(ctx);
  boxes.upload(ctx);
}

void static_batch::draw(context &ctx) {
  for (auto &[id, v] : modules) {
    const model_handle m = batch_model(ctx, id);
    if (m.id != 0 && v.buffer.id != 0 && v.count() > 0)
      draw_instanced3d(ctx, m, v.buffer, 0, v.count());
  }
  for (auto &[name, v] : props) {
    const model_handle m = prop_model(ctx, name);
    if (m.id != 0 && v.buffer.id != 0 && v.count() > 0)
      draw_instanced3d(ctx, m, v.buffer, 0, v.count());
  }
  material3d_set(ctx, {.specular = 0.06f, .shininess = 10.0f});
  boxes.draw(ctx, mesh3d_cube);
  material3d_set(ctx, {});
}

void static_batch::destroy(context &ctx) {
  for (auto &[k, v] : modules)
    v.destroy(ctx);
  for (auto &[k, v] : props)
    v.destroy(ctx);
  boxes.destroy(ctx);
  modules.clear();
  props.clear();
}

void add_static(context &ctx, const building3d &b, const view_cut &v, static_batch &out) {
  const shown sh = shown_of(b, v);
  const f32 s = render_scale();
  for (const module_place &m : b.as.modules) {
    // A door shows on its open floor too, as the walls round it do, cut low.
    const bool show = m.floor < 0 ? sh.roof() : (sh.whole(m.floor) || ((m.inside || m.door >= 0) && sh.any(m.floor)));
    if (!show || (v.shell && m.inside))
      continue;
    if (m.door >= 0 && !sh.whole(m.floor))
      continue; // a cut floor's door: only its leaf, drawn with draw_doors
    // An animated module's still part here; its leaves move (draw_doors).
    const std::string key = m.dressing ? m.id + "#d" : m.id;
    batch_model(ctx, key); // made now, not mid-frame
    batch_of(out.modules, key).add3(b.at.to_render(m.pos), {s, s, s}, colors::white, b.at.yaw_of(m.yaw));
  }
  for (const solid &so : b.as.solids) {
    if (so.kind == sk_leaf || so.kind == sk_prop || so.kind == sk_collision)
      continue;
    const i32 f = so.floor;
    if (v.shell && !(f == sh.storeys && sh.roof()) && so.kind != sk_facade && so.kind != sk_shell)
      continue;
    if (so.kind == sk_slab) {
      // Slab f is floor f's ground and floor f-1's ceiling: up to the open
      // floor's ground, or the roof when the whole house shows.
      if (f <= sh.open)
        box_of(out.boxes, b, so, so.z1, so.color);
      continue;
    }
    if (sh.whole(f)) {
      if (so.kind != sk_exterior)
        box_of(out.boxes, b, so, so.z1, so.color);
      continue;
    }
    if (f == sh.storeys && sh.roof()) {
      box_of(out.boxes, b, so, so.z1, so.color); // the parapet's corner fillers
      continue;
    }
    if (sh.cut && f == sh.open) {
      // The open floor: every wall, the outer ones too, cut low.
      const f32 top = std::min(so.z1, elev(f) + v.cut_height);
      if (top > so.z0 + 0.01f && so.kind != sk_lintel)
        box_of(out.boxes, b, so, top, so.kind == sk_exterior ? shade(so.color, 0.85f) : so.color);
    }
  }
  const f32 ks = kit_unit * unit3d;
  if (v.shell)
    return;
  for (const furniture &fu : b.as.furniture) {
    if (!sh.any(fu.floor))
      continue;
    prop_model(ctx, fu.model);
    const f32 yaw = -fu.angle;
    const vec2 c = prop_center(fu.model) * (kit_unit / units_per_metre);
    const vec3 off = module_to_plan({c.x, 0.0f, c.y}, {0, 0, 0}, yaw);
    const vec3 origin{fu.c.x - off.x, fu.c.y - off.y, elev(fu.floor) + 0.02f};
    batch_of(out.props, fu.model).add3(b.at.to_render(origin), {ks, ks, ks}, colors::white, b.at.yaw_of(yaw));
  }
}

// --- Doors -------------------------------------------------------------------------------

door_clip clip_of(const building3d &b, i32 i) {
  const door &d = b.as.doors[static_cast<size_t>(i)];
  return d.rigged ? clip_of(load_manifest().find(d.module_id)) : door_clip{};
}

namespace {

// Starts a door or shutter moving the other way: from shut it opens, from
// open it shuts; one caught half way turns back from the matching point of
// the other stretch.
void toggle(door_state &s, const door_clip &c) {
  const f32 open_len = std::max(c.open_to - c.open_from, 1e-4f), close_len = std::max(c.close_to - c.close_from, 1e-4f);
  switch (s.phase) {
  case door_state::shut: s.phase = door_state::opening; s.t = c.open_from; break;
  case door_state::open: s.phase = door_state::closing; s.t = c.close_from; break;
  case door_state::opening:
    s.phase = door_state::closing;
    s.t = c.close_from + (1.0f - (s.t - c.open_from) / open_len) * close_len;
    break;
  case door_state::closing:
    s.phase = door_state::opening;
    s.t = c.open_from + (1.0f - (s.t - c.close_from) / close_len) * open_len;
    break;
  }
}

// One step of a moving door or shutter; true while it moves.
bool advance(door_state &s, const door_clip &c, f32 dt) {
  if (s.phase == door_state::opening) {
    s.t += dt;
    if (s.t >= c.open_to) {
      s.t = c.open_pose; // held open: the clip is not looped
      s.phase = door_state::open;
    }
    return true;
  }
  if (s.phase == door_state::closing) {
    s.t += dt;
    if (s.t >= c.close_to) {
      s.t = c.shut_pose;
      s.phase = door_state::shut;
    }
    return true;
  }
  return false;
}

} // namespace

bool door_toggle(building3d &b, i32 i) {
  if (i < 0 || i >= static_cast<i32>(b.doors.size()))
    return false;
  door_state &s = b.doors[static_cast<size_t>(i)];
  if (s.locked)
    return false;
  toggle(s, clip_of(b, i));
  return true;
}

bool shutter_toggle(building3d &b, i32 i) {
  if (i < 0 || i >= static_cast<i32>(b.shutters.size()))
    return false;
  door_state &s = b.shutters[static_cast<size_t>(i)];
  if (s.locked)
    return false;
  toggle(s, clip_of(load_manifest().find(b.as.shutters[static_cast<size_t>(i)].module_id)));
  return true;
}

namespace {

// The leaf's turn at clip time `t`: its bone's for a rigged door; for a leaf
// between rooms the same timeline, eased.
f32 leaf_angle(const building3d &b, i32 i, f32 t) {
  const door &d = b.as.doors[static_cast<size_t>(i)];
  const door_clip c = clip_of(b, i);
  if (d.rigged) {
    const module_rig &rig = module_rig_of(d.module_id);
    if (rig.ok)
      return rig.turn(0, t, c.shut_pose);
  }
  f32 open = 0.0f;
  if (t <= c.open_to)
    open = (t - c.open_from) / (c.open_to - c.open_from);
  else if (t <= c.close_from)
    open = 1.0f;
  else if (t <= c.close_to)
    open = 1.0f - (t - c.close_from) / (c.close_to - c.close_from);
  open = std::clamp(open, 0.0f, 1.0f);
  open = open * open * (3.0f - 2.0f * open);
  return d.swing * open;
}

void place_leaf_body(context &ctx, building3d &b, i32 i, bool move) {
  if (static_cast<size_t>(i) >= b.leaves.size() || b.leaves[static_cast<size_t>(i)].id == 0)
    return;
  const door &d = b.as.doors[static_cast<size_t>(i)];
  const vec2 dir = along_of(d.closed_angle + b.doors[static_cast<size_t>(i)].angle);
  const vec2 c = b.at.to_table(d.hinge + dir * (d.width * 0.5f));
  const f32 h = elev(d.floor) + (d.z0 + d.z1) * 0.5f;
  const vec3 pos = to_phys(c, h * units_per_metre);
  const vec3 rot{0.0f, -(b.at.angle + angle_of(dir)), 0.0f};
  if (move)
    body3d_move_kinematic(ctx, b.leaves[static_cast<size_t>(i)], pos, rot);
  else
    body3d_set_position(ctx, b.leaves[static_cast<size_t>(i)], pos, rot);
}

// A shutter leaf's box at its clip time: its bounds round the leaf's centre,
// moved and turned as the bone moves it (physics metres).
void place_shutter_bodies(context &ctx, building3d &b, i32 i, bool move) {
  if (static_cast<size_t>(i) >= b.shutter_leaves.size())
    return;
  const shutter &sh = b.as.shutters[static_cast<size_t>(i)];
  const module_place &m = b.as.modules[static_cast<size_t>(sh.module)];
  const module_rig &rig = module_rig_of(sh.module_id);
  const module_xf x = xf_of(b, m);
  const std::vector<body3d_handle> &bodies = b.shutter_leaves[static_cast<size_t>(i)];
  for (size_t k = 0; k < bodies.size() && k < rig.leaves.size(); ++k) {
    if (bodies[k].id == 0)
      continue;
    const rig_leaf &l = rig.leaves[k];
    const mat4 mm = leaf_matrix(rig, static_cast<i32>(k), b.shutters[static_cast<size_t>(i)].t, x.origin, x.yaw,
                                render_per_metre);
    const vec3 c = mat4_point(mm, (l.lo + l.hi) * 0.5f) * (1.0f / render_per_metre);
    const vec3 rot = mat4_euler(mm);
    if (move)
      body3d_move_kinematic(ctx, bodies[k], c, rot);
    else
      body3d_set_position(ctx, bodies[k], c, rot);
  }
}

} // namespace

void doors_update(context &ctx, building3d &b, f32 dt) {
  for (i32 i = 0; i < static_cast<i32>(b.doors.size()); ++i) {
    door_state &s = b.doors[static_cast<size_t>(i)];
    const bool moving = advance(s, clip_of(b, i), dt);
    s.angle = leaf_angle(b, i, s.t);
    if (moving)
      place_leaf_body(ctx, b, i, true);
  }
  for (i32 i = 0; i < static_cast<i32>(b.shutters.size()); ++i) {
    door_state &s = b.shutters[static_cast<size_t>(i)];
    const std::string &id = b.as.shutters[static_cast<size_t>(i)].module_id;
    const door_clip c = clip_of(load_manifest().find(id));
    if (advance(s, c, dt))
      place_shutter_bodies(ctx, b, i, true);
    const module_rig &rig = module_rig_of(id);
    s.angle = rig.ok ? rig.turn(0, s.t, c.shut_pose) : 0.0f;
  }
}

vec2 door_center(const building3d &b, i32 i) {
  const door &d = b.as.doors[static_cast<size_t>(i)];
  return b.at.to_table(d.hinge + along_of(d.closed_angle) * (d.width * 0.5f));
}

i32 door_pick(const building3d &b, const ray3d &ray, f32 *distance_out) {
  i32 best = -1;
  f32 best_t = 1e30f;
  const f32 reach = 0.7f * render_scale(); // 0.7 m round the opening's middle
  for (i32 i = 0; i < static_cast<i32>(b.as.doors.size()); ++i) {
    const door &d = b.as.doors[static_cast<size_t>(i)];
    const vec2 mid = d.hinge + along_of(d.closed_angle) * (d.width * 0.5f);
    const vec3 c = b.at.to_render({mid.x, mid.y, elev(d.floor) + 1.1f});
    const vec3 oc = c - ray.origin;
    const f32 t = dot(oc, ray.direction);
    if (t <= 0.0f)
      continue;
    const vec3 near = ray.origin + ray.direction * t;
    if (length(near - c) < reach && t < best_t) {
      best_t = t;
      best = i;
    }
  }
  if (distance_out)
    *distance_out = best_t;
  return best;
}

void draw_doors(context &ctx, const building3d &b, const view_cut &v) {
  const shown sh = shown_of(b, v);
  const f32 s = render_scale();
  leaf_boxes.clear();
  // The kit's leaves: doors on their floor (cut or whole), shutters with their wall.
  for (const module_place &m : b.as.modules) {
    if (m.door < 0 && m.shutter < 0)
      continue;
    if (m.door >= 0 ? !sh.any(m.floor) : !sh.whole(m.floor))
      continue;
    const module_rig &rig = module_rig_of(m.id);
    const module_xf x = xf_of(b, m);
    const f32 t = clip_time(b, m);
    for (i32 k = 0; k < static_cast<i32>(rig.leaves.size()); ++k) {
      const model_handle lm = leaf_model(ctx, m.id, k);
      const leaf_pose lp = leaf_pose_at(rig, k, t, x.origin, x.yaw, s);
      draw_model(ctx, lm, {.position = lp.pos, .rotation = lp.rot, .scale = {s, s, s}});
    }
  }
  for (i32 i = 0; i < static_cast<i32>(b.as.doors.size()); ++i) {
    const door &d = b.as.doors[static_cast<size_t>(i)];
    if (d.rigged || !sh.any(d.floor))
      continue;
    const vec2 dir = along_of(d.closed_angle + b.doors[static_cast<size_t>(i)].angle);
    const vec2 c = d.hinge + dir * (d.width * 0.5f);
    const f32 top = sh.whole(d.floor) ? d.z1 : std::min(d.z1, v.cut_height);
    leaf_boxes.box(b.at.to_table(c), (elev(d.floor) + d.z0) * units_per_metre,
                   {d.width * units_per_metre, (top - d.z0) * units_per_metre, d.thickness * units_per_metre},
                   b.at.angle + angle_of(dir), rgb8(120, 84, 52));
  }
  if (leaf_boxes.count() > 0) {
    leaf_boxes.upload(ctx);
    leaf_boxes.draw(ctx, mesh3d_cube);
  }
}

// --- Physics --------------------------------------------------------------------------------

void physics_add(context &ctx, building3d &b, bool inside) {
  physics_remove(ctx, b);
  for (const solid &s : b.as.solids) {
    if (s.kind == sk_leaf || s.kind == sk_shell || (!inside && s.kind != sk_exterior && s.kind != sk_facade))
      continue;
    const vec2 c = b.at.to_table(s.c);
    const body3d_handle h = body3d_create(
        ctx, {.shape = shape3d_box,
              .position = to_phys(c, (s.z0 + s.z1) * 0.5f * units_per_metre),
              .rotation = {0.0f, -(b.at.angle + s.angle), 0.0f},
              .size = {std::max(s.half.x * 2.0f, 0.02f), std::max(s.z1 - s.z0, 0.02f), std::max(s.half.y * 2.0f, 0.02f)}});
    if (h.id != 0)
      b.bodies.push_back(h);
  }
  b.leaves.assign(b.as.doors.size(), body3d_handle{});
  for (i32 i = 0; i < static_cast<i32>(b.as.doors.size()); ++i) {
    const door &d = b.as.doors[static_cast<size_t>(i)];
    if (!inside && !d.rigged)
      continue;
    b.leaves[static_cast<size_t>(i)] =
        body3d_create(ctx, {.shape = shape3d_box,
                            .size = {d.width, d.z1 - d.z0, std::max(d.thickness, 0.04f)},
                            .motion = body3d_kinematic});
    place_leaf_body(ctx, b, i, false);
  }
  // The shutters people on the street can walk into: the ground floor's.
  // A leaf's box is its bounds as the loader keeps it, turned by its bone.
  b.shutter_leaves.assign(b.as.shutters.size(), {});
  for (i32 i = 0; i < static_cast<i32>(b.as.shutters.size()); ++i) {
    const shutter &sh = b.as.shutters[static_cast<size_t>(i)];
    if (sh.floor != 0)
      continue;
    for (const rig_leaf &l : module_rig_of(sh.module_id).leaves) {
      const vec3 size = l.hi - l.lo;
      b.shutter_leaves[static_cast<size_t>(i)].push_back(body3d_create(
          ctx, {.shape = shape3d_box,
                .size = {std::max(size.x, 0.02f), std::max(size.y, 0.02f), std::max(size.z, 0.02f)},
                .motion = body3d_kinematic}));
    }
    place_shutter_bodies(ctx, b, i, false);
  }
}

void physics_remove(context &ctx, building3d &b) {
  for (const body3d_handle h : b.bodies)
    body3d_destroy(ctx, h);
  for (const body3d_handle h : b.leaves)
    if (h.id != 0)
      body3d_destroy(ctx, h);
  for (const std::vector<body3d_handle> &list : b.shutter_leaves)
    for (const body3d_handle h : list)
      if (h.id != 0)
        body3d_destroy(ctx, h);
  b.bodies.clear();
  b.leaves.clear();
  b.shutter_leaves.clear();
}

} // namespace sandtable::city::pbk
