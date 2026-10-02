#include "pbk_render.h"
#include "render_kit.h"
#include "render_lod.h"

#include "../physics.h"
#include "../clock.h"
#include <cmath>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// The town's houses from the procedural building kit (pbk.h), the old kit
// (render_kit.cpp) standing in until each is ready and wherever the rules
// give NoFit.
//
// Only what the camera comes near is made: each frame the visible, detailed
// chunks' houses are queued, nearest first, and a worker thread runs the
// generator on them (pure data, no context). A finished house is laid into
// chunk-ordered instance batches, one per module, and drawn chunk by chunk
// like the rest of the town; the old kit's instances of it are skipped. A
// door's leaf and each shutter at rest are instances too; a house whose
// leaves move draws them with their clip. The house's box in the physics gives way to its shell: walls
// with the door's real opening, the door's leaf on its hinge.

namespace sandtable::city {

bool pbk_city = true;

namespace {

using pbk::building3d;

enum class state : u8 { none, queued, ready, nofit, skip };

struct slot {
  state s = state::none;
  std::vector<pbk::request> reqs; // best first (pbk::requests_for_building)
  vec2 center{};
  f32 angle = 0.0f;
  building3d b;
  std::string why;
  std::vector<u8> baked; // each shutter's phase as the leaf batches hold it
  pbk::static_batch interior;
  bool interior_loaded = false;
  instances bulbs;
  i32 bulb_hour = -1, bulb_night = -1;
};

const city_map *the_map = nullptr;
u64 map_hash = 0;
std::vector<slot> slots;
std::vector<std::vector<i32>> in_chunk;
std::vector<i32> ready_sorted; // buildings drawn from the kit, for the old kit's skip list

// --- The worker --------------------------------------------------------------------------

struct job {
  i32 id;
  u64 hash;
  std::vector<pbk::request> reqs;
  obb box;
};
struct result {
  i32 id;
  u64 hash;
  bool ok;
  building3d b;
  std::string why;
};

std::mutex mu;
std::condition_variable cv;
std::deque<job> jobs;
std::deque<result> results;
std::thread worker;
bool stopping = false;
i32 in_flight = 0;

void work() {
  for (;;) {
    job j;
    {
      std::unique_lock<std::mutex> lock(mu);
      cv.wait(lock, [] { return stopping || !jobs.empty(); });
      if (stopping)
        return;
      j = std::move(jobs.front());
      jobs.pop_front();
    }
    // The first of the building's archetypes the rules fit.
    pbk::gen_result g;
    for (const pbk::request &rq : j.reqs) {
      g = pbk::generate(rq);
      if (g.ok)
        break;
    }
    result r{j.id, j.hash, g.ok, {}, g.ok ? std::string() : (g.reasons.empty() ? "NoFit" : g.reasons.back())};
    if (g.ok) {
      // The plan's front on the box's front.
      const vec2 c = j.box.center + j.box.axis_y() * (g.p.depth * 0.5f * units_per_metre - j.box.half.y);
      r.b = pbk::make_building(g.p, c, j.box.angle);
    }
    const std::lock_guard<std::mutex> lock(mu);
    results.push_back(std::move(r));
  }
}

void start_worker() {
  if (worker.joinable())
    return;
  // Read once here, so the worker only ever reads them.
  pbk::load_rules();
  pbk::load_manifest();
  stopping = false;
  worker = std::thread(work);
}

void stop_worker() {
  {
    const std::lock_guard<std::mutex> lock(mu);
    stopping = true;
    jobs.clear();
  }
  cv.notify_all();
  if (worker.joinable())
    worker.join();
  results.clear();
  in_flight = 0;
}

// The game may end without view_shutdown(): the worker is stopped before the
// program's statics go, never left running into std::thread's terminate.
struct worker_guard {
  ~worker_guard() { stop_worker(); }
} guard;

// --- Which houses ------------------------------------------------------------------------

// --- The batches ----------------------------------------------------------------------------

struct named {
  std::string id;
  bool roof = false;
  chunked c;
};
std::vector<named> batches; // modules by ID, then the boxes (id "")
chunked boxes;
chunked far_shell;
std::vector<std::pair<u32, u32>> far_spans;
struct window_light { vec3 pos; vec2 at; i32 chunk; };
std::vector<window_light> window_lights;
std::vector<named> doors; // the leaves at rest: shut doors, shutters as they stand; one batch per leaf
// Where each building's instances are, to leave out the open ones and the
// doors that move.
struct span {
  i32 batch; // into `batches` (-1 boxes), or into `doors` for door spans
  u32 first, last;
};
std::vector<std::vector<span>> spans, door_spans;
bool dirty = false;
f32 since_rebuild = 0.0f;

named &named_of(std::vector<named> &list, const std::string &id) {
  for (named &n : list)
    if (n.id == id)
      return n;
  list.push_back({id, false, {}});
  const auto *mi = pbk::load_manifest().find(id.substr(0, id.find('#')));
  list.back().roof = mi && (mi->family == "RoofSlope" || mi->family == "Ridge" || mi->family == "Gable" ||
                            mi->family == "Parapet" || mi->family == "Coping");
  return list.back();
}

void rebuild(context &ctx) {
  const i32 chunks = chunk_count();
  for (named &n : batches)
    n.c.begin(chunks);
  for (named &n : doors)
    n.c.begin(chunks);
  boxes.begin(chunks);
  far_shell.begin(chunks);
  far_spans.assign(slots.size(), {});
  window_lights.clear();
  spans.assign(slots.size(), {});
  door_spans.assign(slots.size(), {});
  pbk::static_batch one;
  const f32 s = pbk::load_manifest().render_scale;
  for (i32 ch = 0; ch < chunks; ++ch) {
    for (named &n : batches)
      n.c.mark(ch);
    for (named &n : doors)
      n.c.mark(ch);
    boxes.mark(ch);
    far_shell.mark(ch);
    for (const i32 id : in_chunk[static_cast<size_t>(ch)]) {
      slot &sl = slots[static_cast<size_t>(id)];
      if (sl.s != state::ready)
        continue;
      // Extrude the roof slab's rectangle decomposition: also follows the
      // rounded footprint, with no windows, interior or animated leaves.
      rgba wall = {0.72f, 0.65f, 0.54f, 1};
      for (const pbk::solid &so : sl.b.as.solids)
        if (so.kind == pbk::sk_shell) { wall = so.color; break; }
      const u32 far_first = far_shell.inst.count();
      for (const pbk::solid &so : sl.b.as.solids)
        if (so.kind == pbk::sk_slab && so.floor == sl.b.p.storeys())
          far_shell.inst.box(sl.b.at.to_table(so.c), 0,
              {so.half.x * 2 * units_per_metre, so.z1 * units_per_metre,
               so.half.y * 2 * units_per_metre}, sl.b.at.angle + so.angle, wall);
      far_spans[static_cast<size_t>(id)] = {far_first, far_shell.inst.count()};
      // One candidate per building, not a light for every pane. Wooden
      // shutters are opaque and are never a source of light through wood.
      for (const pbk::aperture &a : sl.b.p.apertures) {
        const pbk::module_info *mi = pbk::load_manifest().find(a.module_id);
        if (!mi || mi->shutters)
          continue;
        const vec2 p = a.center - a.inward * 0.35f;
        window_lights.push_back({sl.b.at.to_render({p.x, p.y, a.floor * pbk::storey + 1.8f}),
                                 sl.b.at.to_table(p), ch});
        break;
      }
      one.clear();
      pbk::add_static(ctx, sl.b, {.shell = true}, one);
      for (auto &[mid, inst] : one.modules) {
        named &n = named_of(batches, mid);
        if (n.c.start.empty())
          n.c.begin(chunks), n.c.mark(ch);
        const u32 first = n.c.inst.count();
        n.c.inst.data.insert(n.c.inst.data.end(), inst.data.begin(), inst.data.end());
        spans[static_cast<size_t>(id)].push_back({static_cast<i32>(&n - batches.data()), first, n.c.inst.count()});
      }
      {
        const u32 first = boxes.inst.count();
        boxes.inst.data.insert(boxes.inst.data.end(), one.boxes.data.begin(), one.boxes.data.end());
        spans[static_cast<size_t>(id)].push_back({-1, first, boxes.inst.count()});
      }
      // The leaves at rest: the street door shut, each shutter where it stands.
      sl.baked.assign(sl.b.shutters.size(), 0);
      for (size_t i = 0; i < sl.b.shutters.size(); ++i)
        sl.baked[i] = static_cast<u8>(sl.b.shutters[i].phase);
      for (const pbk::module_place &m : sl.b.as.modules) {
        if (m.door < 0 && m.shutter < 0)
          continue;
        const pbk::module_rig &rig = pbk::module_rig_of(m.id);
        const f32 t = m.door >= 0 ? sl.b.doors[static_cast<size_t>(m.door)].t
                                  : sl.b.shutters[static_cast<size_t>(m.shutter)].t;
        for (i32 k = 0; k < static_cast<i32>(rig.leaves.size()); ++k) {
          const std::string key = m.id + "#leaf" + std::to_string(k);
          pbk::batch_model(ctx, key);
          named &n = named_of(doors, key);
          if (n.c.start.empty())
            n.c.begin(chunks), n.c.mark(ch);
          const u32 first = n.c.inst.count();
          const pbk::leaf_pose lp = pbk::leaf_pose_at(rig, k, t, sl.b.at.to_render(m.pos), sl.b.at.yaw_of(m.yaw), s);
          n.c.inst.add_turned(lp.pos, lp.scale, colors::white, lp.rot);
          door_spans[static_cast<size_t>(id)].push_back({static_cast<i32>(&n - doors.data()), first, n.c.inst.count()});
        }
      }
    }
  }
  // Batches that first appeared part way have their earlier chunks empty.
  for (std::vector<named> *list : {&batches, &doors})
    for (named &n : *list) {
      for (i32 c = 1; c <= chunks; ++c)
        if (n.c.start[static_cast<size_t>(c)] < n.c.start[static_cast<size_t>(c - 1)])
          n.c.start[static_cast<size_t>(c)] = n.c.start[static_cast<size_t>(c - 1)];
      n.c.end();
      n.c.inst.upload(ctx);
    }
  boxes.end();
  boxes.inst.upload(ctx);
  far_shell.end();
  far_shell.inst.upload(ctx);
  one.destroy(ctx);
  dirty = false;
  since_rebuild = 0.0f;
}

// Whether a house's leaves are not as its batches hold them: a door open or
// moving, a shutter moving or turned since. Those are drawn on their own.
bool leaves_moved(const slot &sl) {
  for (const pbk::door_state &d : sl.b.doors)
    if (d.phase != pbk::door_state::shut)
      return true;
  for (size_t i = 0; i < sl.b.shutters.size(); ++i)
    if (i >= sl.baked.size() || sl.baked[i] != static_cast<u8>(sl.b.shutters[i].phase))
      return true;
  return false;
}

skip_list skips_for(const std::vector<i32> &cut, i32 batch, bool door_batch,
                    const std::vector<i32> &moving = {}) {
  skip_list out;
  const auto add = [&](i32 id) {
    for (const span &sp : (door_batch ? door_spans : spans)[static_cast<size_t>(id)])
      if (sp.batch == batch)
        out.push_back({sp.first, sp.last});
  };
  for (const i32 id : cut)
    if (id >= 0 && id < static_cast<i32>(spans.size()))
      add(id);
  if (door_batch)
    for (const i32 id : moving)
      add(id);
  std::sort(out.begin(), out.end());
  return out;
}

// --- Taking in the worker's houses ------------------------------------------------------------

void take_results(context &ctx) {
  std::deque<result> got;
  {
    const std::lock_guard<std::mutex> lock(mu);
    got.swap(results);
  }
  for (result &r : got) {
    --in_flight;
    if (r.hash != map_hash || r.id < 0 || r.id >= static_cast<i32>(slots.size()))
      continue;
    slot &sl = slots[static_cast<size_t>(r.id)];
    if (!r.ok) {
      sl.s = state::nofit;
      sl.why = r.why;
      continue;
    }
    sl.b = std::move(r.b);
    sl.b.city_building = r.id;
    sl.s = state::ready;
    // Its shell and street door take the place of its box in the physics.
    if (physics_drop_building(ctx, r.id))
      pbk::physics_add(ctx, sl.b, false);
    ready_sorted.insert(std::lower_bound(ready_sorted.begin(), ready_sorted.end(), r.id), r.id);
    dirty = true;
  }
}

void queue_near() {
  if (in_flight >= 6)
    return;
  const view_cull &vc = cull();
  struct cand {
    f32 d;
    i32 id;
  };
  std::vector<cand> c;
  for (i32 ch = 0; ch < chunk_count(); ++ch) {
    if (!chunk_visible(ch) || !chunk_detailed(ch, vc.detail_r))
      continue;
    for (const i32 id : in_chunk[static_cast<size_t>(ch)])
      if (slots[static_cast<size_t>(id)].s == state::none)
        c.push_back({distance(the_map->buildings[static_cast<size_t>(id)].box.center, vc.center), id});
  }
  std::sort(c.begin(), c.end(), [](const cand &a, const cand &b) { return a.d < b.d || (a.d == b.d && a.id < b.id); });
  {
    const std::lock_guard<std::mutex> lock(mu);
    for (const cand &k : c) {
      if (in_flight >= 6)
        break;
      slot &sl = slots[static_cast<size_t>(k.id)];
      sl.s = state::queued;
      jobs.push_back({k.id, map_hash, sl.reqs, the_map->buildings[static_cast<size_t>(k.id)].box});
      ++in_flight;
    }
  }
  cv.notify_one();
}

// A building drawn open in the cutaway, cached for its floor.
struct cut_view {
  i32 id = -1, floor = -1;
  pbk::static_batch batch;
};
std::vector<cut_view> cut_views;

} // namespace

void pbk_build(context &ctx, const city_map &map) {
  pbk_cleanup(ctx);
  if (!pbk_city)
    return;
  the_map = &map;
  map_hash = map.hash ^ 0x5bd1e995u;
  slots.assign(map.buildings.size(), {});
  in_chunk.assign(static_cast<size_t>(chunk_count()), {});
  for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i) {
    slot &sl = slots[static_cast<size_t>(i)];
    sl.reqs = pbk::requests_for_building(map, i, sl.center);
    if (sl.reqs.empty()) {
      sl.s = state::skip;
      continue;
    }
    in_chunk[static_cast<size_t>(chunk_of(map.buildings[static_cast<size_t>(i)].box.center))].push_back(i);
  }
  start_worker();
}

bool pbk_ready(i32 id) {
  return id >= 0 && id < static_cast<i32>(slots.size()) && slots[static_cast<size_t>(id)].s == state::ready;
}

const std::vector<i32> &pbk_ready_list() { return ready_sorted; }
const std::vector<i32> &pbk_ready_ids() { return ready_sorted; }

pbk::building3d *pbk_building(i32 id) { return pbk_ready(id) ? &slots[static_cast<size_t>(id)].b : nullptr; }

void pbk_update(context &ctx, f32 dt) {
  if (!pbk_city || slots.empty())
    return;
  take_results(ctx);
  queue_near();
  since_rebuild += dt;
  // New houses are laid in at most twice a second: one upload for many.
  if (dirty && (since_rebuild > 0.5f || in_flight == 0))
    rebuild(ctx);
  for (const i32 id : ready_sorted) {
    slot &sl = slots[static_cast<size_t>(id)];
    pbk::doors_update(ctx, sl.b, dt);
    // A shutter come to rest in its other pose: laid into the batches again.
    for (size_t i = 0; i < sl.b.shutters.size(); ++i) {
      const pbk::door_state::phase_t ph = sl.b.shutters[i].phase;
      if ((ph == pbk::door_state::open || ph == pbk::door_state::shut) && i < sl.baked.size() &&
          sl.baked[i] != static_cast<u8>(ph))
        dirty = true;
    }
  }
}

void pbk_draw(context &ctx, const view_options &opt) {
  if (!pbk_city || slots.empty())
    return;
  const bool observation = opt.camera == camera_mode::observation;
  pbk::window_lighting(ctx, opt.night, observation);
  const auto near = [](i32 c) { return chunk_detailed(c, cull().detail_r); };
  const auto far = [&](i32 c) { return !near(c); };
  // Inspect resting poses once per visible house, rather than again for
  // every leaf batch. Offscreen/distant animation is never submitted.
  std::vector<i32> moving;
  for (const i32 id : ready_sorted) {
    const slot &sl = slots[static_cast<size_t>(id)];
    if (near(chunk_of(the_map->buildings[static_cast<size_t>(id)].box.center)) &&
        view_sees(sl.b.at.center, 60.0f) &&
        std::find(opt.cut.begin(), opt.cut.end(), id) == opt.cut.end() && leaves_moved(sl))
      moving.push_back(id);
  }
  const f32 on = smooth_fade(0.25f, 0.55f, opt.night);
  if (on > 0.01f && observation && opt.cut.empty()) {
    // Twelve street lamps plus three windows leaves one slot for gameplay.
    std::vector<std::pair<f32, const window_light *>> candidates;
    for (const window_light &w : window_lights)
      if (chunk_visible(w.chunk) && near(w.chunk)) {
        const f32 d = distance(w.at, cull().center);
        if (d < 160.0f)
          candidates.emplace_back(d, &w);
      }
    const size_t n = std::min<size_t>(3, candidates.size());
    std::partial_sort(candidates.begin(), candidates.begin() + n, candidates.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });
    for (size_t i = 0; i < n; ++i)
      light3d_add(ctx, {.position = candidates[i].second->pos, .color = {1, 0.68f, 0.32f, 1},
                        .intensity = 0.65f * on * (1.0f - smooth_fade(80.0f, 160.0f, candidates[i].first)),
                        .radius = 3.0f * units_per_metre * unit3d, .shadows = false});
  }
  for (i32 k = 0; k < static_cast<i32>(batches.size()); ++k) {
    named &n = batches[static_cast<size_t>(k)];
    const skip_list sk = skips_for(opt.cut, k, false);
    draw_window_chunks(ctx, n.c, n.id, !n.roof, opt, &sk);
  }
  for (i32 k = 0; k < static_cast<i32>(doors.size()); ++k) {
    named &n = doors[static_cast<size_t>(k)];
    const skip_list sk = skips_for(opt.cut, k, true, moving);
    draw_window_chunks(ctx, n.c, n.id, true, opt, &sk);
  }
  const skip_list sb = skips_for(opt.cut, -1, false);
  material3d_set(ctx, {.specular = 0.06f, .shininess = 10.0f});
  draw_chunks(ctx, boxes, mesh3d_cube, near, &sb);
  skip_list sf;
  for (i32 id : opt.cut)
    if (id >= 0 && id < static_cast<i32>(far_spans.size()))
      sf.push_back(far_spans[static_cast<size_t>(id)]);
  std::sort(sf.begin(), sf.end());
  draw_chunks(ctx, far_shell, mesh3d_cube, far, &sf);
  material3d_set(ctx, {});
  // The houses whose doors or shutters move: their leaves with the clip.
  for (const i32 id : moving) {
    const slot &sl = slots[static_cast<size_t>(id)];
    pbk::draw_doors(ctx, sl.b, {.shell = true}, &opt);
  }
  // Detailed interiors are cached only for nearby immersive views. The
  // observation shell remains cheap; its cutaway uses the same room lights.
  std::vector<std::pair<f32, i32>> houses;
  const f32 interior_reach = std::min(std::clamp(pbk::lighting_budget("interior_reach_world_units", 140), 20.0f, 260.0f),
      (std::clamp(pbk::glazing_option("clear_radius_m", 8), 1.0f, 15.0f) + 1) * units_per_metre);
  const size_t max_houses = static_cast<size_t>(std::clamp(pbk::lighting_budget("max_interior_houses_per_view", 8), 1.0f, 16.0f));
  if (!observation) {
    for (const i32 id : ready_sorted) {
      const slot &sl = slots[static_cast<size_t>(id)];
      const vec2 local = sl.b.at.to_local(cull().center);
      const f32 d = (pbk::point_in_polygon(sl.b.p.outer, local) ? 0 :
                     pbk::distance_to_outline(sl.b.p.outer, local)) * units_per_metre;
      if (d < interior_reach && near(chunk_of(the_map->buildings[static_cast<size_t>(id)].box.center)) &&
          view_sees(sl.b.at.center, 60.0f))
        houses.emplace_back(d, id);
    }
    std::sort(houses.begin(), houses.end());
    if (houses.size() > max_houses) houses.resize(max_houses);
  } else {
    for (const i32 id : opt.cut)
      if (pbk_ready(id)) houses.emplace_back(distance(slots[id].b.at.center, cull().center), id);
  }
  struct candidate { f32 distance; vec3 pos; const pbk::room_light *light; };
  std::vector<candidate> room_lights;
  const f32 hour = static_cast<f32>(std::fmod(clock_now(), 24.0));
  for (const auto &[d, id] : houses) {
    (void)d;
    slot &sl = slots[static_cast<size_t>(id)];
    if (!observation) {
      if (!sl.interior_loaded) {
        pbk::add_static(ctx, sl.b, {.inside_only = true}, sl.interior);
        const f32 s = units_per_metre * unit3d;
        for (const pbk::room_light &l : sl.b.lights)
          sl.interior.boxes.add3(sl.b.at.to_render(l.local), vec3{0.28f, 0.05f, 0.28f} * s,
                                 {0.65f, 0.65f, 0.6f, 1}, sl.b.at.yaw_of(0));
        sl.interior.upload(ctx);
        sl.interior_loaded = true;
      }
      sl.interior.draw(ctx);
      pbk::draw_doors(ctx, sl.b, {.inside_only = true}, &opt);
      const i32 h = static_cast<i32>(std::floor(hour)), dark = static_cast<i32>(std::round(opt.night * 64));
      if (sl.bulb_hour != h || sl.bulb_night != dark) {
        sl.bulbs.clear();
        const f32 s = units_per_metre * unit3d;
        for (const pbk::room_light &l : sl.b.lights) {
          const f32 power = pbk::room_light_power(l, hour, opt.night);
          if (power <= 0) continue;
          vec3 p = l.local; p.z -= 0.028f;
          sl.bulbs.add3(sl.b.at.to_render(p), vec3{0.22f, 0.012f, 0.22f} * s,
                        shade(l.color, std::min(power, 1.0f)), sl.b.at.yaw_of(0));
        }
        sl.bulbs.upload(ctx);
        sl.bulb_hour = h; sl.bulb_night = dark;
      }
      material3d_set(ctx, {.emission = {1, 1, 1, 0.8f}, .unlit = true, .cast_shadows = false});
      sl.bulbs.draw(ctx, mesh3d_cube);
      material3d_set(ctx, {});
    }
    const i32 top = observation ? (id == opt.selected || opt.around ? opt.floor : 0) : sl.b.p.storeys() - 1;
    for (const pbk::room_light &l : sl.b.lights) {
      if (l.floor > top || pbk::room_light_power(l, hour, opt.night) <= 0)
        continue;
      const vec3 pos = sl.b.at.to_render(l.local);
      const vec2 at = sl.b.at.to_table({l.local.x, l.local.y});
      room_lights.push_back({distance(at, cull().center) + l.floor * 12.0f, pos, &l});
    }
  }
  std::sort(room_lights.begin(), room_lights.end(), [](const candidate &a, const candidate &b) {
    return a.distance < b.distance;
  });
  const size_t room_budget = static_cast<size_t>(std::clamp(pbk::lighting_budget("max_room_lights_per_view", 4), 0.0f, 4.0f));
  for (size_t i = 0; i < std::min(room_budget, room_lights.size()); ++i) {
    const candidate &c = room_lights[i];
    light3d_add(ctx, {.position = c.pos, .color = c.light->color,
                      .intensity = pbk::room_light_power(*c.light, hour, opt.night),
                      .radius = c.light->radius * units_per_metre * unit3d, .shadows = false});
  }
}

bool pbk_cut_draw(context &ctx, i32 id, i32 floor) {
  building3d *b = pbk_building(id);
  if (!b)
    return false;
  floor = std::clamp(floor, 0, b->p.storeys() - 1);
  cut_view *cv = nullptr;
  for (cut_view &c : cut_views)
    if (c.id == id)
      cv = &c;
  if (!cv) {
    cut_views.push_back({});
    cv = &cut_views.back();
    cv->id = id;
  }
  const pbk::view_cut v{.floors = floor, .cut = true};
  if (cv->floor != floor) {
    cv->batch.clear();
    pbk::add_static(ctx, *b, v, cv->batch);
    cv->batch.upload(ctx);
    cv->floor = floor;
  }
  cv->batch.draw(ctx);
  pbk::draw_doors(ctx, *b, v);
  return true;
}

i32 pbk_door_toggle_near(i32 id, vec2 at) {
  building3d *b = pbk_building(id);
  if (!b)
    return -1;
  i32 best = -1;
  f32 bd = 1e30f;
  for (i32 i = 0; i < static_cast<i32>(b->as.doors.size()); ++i) {
    const f32 d = distance(pbk::door_center(*b, i), at);
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  if (best >= 0)
    pbk::door_toggle(*b, best);
  return best;
}

pbk_stats pbk_last_stats() {
  pbk_stats s;
  for (const slot &sl : slots) {
    s.ready += sl.s == state::ready ? 1 : 0;
    s.nofit += sl.s == state::nofit ? 1 : 0;
    s.queued += sl.s == state::queued ? 1 : 0;
    s.eligible += sl.s != state::skip ? 1 : 0;
  }
  return s;
}

void pbk_cleanup(context &ctx) {
  stop_worker();
  for (slot &sl : slots)
    if (sl.s == state::ready) {
      pbk::physics_remove(ctx, sl.b);
      sl.interior.destroy(ctx);
      sl.bulbs.destroy(ctx);
    }
  slots.clear();
  in_chunk.clear();
  ready_sorted.clear();
  for (named &n : batches)
    n.c.inst.destroy(ctx);
  for (named &n : doors)
    n.c.inst.destroy(ctx);
  batches.clear();
  doors.clear();
  boxes.inst.destroy(ctx);
  boxes.start.clear();
  far_shell.inst.destroy(ctx);
  far_shell.start.clear();
  far_spans.clear();
  window_lights.clear();
  spans.clear();
  door_spans.clear();
  for (cut_view &c : cut_views)
    c.batch.destroy(ctx);
  cut_views.clear();
  the_map = nullptr;
  dirty = false;
}

void pbk_shutdown(context &ctx) {
  pbk_cleanup(ctx);
  pbk::models_unload(ctx);
}

} // namespace sandtable::city
