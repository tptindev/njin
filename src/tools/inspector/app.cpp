#include "app.h"
#include <cstdio>

namespace inspector {
void send_cmd(app &a, const json_value &cmd) {
  if (a.link.connected())
    a.link.send(njin::json_dump(cmd, false));
}

void select(app &a, long long id) {
  a.selected = id;
  a.selected_detail = json_value{};
  send_cmd(a, json_value::make_object().set("cmd", "select").set("id", (double)id));
}

// --- messages from the game ---

namespace {
void on_world(app &a, const json_value &m) {
  a.types.clear();
  for (const json_value &t : m["types"].items)
    a.types.push_back(t.string_or("?"));
  a.ents.clear();
  a.ents.reserve(m["ents"].size());
  for (const json_value &e : m["ents"].items) {
    entity_row r;
    r.id = (uint32_t)e["id"].number_or(0);
    r.label = e["n"].string_or("");
    r.ram = (long long)e["m"].number_or(0);
    r.gpu_mem = (long long)e["g"].number_or(0);
    for (const json_value &c : e["c"].items)
      r.comps.push_back(c.int_or(0));
    if (e["p"].size() == 2) {
      r.has_pos = true;
      r.x = e["p"][(size_t)0].f32_or(0);
      r.y = e["p"][(size_t)1].f32_or(0);
    }
    if (e["col"].is(json_value::object)) {
      const json_value &c = e["col"];
      r.has_col = true;
      r.col.circle = c["s"].int_or(0) == 1;
      r.col.x = c["b"][(size_t)0].f32_or(0);
      r.col.y = c["b"][(size_t)1].f32_or(0);
      r.col.w = c["b"][(size_t)2].f32_or(0);
      r.col.h = c["b"][(size_t)3].f32_or(0);
      r.col.trigger = c["tr"].bool_or(false);
      r.col.enabled = c["en"].bool_or(true);
    }
    a.ents.push_back(std::move(r));
  }
  a.truncated = m["truncated"].bool_or(false);
  a.has_cam = m["camera"].size() == 4;
  for (int i = 0; i < 4 && a.has_cam; i++)
    a.cam[i] = m["camera"][(size_t)i].f32_or(0);
  a.maps.clear();
  for (const json_value &t : m["tilemaps"].items) {
    tilemap_row r;
    r.id = (uint32_t)t["id"].number_or(0);
    r.x = t["b"][(size_t)0].f32_or(0);
    r.y = t["b"][(size_t)1].f32_or(0);
    r.w = t["b"][(size_t)2].f32_or(0);
    r.h = t["b"][(size_t)3].f32_or(0);
    r.solid = t["solid"].bool_or(false);
    a.maps.push_back(r);
  }
}

void on_message(app &a, const std::string &line) {
  json_value m;
  if (!njin::json_parse(line, m))
    return;
  const std::string t = m["t"].string_or("");
  if (t == "hello") {
    if (m["v"].int_or(0) != protocol_version) {
      char buf[128];
      std::snprintf(buf, sizeof buf, "game speaks protocol %d, this inspector %d: rebuild one of them",
                    m["v"].int_or(0), protocol_version);
      a.problem = buf;
      a.link.close();
      return;
    }
    a.handshake = true;
    a.problem.clear();
    a.game_title = m["title"].string_or("njin");
    a.game_pid = (uint32_t)m["pid"].number_or(0);
    a.engine_version = m["engine"].string_or("");
    a.usage.clear();
    a.rec = {};
    a.mon.attach(a.game_pid);
    a.frames.clear();
    a.fitted = false;
    if (a.selected >= 0)
      select(a, a.selected); // the game forgets the selection on reconnect
  } else if (t == "stats") {
    for (const json_value &f : m["frames"].items) {
      a.frames.push_back(f.f32_or(0));
      if (a.frames.size() > frame_history)
        a.frames.pop_front();
    }
    a.entity_count = (long long)m["entities"].number_or(0);
    a.paused = m["paused"].bool_or(false);
    a.scale = m["scale"].f32_or(1);
    a.scene = m["scene"].string_or("");
    a.collision_debug = m["collision_debug"].bool_or(false);
    a.dropped = (long long)m["dropped"].number_or(0);
    const json_value &r = m["render"];
    a.render = {(long long)r["sprites"].number_or(0),        (long long)r["sprites_culled"].number_or(0),
                (long long)r["tile_chunks"].number_or(0),    (long long)r["emitters"].number_or(0),
                (long long)r["emitters_culled"].number_or(0), (long long)r["particles"].number_or(0),
                (long long)r["particles_gpu"].number_or(0),  (long long)r["instanced"].number_or(0),
                (long long)r["batches"].number_or(0),        (long long)r["post_passes"].number_or(0)};
  } else if (t == "world") {
    on_world(a, m);
  } else if (t == "entity") {
    if ((long long)m["id"].number_or(-1) == a.selected)
      a.selected_detail = std::move(m);
  } else if (t == "prof") {
    a.prof_frame = m["frame"].f32_or(0);
    a.phases.clear();
    for (const json_value &p : m["phases"].items)
      a.phases.push_back({p["n"].string_or(""), p["ms"].f32_or(0)});
    a.systems.clear();
    for (const json_value &s : m["systems"].items)
      a.systems.push_back({s["p"].int_or(0), s["n"].string_or(""), s["ms"].f32_or(0), s["avg"].f32_or(0),
                           s["peak"].f32_or(0), s["calls"].int_or(0)});
  } else if (t == "mem") {
    a.mem_types.clear();
    for (const json_value &r : m["types"].items)
      a.mem_types.push_back({r["n"].string_or(""), (long long)r["count"].number_or(0), (long long)r["size"].number_or(0),
                             (long long)r["heap"].number_or(0), (long long)r["bytes"].number_or(0),
                             r["known"].bool_or(false)});
    a.mem_components = (long long)m["components_bytes"].number_or(0);
    a.mem_registry = (long long)m["registry_bytes"].number_or(0);
    a.mem_entities = (long long)m["entities"].number_or(0);
  } else if (t == "res") {
    a.resources.clear();
    for (const json_value &r : m["items"].items)
      a.resources.push_back({r["k"].string_or(""), r["n"].string_or(""), r["i"].string_or(""),
                             (long long)r["b"].number_or(0), r["gpu"].bool_or(false)});
    a.res_gpu = (long long)m["gpu_bytes"].number_or(0);
    a.res_ram = (long long)m["ram_bytes"].number_or(0);
  } else if (t == "rec") {
    recording_state &r = a.rec;
    r.on = m["on"].bool_or(false);
    r.frames = m["frames"].int_or(0);
    r.secs = m["secs"].f32_or(0);
    r.bytes = (long long)m["bytes"].number_or(0);
    r.width = m["w"].int_or(0);
    r.height = m["h"].int_or(0);
    r.fps = m["fps"].f32_or(0);
    r.max_secs = m["max"].f32_or(0);
    r.file = m["file"].string_or("");
    r.dir = m["dir"].string_or("");
    r.error = m["err"].string_or("");
  } else if (t == "watch") {
    a.watches = m["values"];
  } else if (t == "log") {
    a.logs.push_back({m["lv"].int_or(2), m["src"].string_or(""), m["msg"].string_or("")});
    if (a.logs.size() > log_history)
      a.logs.pop_front();
  }
}
} // namespace

// Reads the game process from the OS about twice a second and keeps two
// minutes of history for the graphs.
static void sample_process(app &a, float dt) {
  a.clock += dt;
  if (!a.mon.update(a.clock))
    return;
  const proc_sample &s = a.mon.last;
  if (!s.valid)
    return;
  usage_history &h = a.usage;
  h.t.push_back(a.clock);
  h.cpu.push_back(s.cpu_percent);
  h.cpu_core.push_back(s.cpu_core_percent);
  h.ram.push_back(s.ram_working_set);
  h.ram_private.push_back(s.ram_private);
  h.gpu.push_back(s.gpu_valid ? s.gpu_percent : 0.0);
  h.gpu_3d.push_back(s.gpu_valid ? s.gpu_3d_percent : 0.0);
  h.vram.push_back(s.gpu_dedicated);
  h.shared.push_back(s.gpu_shared);
  constexpr size_t keep = 240;
  if (h.t.size() > keep) {
    for (std::vector<double> *v : {&h.t, &h.cpu, &h.cpu_core, &h.ram, &h.ram_private, &h.gpu, &h.gpu_3d, &h.vram,
                                   &h.shared})
      v->erase(v->begin(), v->begin() + (long)(v->size() - keep));
  }
}

void update_connection(app &a, float dt) {
  sample_process(a, dt);
  if (a.link.connected()) {
    if (!a.link.pump()) {
      a.handshake = false;
      a.mon.attach(0);
      return;
    }
    std::string line;
    while (a.link.connected() && a.link.next(line))
      on_message(a, line);
    return;
  }
  a.handshake = false;
  if (a.connecting.valid()) {
    const int r = njin::net_connect_poll(a.connecting);
    if (r == 1) {
      a.link.sock = a.connecting;
      a.connecting = {};
    }
    return;
  }
  a.retry -= dt;
  if (a.retry <= 0.0f) {
    a.retry = 0.5f;
    a.connecting = njin::net_connect_start(a.host.c_str(), (uint16_t)a.port);
  }
}
} // namespace inspector
