#include "njin_netcode_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_log.h"
#include "njin_netcode.h"
#include "njin_network_impl.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace njin {
namespace {
// Channels of the session's host.
constexpr u8 ch_control = 0; // reliable: welcome
constexpr u8 ch_state = 1;   // unreliable: inputs up, snapshots down
constexpr u8 ch_game = 2;    // reliable game messages
constexpr u8 ch_game_fast = 3; // other game messages
constexpr u32 channel_count = 4;

constexpr u8 msg_welcome = 1;
constexpr u32 max_input_bytes = 255;
constexpr u32 max_components = 31; // bit 0 of a change mask is the owner
constexpr usize server_ring = 32;  // snapshots kept per client as baselines
constexpr usize client_ring = 64;  // decoded snapshots kept by a client
constexpr usize margin_window = 32;
constexpr i32 snap_error = 8; // ticks off the target margin before the client jumps
constexpr usize arrival_window = 64; // snapshots (about 2 s at 30 Hz) to measure jitter over

// What one entity looks like in a snapshot: its owner and the bytes of each
// registered component it has.
struct ent_state {
  u32 owner = 0;
  u32 present = 0; // bit i: has component i
  std::vector<std::string> comps;

  bool operator==(const ent_state &) const = default;
};
using world_state = std::map<u32, ent_state>;

struct remote_client { // server side
  net_peer peer;
  u32 id = 0;
  std::map<u32, std::string> inputs; // by tick
  std::string last_input;
  u32 newest_input = 0;
  std::deque<i32> margins;
  u32 ack = 0;
  std::deque<std::pair<u32, world_state>> sent; // what each snapshot left the client with
  std::unordered_map<u32, f32> priority;
};

struct local_entity { // client side
  entt::entity entity = entt::null;
  u32 owner = 0;
  std::vector<std::deque<std::pair<u32, std::string>>> samples; // per component, for interpolation
  std::vector<std::pair<u32, std::string>> confirmed;            // per component, predicted ones
  u32 applied = 0;                                               // bit i: component i set at least once
};

struct session {
  netcode_role role = netcode_role::none;
  netcode_opts opts;
  net_host host;
  net_peer server; // client: the server
  bool connected = false;
  bool synced = false;
  u32 self = 0;
  u32 tick = 0;
  std::map<u32, std::string> local_inputs;
  std::string last_local_input;
  std::deque<netcode_event> events;

  // server
  std::unordered_map<u32, remote_client> clients;
  std::map<u32, entt::entity> replicated; // net id -> entity
  std::unordered_map<entt::entity, std::pair<u32, u32>> net_of; // entity -> (net id, owner)
  u32 next_net_id = 1;
  u32 last_snapshot = 0;
  u32 missing_inputs = 0;
  u32 snapshot_bytes = 0;

  // client
  std::deque<std::pair<u32, world_state>> received;
  u32 newest_snapshot = 0;
  world_state applied;
  std::unordered_map<u32, local_entity> entities;
  std::unordered_map<entt::entity, u32> id_of_entity;
  i32 margin = 0;
  u32 margin_guard = 0; // ignore margins of inputs older than this (after a jump)
  // Snapshot arrival: tick time minus net_clock() at arrival, over a window.
  // The largest is the least delayed; the spread is the jitter.
  std::deque<f64> arrivals;
  std::deque<u32> arrival_ticks; // the ticks of the same snapshots
  bool has_offset = false;
  f64 offset = 0.0; // server time minus net_clock(), seconds, smoothed
  f64 delay = 0.0;        // interpolation delay in use, seconds
  f64 delay_target = 0.0; // what the recent snapshots ask for
  f32 render_tick = 0.0f;
};
} // namespace

struct netcode_state {
  std::vector<netcode_component_erased> comps;
  std::function<bool(context &, u32, entt::entity)> relevance;
  std::function<f32(context &, u32, entt::entity)> priority;
  std::unique_ptr<session> s;
};

void netcode_state_destroy(netcode_state *state) { delete state; }

namespace {
netcode_state &state_of(context &ctx) {
  if (!ctx_ref<network_store>(ctx)->netcode)
    ctx_ref<network_store>(ctx)->netcode = new netcode_state();
  return *ctx_ref<network_store>(ctx)->netcode;
}

session *session_of(context &ctx) {
  netcode_state *st = ctx_ref<network_store>(ctx)->netcode;
  return st && st->s ? st->s.get() : nullptr;
}

const session *session_of(const context &ctx) { return session_of(const_cast<context &>(ctx)); }

u32 comp_count(const context &ctx) { return ctx_ref<network_store>(ctx)->netcode ? (u32)ctx_ref<network_store>(ctx)->netcode->comps.size() : 0; }

// The widest gap between snapshots seen lately (losses in a row), at least two
// intervals, plus the jitter seen lately.
f64 interp_target(const context &ctx, const session &s, f64 spread) {
  if (s.opts.interp_delay > 0.0f)
    return s.opts.interp_delay;
  u32 gap = s.opts.snapshot_interval * 2;
  for (usize i = 1; i < s.arrival_ticks.size(); ++i)
    gap = std::max(gap, s.arrival_ticks[i] - s.arrival_ticks[i - 1]);
  return (f64)gap * ctx_ref<time_state>(ctx).fixed_dt + spread + 0.015;
}

// --- inputs ---

std::string encode_inputs(const session &s) {
  bit_writer w;
  const u32 newest = s.local_inputs.empty() ? s.tick : s.local_inputs.rbegin()->first;
  // Consecutive ticks down from the newest, as many as redundancy allows.
  std::vector<const std::string *> list;
  for (u32 t = newest; t > 0 && list.size() < s.opts.input_redundancy; --t) {
    const auto it = s.local_inputs.find(t);
    if (it == s.local_inputs.end())
      break;
    list.push_back(&it->second);
  }
  w.write_u32(newest);
  w.write_u32(s.newest_snapshot);
  w.write_int((i32)list.size(), 0, 32);
  for (usize i = 0; i < list.size(); ++i) {
    // An input the same as the one after it costs one bit.
    const bool same = i > 0 && *list[i] == *list[i - 1];
    if (i > 0)
      w.write_bool(same);
    if (!same)
      w.write_string(*list[i], max_input_bytes);
  }
  return w.data();
}

void decode_inputs(session &s, remote_client &c, std::string_view data) {
  bit_reader r(data);
  const u32 newest = r.read_u32();
  const u32 ack = r.read_u32();
  const i32 count = r.read_int(0, 32);
  std::vector<std::string> list;
  for (i32 i = 0; i < count && r.ok(); ++i) {
    if (i > 0 && r.read_bool())
      list.push_back(list.back());
    else
      list.push_back(r.read_string(max_input_bytes));
  }
  if (!r.ok())
    return; // damaged or hostile: ignore
  if (ack > c.ack)
    c.ack = ack;
  if (count > 0) {
    for (i32 i = 0; i < count; ++i) {
      if ((u32)i >= newest)
        break;
      const u32 t = newest - (u32)i;
      // Only ticks still to come, and not absurdly far ahead.
      if (t >= s.tick && t <= s.tick + 256)
        c.inputs.emplace(t, list[(usize)i]);
    }
    // A client that jumped its tick makes the old reports meaningless.
    if (c.newest_input != 0 && std::abs((i64)newest - (i64)c.newest_input) > snap_error)
      c.margins.clear();
    c.newest_input = newest; // inputs travel sequenced: this is the latest
    c.margins.push_back((i32)((i64)newest - (i64)s.tick));
    if (c.margins.size() > margin_window)
      c.margins.pop_front();
  }
}

bool read_input(session &s, u32 player, std::string &out) {
  if (player == 0) {
    const auto it = s.local_inputs.find(s.tick);
    if (it != s.local_inputs.end()) {
      out = it->second;
      s.last_local_input = it->second;
      return true;
    }
    out = s.last_local_input;
    return false;
  }
  const auto ci = s.clients.find(player);
  if (ci == s.clients.end()) {
    out.clear();
    return false;
  }
  remote_client &c = ci->second;
  c.inputs.erase(c.inputs.begin(), c.inputs.lower_bound(s.tick));
  const auto it = c.inputs.find(s.tick);
  if (it != c.inputs.end()) {
    out = it->second;
    c.last_input = it->second;
    return true;
  }
  if (c.newest_input > 0)
    ++s.missing_inputs;
  out = c.last_input;
  return false;
}

// --- server snapshots ---

world_state capture(context &ctx, session &s) {
  const netcode_state &st = *ctx_ref<network_store>(ctx)->netcode;
  entt::registry &reg = world(ctx);
  world_state out;
  for (auto it = s.replicated.begin(); it != s.replicated.end();) {
    const entt::entity e = it->second;
    if (!reg.valid(e)) { // destroyed by the game: gone for the clients too
      s.net_of.erase(e);
      it = s.replicated.erase(it);
      continue;
    }
    ent_state es;
    es.owner = s.net_of[e].second;
    es.comps.resize(st.comps.size());
    for (usize i = 0; i < st.comps.size(); ++i) {
      bit_writer w;
      if (st.comps[i].write(reg, e, w)) {
        es.present |= 1u << i;
        es.comps[i] = w.data();
      }
    }
    out.emplace(it->first, std::move(es));
    ++it;
  }
  return out;
}

std::string encode_entity(u32 id, const ent_state &now, const ent_state *base, u32 comps) {
  bit_writer w;
  w.write_varint(id);
  u32 changed = 0;
  if (!base || base->owner != now.owner)
    changed |= 1u;
  for (u32 i = 0; i < comps; ++i)
    if ((now.present >> i & 1u) && (!base || !(base->present >> i & 1u) || base->comps[i] != now.comps[i]))
      changed |= 1u << (i + 1);
  w.write_bits(changed, comps + 1);
  w.write_bits(now.present, comps);
  if (changed & 1u)
    w.write_varint(now.owner);
  for (u32 i = 0; i < comps; ++i)
    if (changed >> (i + 1) & 1u) {
      w.write_varint(now.comps[i].size());
      w.write_bytes(now.comps[i]);
    }
  return w.data();
}

void send_snapshot(context &ctx, session &s, remote_client &c, const world_state &now) {
  const netcode_state &st = *ctx_ref<network_store>(ctx)->netcode;
  const u32 comps = (u32)st.comps.size();
  // Baseline: the snapshot the client last confirmed, if still kept.
  const world_state *base = nullptr;
  u32 base_tick = 0;
  for (const auto &[t, ws] : c.sent)
    if (t == c.ack) {
      base = &ws;
      base_tick = t;
    }
  static const world_state empty;
  const world_state &b = base ? *base : empty;

  std::vector<u32> visible_removed;
  struct candidate {
    u32 id;
    f32 priority;
  };
  std::vector<candidate> candidates;
  std::vector<u32> visible;
  for (const auto &[id, es] : now) {
    const entt::entity e = s.replicated.at(id);
    const bool relevant = es.owner == c.id || !st.relevance || st.relevance(ctx, c.id, e);
    if (!relevant)
      continue;
    visible.push_back(id);
    const auto bi = b.find(id);
    if (bi != b.end() && bi->second == es)
      continue; // the client has it as it is
    f32 &acc = c.priority[id];
    acc += st.priority ? std::max(st.priority(ctx, c.id, e), 0.0f) : 1.0f;
    candidates.push_back({id, acc});
  }
  for (const auto &[id, es] : b)
    if (!std::binary_search(visible.begin(), visible.end(), id))
      visible_removed.push_back(id);
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const candidate &x, const candidate &y) { return x.priority > y.priority; });

  bit_writer w;
  w.write_u32(s.tick);
  w.write_u32(base_tick);
  const bool has_margin = !c.margins.empty();
  w.write_bool(has_margin);
  if (has_margin) {
    w.write_int(*std::min_element(c.margins.begin(), c.margins.end()), -128, 127);
    w.write_u32(c.newest_input);
  }
  w.write_varint(visible_removed.size());
  u32 prev = 0;
  for (const u32 id : visible_removed) { // ascending: write the gaps
    w.write_varint(id - prev);
    prev = id;
  }
  // Entities in priority order until the budget is spent.
  std::vector<std::string> chunks;
  usize size = w.data().size() + 4;
  world_state left = b; // what the client will have after this snapshot
  for (const u32 id : visible_removed) {
    left.erase(id);
    c.priority.erase(id);
  }
  for (const candidate &cand : candidates) {
    const ent_state &es = now.at(cand.id);
    const auto bi = b.find(cand.id);
    std::string chunk = encode_entity(cand.id, es, bi == b.end() ? nullptr : &bi->second, comps);
    if (!chunks.empty() && size + chunk.size() > s.opts.snapshot_budget)
      continue; // a smaller one may still fit
    size += chunk.size();
    chunks.push_back(std::move(chunk));
    left[cand.id] = es;
    c.priority[cand.id] = 0.0f;
  }
  w.write_varint(chunks.size());
  for (const std::string &chunk : chunks)
    w.write_bytes(chunk);
  net_peer_send(ctx, c.peer, w.data(), net_delivery::unreliable, ch_state);
  s.snapshot_bytes = std::max<u32>(s.snapshot_bytes, (u32)w.data().size());
  c.sent.emplace_back(s.tick, std::move(left));
  if (c.sent.size() > server_ring)
    c.sent.pop_front();
}

// --- client snapshots ---

void destroy_local(context &ctx, session &s, u32 id) {
  const auto it = s.entities.find(id);
  if (it == s.entities.end())
    return;
  const entt::entity e = it->second.entity;
  entt::registry &reg = world(ctx);
  if (reg.valid(e))
    reg.destroy(e);
  s.id_of_entity.erase(e);
  s.entities.erase(it);
  netcode_event ev;
  ev.kind = netcode_event_kind::despawned;
  ev.entity = e;
  ev.net_id = id;
  s.events.push_back(std::move(ev));
}

void apply(context &ctx, session &s, const world_state &ws, u32 tick) {
  const netcode_state &st = *ctx_ref<network_store>(ctx)->netcode;
  entt::registry &reg = world(ctx);
  for (const auto &[id, es] : s.applied)
    if (!ws.count(id))
      destroy_local(ctx, s, id);
  for (const auto &[id, es] : ws) {
    local_entity &le = s.entities[id];
    if (le.entity == entt::null || !reg.valid(le.entity)) {
      le = local_entity{};
      le.entity = reg.create();
      le.samples.resize(st.comps.size());
      le.confirmed.resize(st.comps.size());
      s.id_of_entity[le.entity] = id;
      netcode_event ev;
      ev.kind = netcode_event_kind::spawned;
      ev.entity = le.entity;
      ev.net_id = id;
      s.events.push_back(std::move(ev));
    }
    le.owner = es.owner;
    const bool mine = es.owner != 0 && es.owner == s.self;
    for (usize i = 0; i < st.comps.size(); ++i) {
      const netcode_component_erased &comp = st.comps[i];
      const u32 bit = 1u << i;
      if (!(es.present & bit)) {
        if (le.applied & bit) {
          comp.remove(reg, le.entity);
          le.applied &= ~bit;
          le.samples[i].clear();
          le.confirmed[i] = {};
        }
        continue;
      }
      const std::string &bytes = es.comps[i];
      if (comp.predicted && mine) {
        le.confirmed[i] = {tick, bytes};
        if (!(le.applied & bit)) { // the first value starts the game's own simulation
          comp.read(reg, le.entity, bytes);
          le.applied |= bit;
        }
        continue;
      }
      if (comp.lerp && !mine) {
        auto &samples = le.samples[i];
        if (samples.empty() || samples.back().first < tick)
          samples.emplace_back(tick, bytes);
        if (!(le.applied & bit)) {
          comp.read(reg, le.entity, bytes);
          le.applied |= bit;
        }
        continue;
      }
      const auto prev = s.applied.find(id);
      const bool changed = prev == s.applied.end() || !(prev->second.present & bit) || prev->second.comps[i] != bytes ||
                           !(le.applied & bit);
      if (changed) {
        comp.read(reg, le.entity, bytes);
        le.applied |= bit;
      }
    }
  }
  s.applied = ws;
}

void adjust_tick(context &ctx, session &s, i32 margin, u32 input_tick) {
  if (input_tick < s.margin_guard)
    return; // reports on inputs sent before the last jump
  s.margin = margin;
  const i32 error = margin - (i32)s.opts.input_margin;
  if (std::abs(error) > snap_error) {
    // Far off (start, a hitch, a lag spike): jump instead of drifting.
    s.tick = (u32)std::max<i64>(1, (i64)s.tick - error);
    s.margin_guard = s.tick;
    // Inputs set for ticks we now come back to are set again as they come.
    s.local_inputs.erase(s.local_inputs.upper_bound(s.tick + s.opts.input_delay), s.local_inputs.end());
    time_set_fixed_rate(ctx, 1.0f);
    return;
  }
  // Within reach: run up to 4% faster or slower until on target.
  time_set_fixed_rate(ctx, 1.0f - (f32)std::clamp(error, -4, 4) * 0.01f);
}

void receive_snapshot(context &ctx, session &s, std::string_view data) {
  const u32 comps = comp_count(ctx);
  bit_reader r(data);
  const u32 tick = r.read_u32();
  const u32 base_tick = r.read_u32();
  const bool has_margin = r.read_bool();
  i32 margin = 0;
  u32 input_tick = 0;
  if (has_margin) {
    margin = r.read_int(-128, 127);
    input_tick = r.read_u32();
  }
  if (!r.ok() || tick <= s.newest_snapshot)
    return;
  world_state ws;
  if (base_tick != 0) {
    const auto it = std::find_if(s.received.begin(), s.received.end(),
                                 [&](const auto &p) { return p.first == base_tick; });
    if (it == s.received.end())
      return; // baseline no longer kept: the server will send in full
    ws = it->second;
  }
  const u64 removed = r.read_varint();
  u32 id = 0;
  for (u64 i = 0; i < removed && r.ok(); ++i) {
    id += (u32)r.read_varint();
    ws.erase(id);
  }
  const u64 count = r.read_varint();
  for (u64 n = 0; n < count && r.ok(); ++n) {
    r.read_bytes(0); // each entity starts on a whole byte
    const u32 eid = (u32)r.read_varint();
    const u32 changed = r.read_bits(comps + 1);
    const u32 present = r.read_bits(comps);
    ent_state &es = ws[eid];
    es.comps.resize(comps);
    es.present = present;
    if (changed & 1u)
      es.owner = (u32)r.read_varint();
    for (u32 i = 0; i < comps; ++i) {
      if (!(present >> i & 1u)) {
        es.comps[i].clear();
        continue;
      }
      if (changed >> (i + 1) & 1u) {
        const u64 len = r.read_varint();
        es.comps[i] = len <= 65536 ? r.read_bytes((usize)len) : std::string();
        if (len > 65536)
          r.read_bytes(1u << 30); // fails the reader
      }
    }
  }
  if (!r.ok())
    return;
  s.newest_snapshot = tick;
  s.received.emplace_back(tick, ws);
  if (s.received.size() > client_ring)
    s.received.pop_front();

  // Server time for interpolation follows the least delayed snapshots; the
  // delay grows at once with the jitter and shrinks slowly, so a late
  // snapshot is waited for instead of snapped to.
  s.arrivals.push_back((f64)tick * ctx_ref<time_state>(ctx).fixed_dt - net_clock(ctx));
  s.arrival_ticks.push_back(tick);
  if (s.arrivals.size() > arrival_window) {
    s.arrivals.pop_front();
    s.arrival_ticks.pop_front();
  }
  const auto [lo, hi] = std::minmax_element(s.arrivals.begin(), s.arrivals.end());
  const f64 target = interp_target(ctx, s, *hi - *lo);
  s.delay_target = target;
  if (!s.has_offset || std::fabs(*hi - s.offset) > 0.25) {
    s.offset = *hi;
    s.delay = target;
    s.has_offset = true;
  } else {
    s.offset += (*hi - s.offset) * 0.05;
  }
  apply(ctx, s, ws, tick);
  if (has_margin)
    adjust_tick(ctx, s, margin, input_tick);
}

void interpolate(context &ctx, session &s) {
  if (!s.has_offset)
    return;
  const netcode_state &st = *ctx_ref<network_store>(ctx)->netcode;
  entt::registry &reg = world(ctx);
  const f64 dt = ctx_ref<time_state>(ctx).fixed_dt;
  // The delay moves toward its target by changing how fast the render time
  // runs, by at most 10% (growing) or 2% (shrinking), never by a jump.
  const f64 step = (f64)ctx_ref<time_state>(ctx).dt_real;
  if (s.delay < s.delay_target)
    s.delay = std::min(s.delay_target, s.delay + step * 0.1);
  else
    s.delay = std::max(s.delay_target, s.delay - step * 0.02);
  const f64 render = (net_clock(ctx) + s.offset - s.delay) / dt;
  s.render_tick = (f32)render;
  for (auto &[id, le] : s.entities) {
    if (!reg.valid(le.entity))
      continue;
    for (usize i = 0; i < st.comps.size(); ++i) {
      auto &samples = le.samples[i];
      if (samples.empty() || !st.comps[i].lerp)
        continue;
      // Keep one sample at or before the render tick, drop the older ones.
      while (samples.size() >= 2 && (f64)samples[1].first <= render)
        samples.pop_front();
      const auto &a = samples.front();
      if (samples.size() == 1 || render <= (f64)a.first) {
        st.comps[i].read(reg, le.entity, a.second); // nothing newer yet: hold
        continue;
      }
      const auto &b = samples[1];
      const f32 t = (f32)((render - (f64)a.first) / (f64)(b.first - a.first));
      st.comps[i].lerp(reg, le.entity, a.second, b.second, std::clamp(t, 0.0f, 1.0f));
    }
  }
}

// --- the loop ---

void on_server_event(context &ctx, session &s, net_host_event &e) {
  switch (e.kind) {
  case net_host_event_kind::connected: {
    if (e.code != s.opts.version) {
      net_peer_disconnect(ctx, e.peer, netcode_code_version);
      return;
    }
    if (s.clients.size() >= s.opts.max_clients) {
      net_peer_disconnect(ctx, e.peer, netcode_code_full);
      return;
    }
    remote_client c;
    c.peer = e.peer;
    c.id = e.peer.id;
    s.clients.emplace(c.id, std::move(c));
    bit_writer w;
    w.write_u8(msg_welcome);
    w.write_varint(e.peer.id);
    w.write_u32(s.tick);
    w.write_f32(ctx_ref<time_state>(ctx).fixed_dt);
    net_peer_send(ctx, e.peer, w.data(), net_delivery::reliable, ch_control);
    netcode_event ev;
    ev.kind = netcode_event_kind::joined;
    ev.player = e.peer.id;
    s.events.push_back(std::move(ev));
    break;
  }
  case net_host_event_kind::disconnected: {
    if (s.clients.erase(e.peer.id) == 0)
      return;
    netcode_event ev;
    ev.kind = netcode_event_kind::left;
    ev.player = e.peer.id;
    ev.code = e.code;
    s.events.push_back(std::move(ev));
    break;
  }
  case net_host_event_kind::packet: {
    const auto ci = s.clients.find(e.peer.id);
    if (ci == s.clients.end())
      return;
    if (e.channel == ch_state) {
      decode_inputs(s, ci->second, e.data);
    } else if (e.channel == ch_game || e.channel == ch_game_fast) {
      netcode_event ev;
      ev.kind = netcode_event_kind::message;
      ev.player = e.peer.id;
      ev.data = std::move(e.data);
      s.events.push_back(std::move(ev));
    }
    break;
  }
  }
}

void on_client_event(context &ctx, session &s, net_host_event &e) {
  switch (e.kind) {
  case net_host_event_kind::connected:
    s.connected = true; // ticks start with the welcome
    break;
  case net_host_event_kind::disconnected: {
    s.connected = false;
    s.synced = false;
    std::vector<u32> ids;
    for (const auto &[id, le] : s.entities)
      ids.push_back(id);
    for (const u32 id : ids)
      destroy_local(ctx, s, id);
    s.applied.clear();
    time_set_fixed_rate(ctx, 1.0f);
    netcode_event ev;
    ev.kind = netcode_event_kind::left;
    ev.code = e.code;
    s.events.push_back(std::move(ev));
    break;
  }
  case net_host_event_kind::packet:
    if (e.channel == ch_control) {
      bit_reader r(e.data);
      if (r.read_u8() != msg_welcome)
        return;
      const u32 self = (u32)r.read_varint();
      const u32 server_tick = r.read_u32();
      const f32 server_dt = r.read_f32();
      if (!r.ok())
        return;
      if (std::fabs(server_dt - ctx_ref<time_state>(ctx).fixed_dt) > 1e-6f)
        NJIN_WARN("netcode: server ticks at %.1f Hz, this game at %.1f Hz; set the same config::fixed_hz",
                  1.0 / server_dt, 1.0 / ctx_ref<time_state>(ctx).fixed_dt);
      s.self = self;
      // A first guess; the server's reports on our inputs correct it.
      s.tick = server_tick + s.opts.input_margin + 2;
      s.synced = true;
      netcode_event ev;
      ev.kind = netcode_event_kind::joined;
      ev.player = self;
      s.events.push_back(std::move(ev));
    } else if (e.channel == ch_state) {
      receive_snapshot(ctx, s, e.data);
    } else {
      netcode_event ev;
      ev.kind = netcode_event_kind::message;
      ev.data = std::move(e.data);
      s.events.push_back(std::move(ev));
    }
    break;
  }
}

void receive(context &ctx, session &s) {
  net_host_event e;
  while (net_host_poll(ctx, s.host, e)) {
    if (s.role == netcode_role::server)
      on_server_event(ctx, s, e);
    else
      on_client_event(ctx, s, e);
  }
}

void netcode_pre_update(context &ctx) {
  session *s = session_of(ctx);
  if (!s)
    return;
  receive(ctx, *s);
  if (s->role == netcode_role::client)
    interpolate(ctx, *s);
}

void netcode_fixed(context &ctx) {
  session *s = session_of(ctx);
  if (!s || (s->role == netcode_role::client && !s->synced))
    return;
  ++s->tick;
  receive(ctx, *s); // input that just arrived can still make this tick
  s->local_inputs.erase(s->local_inputs.begin(), s->local_inputs.lower_bound(s->tick > 64 ? s->tick - 64 : 0));
}

void netcode_send_all_state(context &ctx) {
  session *s = session_of(ctx);
  if (!s)
    return;
  if (s->role == netcode_role::server) {
    if (s->tick == 0 || s->tick - s->last_snapshot < std::max(s->opts.snapshot_interval, 1u))
      return;
    s->last_snapshot = s->tick;
    s->snapshot_bytes = 0;
    if (s->clients.empty())
      return;
    const world_state now = capture(ctx, *s);
    for (auto &[id, c] : s->clients)
      send_snapshot(ctx, *s, c, now);
  } else if (s->synced && s->connected) {
    net_peer_send(ctx, s->server, encode_inputs(*s), net_delivery::unreliable, ch_state);
  }
  net_host_flush(ctx, s->host);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_pre_update, netcode_pre_update, "netcode_receive");
  ecs_register(ctx, phase_fixed_update, netcode_fixed, "netcode_tick");
  ecs_register(ctx, phase_post_update, netcode_send_all_state, "netcode_send");
}

bool start(context &ctx, netcode_role role, const netcode_opts &opts, u16 port) {
  netcode_state &st = state_of(ctx);
  if (st.s) {
    NJIN_WARN("netcode: a session is already running; netcode_stop() it first");
    return false;
  }
  auto s = std::make_unique<session>();
  s->role = role;
  s->opts = opts;
  s->opts.input_redundancy = std::clamp(opts.input_redundancy, 1u, 32u);
  net_host_opts ho;
  ho.port = port;
  ho.max_peers = role == netcode_role::server ? std::max(opts.max_clients, 1u) + 1 : 1;
  ho.channels = channel_count;
  ho.timeout = opts.timeout;
  ho.sim = opts.sim;
  s->host = net_host_create(ctx, ho);
  if (s->host.id == 0)
    return false;
  st.s = std::move(s);
  return true;
}
} // namespace

mod_desc netcode_module() { return mod_desc{.name = "njin.netcode", .setup = setup}; }

void netcode_register_component(context &ctx, netcode_component_erased desc) {
  netcode_state &st = state_of(ctx);
  if (st.s) {
    NJIN_WARN("netcode_component: register components before netcode_host() or netcode_join()");
    return;
  }
  if (st.comps.size() >= max_components) {
    NJIN_WARN("netcode_component: at most %u component types", max_components);
    return;
  }
  st.comps.push_back(std::move(desc));
}

bool netcode_host(context &ctx, u16 port, const netcode_opts &opts) {
  return start(ctx, netcode_role::server, opts, port);
}

bool netcode_join(context &ctx, const char *address, u16 port, const netcode_opts &opts) {
  if (!start(ctx, netcode_role::client, opts, 0))
    return false;
  session &s = *session_of(ctx);
  s.server = net_host_connect(ctx, s.host, address, port, opts.version);
  if (s.server.id == 0) {
    netcode_stop(ctx);
    return false;
  }
  return true;
}

void netcode_stop(context &ctx) {
  session *s = session_of(ctx);
  if (!s)
    return;
  if (s->role == netcode_role::client) {
    std::vector<u32> ids;
    for (const auto &[id, le] : s->entities)
      ids.push_back(id);
    entt::registry &reg = world(ctx);
    for (const u32 id : ids)
      if (reg.valid(s->entities[id].entity))
        reg.destroy(s->entities[id].entity);
    time_set_fixed_rate(ctx, 1.0f);
  }
  net_host_destroy(ctx, s->host);
  ctx_ref<network_store>(ctx)->netcode->s.reset();
}

netcode_role netcode_role_of(const context &ctx) {
  const session *s = session_of(ctx);
  return s ? s->role : netcode_role::none;
}

u32 netcode_tick(const context &ctx) {
  const session *s = session_of(ctx);
  return s ? s->tick : 0;
}

bool netcode_synced(const context &ctx) {
  const session *s = session_of(ctx);
  return s && (s->role == netcode_role::server || s->synced);
}

u32 netcode_self(const context &ctx) {
  const session *s = session_of(ctx);
  return s ? s->self : 0;
}

void netcode_set_input(context &ctx, std::string_view input) {
  session *s = session_of(ctx);
  if (!s)
    return;
  s->local_inputs[s->tick + s->opts.input_delay] = std::string(input.substr(0, max_input_bytes));
}

bool netcode_input(context &ctx, u32 player, std::string &out) {
  session *s = session_of(ctx);
  if (!s) {
    out.clear();
    return false;
  }
  return read_input(*s, player, out);
}

u32 netcode_replicate(context &ctx, entt::entity entity, u32 owner) {
  session *s = session_of(ctx);
  if (!s || s->role != netcode_role::server || !world(ctx).valid(entity))
    return 0;
  const auto it = s->net_of.find(entity);
  if (it != s->net_of.end()) {
    it->second.second = owner;
    return it->second.first;
  }
  const u32 id = s->next_net_id++;
  s->net_of[entity] = {id, owner};
  s->replicated[id] = entity;
  return id;
}

void netcode_unreplicate(context &ctx, entt::entity entity) {
  session *s = session_of(ctx);
  if (!s || s->role != netcode_role::server)
    return;
  const auto it = s->net_of.find(entity);
  if (it == s->net_of.end())
    return;
  s->replicated.erase(it->second.first);
  s->net_of.erase(it);
}

u32 netcode_id(const context &ctx, entt::entity entity) {
  const session *s = session_of(ctx);
  if (!s)
    return 0;
  if (s->role == netcode_role::server) {
    const auto it = s->net_of.find(entity);
    return it == s->net_of.end() ? 0 : it->second.first;
  }
  const auto it = s->id_of_entity.find(entity);
  return it == s->id_of_entity.end() ? 0 : it->second;
}

entt::entity netcode_entity(const context &ctx, u32 net_id) {
  const session *s = session_of(ctx);
  if (!s)
    return entt::null;
  if (s->role == netcode_role::server) {
    const auto it = s->replicated.find(net_id);
    return it == s->replicated.end() ? entt::null : it->second;
  }
  const auto it = s->entities.find(net_id);
  return it == s->entities.end() ? entt::null : it->second.entity;
}

u32 netcode_owner(const context &ctx, entt::entity entity) {
  const session *s = session_of(ctx);
  if (!s)
    return 0;
  if (s->role == netcode_role::server) {
    const auto it = s->net_of.find(entity);
    return it == s->net_of.end() ? 0 : it->second.second;
  }
  const auto it = s->id_of_entity.find(entity);
  return it == s->id_of_entity.end() ? 0 : s->entities.at(it->second).owner;
}

bool netcode_is_mine(const context &ctx, entt::entity entity) {
  const session *s = session_of(ctx);
  const u32 owner = netcode_owner(ctx, entity);
  return s && owner != 0 && owner == s->self;
}

void netcode_set_relevance(context &ctx, std::function<bool(context &, u32, entt::entity)> fn) {
  state_of(ctx).relevance = std::move(fn);
}

void netcode_set_priority(context &ctx, std::function<f32(context &, u32, entt::entity)> fn) {
  state_of(ctx).priority = std::move(fn);
}

bool netcode_send(context &ctx, u32 player, std::string_view data, net_delivery delivery) {
  session *s = session_of(ctx);
  if (!s)
    return false;
  const u8 channel = delivery == net_delivery::reliable ? ch_game : ch_game_fast;
  if (s->role == netcode_role::client)
    return net_peer_send(ctx, s->server, data, delivery, channel);
  const auto it = s->clients.find(player);
  return it != s->clients.end() && net_peer_send(ctx, it->second.peer, data, delivery, channel);
}

void netcode_send_all(context &ctx, std::string_view data, net_delivery delivery) {
  session *s = session_of(ctx);
  if (!s || s->role != netcode_role::server)
    return;
  for (auto &[id, c] : s->clients)
    net_peer_send(ctx, c.peer, data, delivery, delivery == net_delivery::reliable ? ch_game : ch_game_fast);
}

bool netcode_poll(context &ctx, netcode_event &out) {
  session *s = session_of(ctx);
  if (!s || s->events.empty())
    return false;
  out = std::move(s->events.front());
  s->events.pop_front();
  return true;
}

netcode_stats netcode_info(const context &ctx) {
  const session *s = session_of(ctx);
  if (!s)
    return {};
  netcode_stats out;
  out.tick = s->tick;
  out.render_tick = s->render_tick;
  out.input_margin = s->margin;
  out.fixed_rate = ctx_ref<time_state>(ctx).fixed_rate;
  out.missing_inputs = s->missing_inputs;
  out.snapshot_bytes = s->snapshot_bytes;
  out.entities = (u32)(s->role == netcode_role::server ? s->replicated.size() : s->entities.size());
  if (s->role == netcode_role::client)
    out.rtt = net_peer_info(ctx, s->server).rtt;
  return out;
}

bool netcode_confirmed_bytes(const context &ctx, entt::entity entity, u32 type, std::string &out, u32 &tick) {
  const session *s = session_of(ctx);
  if (!s || s->role != netcode_role::client)
    return false;
  const auto it = s->id_of_entity.find(entity);
  if (it == s->id_of_entity.end())
    return false;
  const local_entity &le = s->entities.at(it->second);
  const auto &comps = ctx_ref<network_store>(ctx)->netcode->comps;
  for (usize i = 0; i < comps.size(); ++i)
    if (comps[i].type == type && !le.confirmed[i].second.empty()) {
      tick = le.confirmed[i].first;
      out = le.confirmed[i].second;
      return true;
    }
  return false;
}
} // namespace njin
