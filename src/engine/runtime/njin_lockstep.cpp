#include "njin_lockstep_impl.h"
#include "njin_bits.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_lockstep.h"
#include "njin_log.h"
#include "njin_network_impl.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>

namespace njin {
namespace {
// Channels of the session's host.
constexpr u8 ch_control = 0; // reliable: the room, commands, closed ticks, hashes
constexpr u8 ch_say = 1;     // lockstep_say(), as the game asks
constexpr u8 ch_ack = 2;     // unreliable: a guest's tick, each frame
constexpr u32 channel_count = 3;

enum msg : u8 {
  msg_welcome = 1, // host -> guest: its id and who is there
  msg_joined,      // host -> guests: someone came in
  msg_left,        // host -> guests: someone went
  msg_start,       // host -> guests: the setup
  msg_ticks,       // host -> guests: ticks closed, with their commands
  msg_command,     // guest -> host: a command to be ordered
  msg_ready,       // guest -> host: its world is made, it steps
  msg_ack,         // guest -> host: the tick it has run
  msg_hash,        // guest -> host: its hash at a tick
  msg_desync,      // host -> guests: a player's hash differed
  msg_say,         // either way: outside the simulation
};

constexpr usize max_setup = 64u << 20;
constexpr u32 max_packet = 80u << 20; // the setup in one packet
constexpr usize hashes_kept = 64;     // per player, waiting for the other side's

struct guest {
  net_peer peer;
  u32 id = 0;
  bool ready = false;
  bool desynced = false;
  u32 acked = 0;
  std::map<u32, u64> hashes; // by tick, not yet compared
};

struct session {
  lockstep_role role = lockstep_role::none;
  lockstep_opts opts;
  net_host host;
  net_peer server; // guest: the host
  u32 self = 0;
  bool welcomed = false;
  bool started = false;
  bool ended = false;
  std::vector<u32> players;
  std::deque<lockstep_event> events;
  u32 tick = 0;
  std::vector<lockstep_command> current;
  u32 stalls = 0;

  // host
  std::vector<guest> guests;
  u32 next_id = 1;
  std::map<u32, std::vector<lockstep_command>> scheduled; // by tick
  std::vector<std::pair<u32, std::vector<lockstep_command>>> outgoing; // ticks run, not yet sent
  std::map<u32, u64> own_hashes;

  // guest
  std::map<u32, std::vector<lockstep_command>> closed; // ticks the host closed, not yet run (empty ones left out)
  u32 closed_through = 0;
  bool said_ready = false;
};
} // namespace

struct lockstep_state {
  std::unique_ptr<session> s;
};

void lockstep_state_destroy(lockstep_state *state) { delete state; }

namespace {
session *session_of(context &ctx) {
  lockstep_state *st = ctx_ref<network_store>(ctx)->lockstep;
  return st && st->s ? st->s.get() : nullptr;
}

const session *session_of(const context &ctx) { return session_of(const_cast<context &>(ctx)); }

void push_event(session &s, lockstep_event_kind kind, u32 player, u32 code = 0, u32 tick = 0, std::string data = {}) {
  lockstep_event ev;
  ev.kind = kind;
  ev.player = player;
  ev.code = code;
  ev.tick = tick;
  ev.data = std::move(data);
  s.events.push_back(std::move(ev));
}

void add_player(session &s, u32 id) {
  if (std::find(s.players.begin(), s.players.end(), id) == s.players.end()) {
    s.players.push_back(id);
    std::sort(s.players.begin(), s.players.end());
  }
}

void remove_player(session &s, u32 id) { std::erase(s.players, id); }

void write_blob(bit_writer &w, std::string_view data) {
  w.write_varint(data.size());
  w.write_bytes(data);
}

std::string read_blob(bit_reader &r, usize max) {
  const u64 len = r.read_varint();
  if (len > max) {
    r.read_bytes(~usize{0} >> 4); // fails the reader
    return {};
  }
  return r.read_bytes((usize)len);
}

guest *guest_of(session &s, net_peer peer) {
  for (guest &g : s.guests)
    if (g.peer.id == peer.id)
      return &g;
  return nullptr;
}

void broadcast(context &ctx, session &s, const bit_writer &w, u32 except = 0,
               net_delivery delivery = net_delivery::reliable, u8 channel = ch_control) {
  for (const guest &g : s.guests)
    if (g.id != except)
      net_peer_send(ctx, g.peer, w.data(), delivery, channel);
}

// A hash differed: everyone told, once per player.
void desync(context &ctx, session &s, guest &g, u32 tick) {
  if (g.desynced)
    return;
  g.desynced = true;
  NJIN_WARN("lockstep: player %u's state differs from the host's at tick %u", g.id, tick);
  push_event(s, lockstep_event_kind::desync, g.id, 0, tick);
  bit_writer w;
  w.write_u8(msg_desync);
  w.write_varint(g.id);
  w.write_varint(tick);
  broadcast(ctx, s, w);
}

void compare_hashes(context &ctx, session &s, guest &g) {
  for (auto it = g.hashes.begin(); it != g.hashes.end();) {
    const auto own = s.own_hashes.find(it->first);
    if (own == s.own_hashes.end()) {
      ++it;
      continue;
    }
    if (own->second != it->second)
      desync(ctx, s, g, it->first);
    it = g.hashes.erase(it);
  }
  while (g.hashes.size() > hashes_kept)
    g.hashes.erase(g.hashes.begin());
}

// Host: a guest's packet.
void host_packet(context &ctx, session &s, guest &g, u8 channel, std::string_view data) {
  bit_reader r(data);
  const u8 type = r.read_u8();
  if (type == msg_command && channel == ch_control) {
    std::string cmd = read_blob(r, s.opts.max_command);
    if (r.ok() && s.started && !s.ended)
      s.scheduled[s.tick + s.opts.delay].push_back({g.id, std::move(cmd)});
  } else if (type == msg_ready) {
    g.ready = true;
  } else if (type == msg_ack) {
    const u32 t = (u32)r.read_varint();
    if (r.ok())
      g.acked = std::max(g.acked, t);
  } else if (type == msg_hash) {
    const u32 t = (u32)r.read_varint();
    const u64 h = r.read_u64();
    if (r.ok()) {
      g.hashes[t] = h;
      compare_hashes(ctx, s, g);
    }
  } else if (type == msg_say) {
    std::string said = read_blob(r, max_packet);
    if (!r.ok())
      return;
    bit_writer w;
    w.write_u8(msg_say);
    w.write_varint(g.id);
    write_blob(w, said);
    for (const guest &o : s.guests)
      if (o.id != g.id)
        net_peer_send(ctx, o.peer, w.data(), channel == ch_say ? net_delivery::reliable : net_delivery::unreliable,
                      ch_say);
    push_event(s, lockstep_event_kind::message, g.id, 0, 0, std::move(said));
  }
}

void host_event(context &ctx, session &s, net_host_event &ev) {
  if (ev.kind == net_host_event_kind::connected) {
    u32 refuse = 0;
    if (ev.code != s.opts.version)
      refuse = lockstep_code_version;
    else if (s.started)
      refuse = lockstep_code_started;
    else if (s.guests.size() + 1 >= s.opts.max_players)
      refuse = lockstep_code_full;
    if (refuse) {
      net_peer_disconnect(ctx, ev.peer, refuse);
      return;
    }
    guest g;
    g.peer = ev.peer;
    g.id = s.next_id++;
    bit_writer w;
    w.write_u8(msg_welcome);
    w.write_varint(g.id);
    w.write_varint(s.players.size());
    for (const u32 id : s.players)
      w.write_varint(id);
    net_peer_send(ctx, g.peer, w.data(), net_delivery::reliable, ch_control);
    bit_writer j;
    j.write_u8(msg_joined);
    j.write_varint(g.id);
    broadcast(ctx, s, j);
    s.guests.push_back(g);
    add_player(s, g.id);
    push_event(s, lockstep_event_kind::joined, g.id);
  } else if (ev.kind == net_host_event_kind::disconnected) {
    guest *g = guest_of(s, ev.peer);
    if (!g)
      return;
    const u32 id = g->id;
    std::erase_if(s.guests, [&](const guest &o) { return o.id == id; });
    remove_player(s, id);
    bit_writer w;
    w.write_u8(msg_left);
    w.write_varint(id);
    w.write_varint(ev.code);
    broadcast(ctx, s, w);
    push_event(s, lockstep_event_kind::left, id, ev.code);
  } else if (guest *g = guest_of(s, ev.peer)) {
    host_packet(ctx, s, *g, ev.channel, ev.data);
  }
}

// Guest: the host's packet.
void guest_packet(session &s, std::string_view data) {
  bit_reader r(data);
  const u8 type = r.read_u8();
  if (type == msg_welcome) {
    const u32 id = (u32)r.read_varint();
    const u64 n = r.read_varint();
    std::vector<u32> there;
    for (u64 i = 0; i < n && i < 4096 && r.ok(); ++i)
      there.push_back((u32)r.read_varint());
    if (!r.ok() || s.welcomed)
      return;
    s.welcomed = true;
    s.self = id;
    add_player(s, id);
    push_event(s, lockstep_event_kind::joined, id);
    for (const u32 p : there) {
      add_player(s, p);
      push_event(s, lockstep_event_kind::joined, p);
    }
  } else if (type == msg_joined) {
    const u32 id = (u32)r.read_varint();
    if (r.ok()) {
      add_player(s, id);
      push_event(s, lockstep_event_kind::joined, id);
    }
  } else if (type == msg_left) {
    const u32 id = (u32)r.read_varint();
    const u32 code = (u32)r.read_varint();
    if (r.ok()) {
      remove_player(s, id);
      push_event(s, lockstep_event_kind::left, id, code);
    }
  } else if (type == msg_start) {
    std::string setup = read_blob(r, max_setup);
    if (r.ok() && !s.started) {
      s.started = true;
      push_event(s, lockstep_event_kind::started, 0, 0, 0, std::move(setup));
    }
  } else if (type == msg_ticks) {
    const u32 first = (u32)r.read_varint();
    const u64 count = r.read_varint();
    for (u64 i = 0; i < count && r.ok(); ++i) {
      const u32 t = first + (u32)i;
      const u64 n = r.read_varint();
      std::vector<lockstep_command> cmds;
      for (u64 k = 0; k < n && r.ok(); ++k) {
        lockstep_command c;
        c.player = (u32)r.read_varint();
        c.data = read_blob(r, s.opts.max_command);
        cmds.push_back(std::move(c));
      }
      if (!r.ok() || t <= s.closed_through)
        continue; // reliable and in order: not expected, but harmless
      if (!cmds.empty())
        s.closed[t] = std::move(cmds);
      s.closed_through = t;
    }
  } else if (type == msg_desync) {
    const u32 id = (u32)r.read_varint();
    const u32 t = (u32)r.read_varint();
    if (r.ok())
      push_event(s, lockstep_event_kind::desync, id, 0, t);
  } else if (type == msg_say) {
    const u32 from = (u32)r.read_varint();
    std::string said = read_blob(r, max_packet);
    if (r.ok())
      push_event(s, lockstep_event_kind::message, from, 0, 0, std::move(said));
  }
}

void guest_event(session &s, net_host_event &ev) {
  if (ev.kind == net_host_event_kind::disconnected) {
    if (s.ended)
      return;
    s.ended = true;
    remove_player(s, 0);
    push_event(s, lockstep_event_kind::left, 0, ev.code);
  } else if (ev.kind == net_host_event_kind::packet) {
    guest_packet(s, ev.data);
  }
}

void pump(context &ctx, session &s) {
  if (s.host.id == 0)
    return;
  net_host_event ev;
  while (net_host_poll(ctx, s.host, ev)) {
    if (s.role == lockstep_role::host)
      host_event(ctx, s, ev);
    else
      guest_event(s, ev);
  }
}

// A guest keeps about `buffer` closed ticks in hand: faster when it has more,
// slower when fewer.
void keep_pace(context &ctx, const session &s) {
  if (s.role != lockstep_role::guest || !s.started || s.ended || !s.said_ready)
    return;
  const i32 error = (i32)(s.closed_through - std::min(s.closed_through, s.tick)) - (i32)s.opts.buffer;
  const f32 rate = std::abs(error) <= 1 ? 1.0f : std::clamp(1.0f + 0.05f * (f32)error, 0.5f, 4.0f);
  time_set_fixed_rate(ctx, rate);
}

void lockstep_pre_update(context &ctx) {
  session *s = session_of(ctx);
  if (!s)
    return;
  pump(ctx, *s);
  keep_pace(ctx, *s);
}

void lockstep_send_all(context &ctx) {
  session *s = session_of(ctx);
  if (!s || s->host.id == 0)
    return;
  if (s->role == lockstep_role::host && !s->outgoing.empty()) {
    bit_writer w;
    w.write_u8(msg_ticks);
    w.write_varint(s->outgoing.front().first);
    w.write_varint(s->outgoing.size());
    for (const auto &[t, cmds] : s->outgoing) {
      w.write_varint(cmds.size());
      for (const lockstep_command &c : cmds) {
        w.write_varint(c.player);
        write_blob(w, c.data);
      }
    }
    broadcast(ctx, *s, w);
    s->outgoing.clear();
  } else if (s->role == lockstep_role::guest && s->welcomed && !s->ended && s->said_ready) {
    bit_writer w;
    w.write_u8(msg_ack);
    w.write_varint(s->tick);
    net_peer_send(ctx, s->server, w.data(), net_delivery::unreliable, ch_ack);
  }
  net_host_flush(ctx, s->host);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_pre_update, lockstep_pre_update, "lockstep_receive");
  ecs_register(ctx, phase_post_update, lockstep_send_all, "lockstep_send");
}

bool start_session(context &ctx, lockstep_role role, const lockstep_opts &opts, u16 port) {
  if (!ctx_ref<network_store>(ctx)->lockstep)
    ctx_ref<network_store>(ctx)->lockstep = new lockstep_state();
  lockstep_state &st = *ctx_ref<network_store>(ctx)->lockstep;
  if (st.s) {
    NJIN_WARN("lockstep: a session is already running; lockstep_stop() it first");
    return false;
  }
  auto s = std::make_unique<session>();
  s->role = role;
  s->opts = opts;
  s->opts.max_players = std::clamp(opts.max_players, 2u, 32u);
  s->opts.delay = std::max(opts.delay, 1u);
  net_host_opts ho;
  ho.port = port;
  ho.max_peers = role == lockstep_role::host ? s->opts.max_players : 1;
  ho.channels = channel_count;
  ho.timeout = opts.timeout;
  ho.max_packet = max_packet;
  ho.sim = opts.sim;
  ho.service_thread = opts.service_thread;
  s->host = net_host_create(ctx, ho);
  if (s->host.id == 0)
    return false;
  if (role == lockstep_role::host) {
    s->welcomed = true;
    s->players = {0};
  }
  st.s = std::move(s);
  return true;
}
} // namespace

mod_desc lockstep_module() { return mod_desc{.name = "njin.lockstep", .setup = setup}; }

bool lockstep_host(context &ctx, u16 port, const lockstep_opts &opts) {
  return start_session(ctx, lockstep_role::host, opts, port);
}

bool lockstep_join(context &ctx, const char *address, u16 port, const lockstep_opts &opts) {
  if (!start_session(ctx, lockstep_role::guest, opts, 0))
    return false;
  session &s = *session_of(ctx);
  s.server = net_host_connect(ctx, s.host, address, port, opts.version);
  if (s.server.id == 0) {
    lockstep_stop(ctx);
    return false;
  }
  return true;
}

void lockstep_stop(context &ctx) {
  session *s = session_of(ctx);
  if (!s)
    return;
  if (s->role == lockstep_role::host)
    for (const guest &g : s->guests)
      net_peer_disconnect(ctx, g.peer, lockstep_code_stopped);
  else if (s->server.id)
    net_peer_disconnect(ctx, s->server, lockstep_code_stopped);
  net_host_flush(ctx, s->host);
  net_host_destroy(ctx, s->host);
  ctx_ref<network_store>(ctx)->lockstep->s.reset();
  time_set_fixed_rate(ctx, 1.0f);
}

lockstep_role lockstep_role_of(const context &ctx) {
  const session *s = session_of(ctx);
  return s ? s->role : lockstep_role::none;
}

u32 lockstep_self(const context &ctx) {
  const session *s = session_of(ctx);
  return s ? s->self : 0;
}

std::vector<u32> lockstep_players(const context &ctx) {
  const session *s = session_of(ctx);
  return s ? s->players : std::vector<u32>{};
}

bool lockstep_start(context &ctx, std::string_view setup) {
  session *s = session_of(ctx);
  if (!s || s->role != lockstep_role::host || s->started)
    return false;
  if (setup.size() > max_setup) {
    NJIN_WARN("lockstep_start: the setup is %zu bytes, at most %zu", setup.size(), max_setup);
    return false;
  }
  s->started = true;
  bit_writer w;
  w.write_u8(msg_start);
  write_blob(w, setup);
  broadcast(ctx, *s, w);
  push_event(*s, lockstep_event_kind::started, 0, 0, 0, std::string(setup));
  return true;
}

bool lockstep_started(const context &ctx) {
  const session *s = session_of(ctx);
  return s && s->started;
}

bool lockstep_poll(context &ctx, lockstep_event &out) {
  session *s = session_of(ctx);
  if (!s)
    return false;
  if (s->events.empty())
    pump(ctx, *s);
  if (s->events.empty())
    return false;
  out = std::move(s->events.front());
  s->events.pop_front();
  return true;
}

bool lockstep_send(context &ctx, std::string_view command) {
  session *s = session_of(ctx);
  if (!s || !s->started || s->ended || command.size() > s->opts.max_command)
    return false;
  if (s->role == lockstep_role::host) {
    s->scheduled[s->tick + s->opts.delay].push_back({0, std::string(command)});
    return true;
  }
  bit_writer w;
  w.write_u8(msg_command);
  write_blob(w, command);
  return net_peer_send(ctx, s->server, w.data(), net_delivery::reliable, ch_control);
}

bool lockstep_step(context &ctx) {
  session *s = session_of(ctx);
  if (!s || !s->started || s->ended)
    return false;
  s->current.clear();
  pump(ctx, *s);
  if (s->role == lockstep_role::host) {
    u32 slowest = s->tick;
    bool waiting = false;
    for (const guest &g : s->guests) {
      waiting = waiting || !g.ready;
      slowest = std::min(slowest, g.acked);
    }
    if (waiting || s->tick - slowest > s->opts.max_lag) {
      ++s->stalls;
      return false;
    }
    ++s->tick;
    const auto it = s->scheduled.find(s->tick);
    if (it != s->scheduled.end()) {
      s->current = std::move(it->second);
      s->scheduled.erase(it);
    }
    if (!s->guests.empty())
      s->outgoing.emplace_back(s->tick, s->current);
    return true;
  }
  if (!s->said_ready) {
    s->said_ready = true;
    bit_writer w;
    w.write_u8(msg_ready);
    net_peer_send(ctx, s->server, w.data(), net_delivery::reliable, ch_control);
    net_host_flush(ctx, s->host);
  }
  if (s->closed_through <= s->tick) {
    ++s->stalls;
    return false;
  }
  ++s->tick;
  const auto it = s->closed.find(s->tick);
  if (it != s->closed.end()) {
    s->current = std::move(it->second);
    s->closed.erase(it);
  }
  return true;
}

const std::vector<lockstep_command> &lockstep_commands(const context &ctx) {
  static const std::vector<lockstep_command> none;
  const session *s = session_of(ctx);
  return s ? s->current : none;
}

u32 lockstep_tick(const context &ctx) {
  const session *s = session_of(ctx);
  return s ? s->tick : 0;
}

bool lockstep_hash_due(const context &ctx) {
  const session *s = session_of(ctx);
  return s && s->started && s->tick > 0 && s->opts.hash_interval > 0 && s->tick % s->opts.hash_interval == 0;
}

void lockstep_hash(context &ctx, u64 hash) {
  session *s = session_of(ctx);
  if (!s || !s->started || s->ended)
    return;
  if (s->role == lockstep_role::host) {
    s->own_hashes[s->tick] = hash;
    while (s->own_hashes.size() > hashes_kept * 4)
      s->own_hashes.erase(s->own_hashes.begin());
    for (guest &g : s->guests)
      compare_hashes(ctx, *s, g);
    return;
  }
  bit_writer w;
  w.write_u8(msg_hash);
  w.write_varint(s->tick);
  w.write_u64(hash);
  net_peer_send(ctx, s->server, w.data(), net_delivery::reliable, ch_control);
}

void lockstep_say(context &ctx, std::string_view data, net_delivery delivery) {
  session *s = session_of(ctx);
  if (!s || s->ended)
    return;
  bit_writer w;
  w.write_u8(msg_say);
  if (s->role == lockstep_role::host) {
    w.write_varint(0);
    write_blob(w, data);
    broadcast(ctx, *s, w, ~0u, delivery, ch_say);
    return;
  }
  write_blob(w, data);
  // The host relays it as it came: reliable on ch_say, otherwise unreliable.
  net_peer_send(ctx, s->server, w.data(), delivery, delivery == net_delivery::reliable ? ch_say : ch_ack);
}

lockstep_stats lockstep_stats_of(const context &ctx) {
  lockstep_stats out;
  const session *s = session_of(ctx);
  if (!s)
    return out;
  out.tick = s->tick;
  out.stalls = s->stalls;
  out.players = (u32)s->players.size();
  if (s->role == lockstep_role::guest) {
    out.ready = s->closed_through > s->tick ? s->closed_through - s->tick : 0;
    if (s->server.id)
      out.rtt = net_peer_info(ctx, s->server).rtt;
  } else {
    for (const guest &g : s->guests) {
      out.lag = std::max(out.lag, s->tick > g.acked ? s->tick - g.acked : 0);
      out.rtt = std::max(out.rtt, net_peer_info(ctx, g.peer).rtt);
    }
  }
  return out;
}

} // namespace njin
