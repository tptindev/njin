// Realtime multiplayer (njin_multiplayer.h) on ENet. Kept apart from the rest
// of the runtime: ENet pulls in winsock, and winsock and raylib declare
// clashing names (CloseWindow, Rectangle, ...) and cannot share a file.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "njin_multiplayer_impl.h"
#include "njin_ctx.h"
#include "njin_log.h"
#include "njin_multiplayer.h"

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <memory>
#include <random>
#include <unordered_map>

namespace njin {
namespace {
// enet_initialize once for every service in the process.
i32 enet_users = 0;

bool enet_start() {
  if (enet_users == 0 && enet_initialize() != 0)
    return false;
  ++enet_users;
  return true;
}

void enet_stop() {
  if (enet_users > 0 && --enet_users == 0)
    enet_deinitialize();
}

// One clock for the process, so net_clock() means the same before and after
// the first host exists.
f64 clock_now() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<f64>(std::chrono::steady_clock::now() - start).count();
}

void set_timeout(ENetPeer *peer, f32 seconds) {
  const enet_uint32 ms = (enet_uint32)(std::max(seconds, 0.1f) * 1000.0f);
  enet_peer_timeout(peer, 0, ms, ms);
}

u32 id_of(const ENetPeer *peer) { return (u32)(uintptr_t)peer->data; }
void set_id(ENetPeer *peer, u32 id) { peer->data = (void *)(uintptr_t)id; }

net_endpoint endpoint_of(const ENetAddress &address) {
  char text[64] = {};
  if (enet_address_get_host_ip(&address, text, sizeof text) != 0)
    text[0] = '\0';
  return {text, address.port};
}

// IP first; a name falls back to a (blocking) lookup.
bool parse_address(const char *text, u16 port, ENetAddress &out) {
  out.port = port;
  return enet_address_set_host_ip(&out, text) == 0 || enet_address_set_host(&out, text) == 0;
}

enet_uint32 flags_of(net_delivery delivery) {
  switch (delivery) {
  case net_delivery::reliable:
    return ENET_PACKET_FLAG_RELIABLE;
  case net_delivery::unordered:
    return ENET_PACKET_FLAG_UNSEQUENCED;
  case net_delivery::unreliable:
    break;
  }
  return 0;
}
} // namespace

struct mp_host {
  u32 id = 0;
  ENetHost *enet = nullptr;
  net_host_opts opts; // `host` is not kept
  std::deque<std::pair<f64, net_host_event>> inbox; // (ready time, event)
  // Ordered events (connects, disconnects, reliable and sequenced packets)
  // keep their order under simulated jitter: none is ready before the last.
  f64 ordered_ready = 0.0;
  std::minstd_rand rng{12345u};
  bool warned_full = false;
  // Bandwidth: ENet's counters are 32-bit, so they are moved out often.
  u64 sent_total = 0, received_total = 0;
  u64 window_sent = 0, window_received = 0;
  f64 window_start = 0.0;
  f32 sent_per_second = 0.0f, received_per_second = 0.0f;
};

struct mp_peer {
  u32 host = 0;
  ENetPeer *peer = nullptr;
  bool connected = false;
};

class mp_service {
public:
  std::unordered_map<u32, std::unique_ptr<mp_host>> hosts;
  std::unordered_map<u32, mp_peer> peers;
  u32 next_id = 1;
  bool enet_ok = enet_start();

  ~mp_service() {
    for (auto &[id, h] : hosts)
      close_host(*h);
    hosts.clear();
    if (enet_ok)
      enet_stop();
  }

  f64 now() const { return clock_now(); }

  mp_host *host(u32 id) {
    const auto it = hosts.find(id);
    return it == hosts.end() ? nullptr : it->second.get();
  }
  const mp_host *host(u32 id) const { return const_cast<mp_service *>(this)->host(id); }

  mp_peer *peer(u32 id) {
    const auto it = peers.find(id);
    return it == peers.end() ? nullptr : &it->second;
  }
  const mp_peer *peer(u32 id) const { return const_cast<mp_service *>(this)->peer(id); }

  void close_host(mp_host &h) {
    for (auto it = peers.begin(); it != peers.end();) {
      if (it->second.host == h.id) {
        set_id(it->second.peer, 0);
        enet_peer_disconnect_now(it->second.peer, 0);
        it = peers.erase(it);
      } else {
        ++it;
      }
    }
    enet_host_destroy(h.enet);
    h.enet = nullptr;
  }

  void take_counters(mp_host &h) {
    h.window_sent += h.enet->totalSentData;
    h.window_received += h.enet->totalReceivedData;
    h.sent_total += h.enet->totalSentData;
    h.received_total += h.enet->totalReceivedData;
    h.enet->totalSentData = 0;
    h.enet->totalReceivedData = 0;
    const f64 t = now();
    const f64 span = t - h.window_start;
    if (span >= 1.0) {
      h.sent_per_second = (f32)(h.window_sent / span);
      h.received_per_second = (f32)(h.window_received / span);
      h.window_sent = h.window_received = 0;
      h.window_start = t;
    }
  }

  void queue(mp_host &h, net_host_event event, bool ordered) {
    const net_sim &sim = h.opts.sim;
    f64 ready = event.time;
    if (sim.latency > 0.0f || sim.jitter > 0.0f) {
      ready += sim.latency + sim.jitter * std::uniform_real_distribution<f64>(0.0, 1.0)(h.rng);
      if (ordered) {
        ready = std::max(ready, h.ordered_ready);
        h.ordered_ready = ready;
      }
    }
    if (h.inbox.size() >= h.opts.max_queued) {
      // The game is not reading: drop the oldest packet, never a connect or a
      // disconnect, which the game needs to keep its peers straight.
      const auto old = std::find_if(h.inbox.begin(), h.inbox.end(),
                                    [](const auto &e) { return e.second.kind == net_host_event_kind::packet; });
      if (old != h.inbox.end())
        h.inbox.erase(old);
      if (!h.warned_full) {
        NJIN_WARN("net_host %u: %u events waiting, dropping the oldest packets; read them with net_host_poll()",
                  h.id, h.opts.max_queued);
        h.warned_full = true;
      }
    }
    h.inbox.emplace_back(ready, std::move(event));
  }

  void handle(mp_host &h, ENetEvent &ev) {
    net_host_event event;
    event.time = now();
    event.code = ev.data;
    switch (ev.type) {
    case ENET_EVENT_TYPE_CONNECT: {
      u32 id = id_of(ev.peer);
      if (id == 0) { // someone connected to us
        id = next_id++;
        set_id(ev.peer, id);
        peers[id] = mp_peer{h.id, ev.peer, true};
      } else if (mp_peer *p = peer(id)) {
        p->connected = true;
      }
      set_timeout(ev.peer, h.opts.timeout);
      event.kind = net_host_event_kind::connected;
      event.peer = {id};
      queue(h, std::move(event), true);
      break;
    }
    case ENET_EVENT_TYPE_DISCONNECT: {
      const u32 id = id_of(ev.peer);
      if (id == 0)
        break; // dropped by the game: no event, as promised
      peers.erase(id);
      set_id(ev.peer, 0);
      event.kind = net_host_event_kind::disconnected;
      event.peer = {id};
      queue(h, std::move(event), true);
      break;
    }
    case ENET_EVENT_TYPE_RECEIVE: {
      const u32 id = id_of(ev.peer);
      const enet_uint32 flags = ev.packet->flags;
      const bool reliable = (flags & ENET_PACKET_FLAG_RELIABLE) != 0;
      const bool lost = !reliable && h.opts.sim.loss > 0.0f &&
                        std::uniform_real_distribution<f32>(0.0f, 1.0f)(h.rng) < h.opts.sim.loss;
      if (id != 0 && ev.packet->dataLength > h.opts.max_packet) {
        // ENet accepts up to twice max_packet (see net_host_create); beyond
        // max_packet the sender is cut off instead of left resending it.
        NJIN_WARN("net_host %u: peer %u sent %zu bytes, over max_packet (%u); disconnecting it", h.id, id,
                  (size_t)ev.packet->dataLength, h.opts.max_packet);
        peers.erase(id);
        set_id(ev.peer, 0);
        enet_peer_disconnect_now(ev.peer, 0);
        event.kind = net_host_event_kind::disconnected;
        event.peer = {id};
        event.code = 0;
        queue(h, std::move(event), true);
      } else if (id != 0 && !lost) {
        event.kind = net_host_event_kind::packet;
        event.peer = {id};
        event.code = 0;
        event.channel = ev.channelID;
        event.data.assign((const char *)ev.packet->data, ev.packet->dataLength);
        queue(h, std::move(event), (flags & ENET_PACKET_FLAG_UNSEQUENCED) == 0);
      }
      enet_packet_destroy(ev.packet);
      break;
    }
    case ENET_EVENT_TYPE_NONE:
      break;
    }
  }

  // Sends what is queued and takes everything that arrived. Never waits.
  void pump(mp_host &h) {
    ENetEvent ev;
    while (enet_host_service(h.enet, &ev, 0) > 0)
      handle(h, ev);
    take_counters(h);
  }

  bool take_ready(mp_host &h, net_host_event &out) {
    if (h.inbox.empty())
      return false;
    const f64 t = now();
    // Without simulation every event is ready and the front is taken; with
    // it, an unordered packet may be ready before older ones.
    for (auto it = h.inbox.begin(); it != h.inbox.end(); ++it)
      if (it->first <= t) {
        out = std::move(it->second);
        h.inbox.erase(it);
        return true;
      }
    return false;
  }
};

mp_service *mp_service_create() { return new mp_service(); }
void mp_service_destroy(mp_service *service) { delete service; }

namespace {
mp_host *host_of(context &ctx, net_host host) {
  mp_service *s = mp_service_of(ctx, false);
  return s ? s->host(host.id) : nullptr;
}

void pump_hosts(context &ctx) {
  if (mp_service *s = mp_service_of(ctx, false))
    for (auto &[id, h] : s->hosts)
      s->pump(*h);
}

void flush_hosts(context &ctx) {
  if (mp_service *s = mp_service_of(ctx, false))
    for (auto &[id, h] : s->hosts)
      enet_host_flush(h->enet);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_pre_update, pump_hosts, "multiplayer_receive");
  ecs_register(ctx, phase_post_update, flush_hosts, "multiplayer_send");
}
} // namespace

mod_desc multiplayer_module() { return mod_desc{.name = "njin.multiplayer", .setup = setup}; }

net_host net_host_create(context &ctx, const net_host_opts &opts) {
  mp_service &s = *mp_service_of(ctx, true);
  if (!s.enet_ok) {
    NJIN_WARN("net_host_create: ENet could not start");
    return {};
  }
  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = opts.port;
  if (opts.host && *opts.host && enet_address_set_host_ip(&address, opts.host) != 0) {
    NJIN_WARN("net_host_create: not an IPv4 address: %s", opts.host);
    return {};
  }
  const u32 peers = std::clamp(opts.max_peers, 1u, (u32)ENET_PROTOCOL_MAXIMUM_PEER_ID);
  const u32 channels = std::clamp(opts.channels, 1u, (u32)ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT);
  ENetHost *enet = enet_host_create(&address, peers, channels, opts.in_bandwidth, opts.out_bandwidth);
  if (!enet) {
    NJIN_WARN("net_host_create: cannot open UDP port %u (taken?)", (unsigned)opts.port);
    return {};
  }
  if (opts.compress)
    enet_host_compress_with_range_coder(enet);
  // Twice max_packet: a packet a little over the limit still reaches handle(),
  // which disconnects its sender. A bigger one ENet drops by itself, and a
  // reliable one then times the sender out.
  const size_t max_packet = std::max<size_t>(opts.max_packet, 1);
  enet->maximumPacketSize = max_packet * 2;
  enet->maximumWaitingData = std::max<size_t>(max_packet * 4, enet->maximumWaitingData);

  auto h = std::make_unique<mp_host>();
  h->id = s.next_id++;
  h->enet = enet;
  h->opts = opts;
  h->opts.host = nullptr;
  h->opts.channels = channels;
  h->opts.max_queued = std::max(opts.max_queued, 1u);
  h->opts.sim.loss = std::clamp(opts.sim.loss, 0.0f, 1.0f);
  h->window_start = s.now();
  const u32 id = h->id;
  s.hosts.emplace(id, std::move(h));
  return {id};
}

void net_host_destroy(context &ctx, net_host host) {
  mp_service *s = mp_service_of(ctx, false);
  mp_host *h = s ? s->host(host.id) : nullptr;
  if (!h)
    return;
  s->close_host(*h);
  s->hosts.erase(host.id);
}

net_peer net_host_connect(context &ctx, net_host host, const char *address, u16 port, u32 code) {
  mp_service *s = mp_service_of(ctx, false);
  mp_host *h = s ? s->host(host.id) : nullptr;
  if (!h)
    return {};
  ENetAddress where{};
  if (!address || !parse_address(address, port, where)) {
    NJIN_WARN("net_host_connect: cannot resolve %s", address ? address : "(null)");
    return {};
  }
  ENetPeer *peer = enet_host_connect(h->enet, &where, h->opts.channels, code);
  if (!peer) {
    NJIN_WARN("net_host_connect: no free peer slot (net_host_opts::max_peers)");
    return {};
  }
  const u32 id = s->next_id++;
  set_id(peer, id);
  set_timeout(peer, h->opts.timeout); // also bounds the wait for an answer
  s->peers[id] = mp_peer{h->id, peer, false};
  return {id};
}

bool net_host_poll(context &ctx, net_host host, net_host_event &out) {
  mp_service *s = mp_service_of(ctx, false);
  mp_host *h = s ? s->host(host.id) : nullptr;
  if (!h)
    return false;
  if (s->take_ready(*h, out))
    return true;
  s->pump(*h);
  return s->take_ready(*h, out);
}

bool net_peer_send(context &ctx, net_peer peer, std::string_view data, net_delivery delivery, u8 channel) {
  mp_service *s = mp_service_of(ctx, false);
  mp_peer *p = s ? s->peer(peer.id) : nullptr;
  const mp_host *h = p ? s->host(p->host) : nullptr;
  if (!h || channel >= h->opts.channels || data.size() > h->opts.max_packet)
    return false;
  ENetPacket *packet = enet_packet_create(data.data(), data.size(), flags_of(delivery));
  if (!packet)
    return false;
  if (enet_peer_send(p->peer, channel, packet) < 0) {
    enet_packet_destroy(packet);
    return false;
  }
  return true;
}

bool net_host_broadcast(context &ctx, net_host host, std::string_view data, net_delivery delivery, u8 channel) {
  mp_host *h = host_of(ctx, host);
  if (!h || channel >= h->opts.channels || data.size() > h->opts.max_packet)
    return false;
  ENetPacket *packet = enet_packet_create(data.data(), data.size(), flags_of(delivery));
  if (!packet)
    return false;
  enet_host_broadcast(h->enet, channel, packet); // frees it when no one takes it
  return true;
}

void net_host_flush(context &ctx, net_host host) {
  if (mp_host *h = host_of(ctx, host))
    enet_host_flush(h->enet);
}

void net_peer_disconnect(context &ctx, net_peer peer, u32 code) {
  mp_service *s = mp_service_of(ctx, false);
  mp_peer *p = s ? s->peer(peer.id) : nullptr;
  if (!p)
    return;
  set_id(p->peer, 0);
  enet_peer_disconnect_later(p->peer, code);
  s->peers.erase(peer.id);
}

bool net_peer_connected(const context &ctx, net_peer peer) {
  const mp_service *s = mp_service_of(ctx);
  const mp_peer *p = s ? s->peer(peer.id) : nullptr;
  return p && p->connected && p->peer->state == ENET_PEER_STATE_CONNECTED;
}

net_peer_stats net_peer_info(const context &ctx, net_peer peer) {
  const mp_service *s = mp_service_of(ctx);
  const mp_peer *p = s ? s->peer(peer.id) : nullptr;
  if (!p)
    return {};
  net_peer_stats out;
  out.rtt = (f32)p->peer->roundTripTime / 1000.0f;
  out.rtt_variance = (f32)p->peer->roundTripTimeVariance / 1000.0f;
  out.packet_loss = (f32)p->peer->packetLoss / (f32)ENET_PEER_PACKET_LOSS_SCALE;
  out.address = endpoint_of(p->peer->address);
  return out;
}

net_host_stats net_host_info(const context &ctx, net_host host) {
  const mp_service *s = mp_service_of(ctx);
  const mp_host *h = s ? s->host(host.id) : nullptr;
  if (!h)
    return {};
  net_host_stats out;
  out.peers = (u32)h->enet->connectedPeers;
  out.sent_per_second = h->sent_per_second;
  out.received_per_second = h->received_per_second;
  out.sent_total = h->sent_total + h->enet->totalSentData;
  out.received_total = h->received_total + h->enet->totalReceivedData;
  return out;
}

u16 net_host_port(const context &ctx, net_host host) {
  const mp_service *s = mp_service_of(ctx);
  const mp_host *h = s ? s->host(host.id) : nullptr;
  return h ? h->enet->address.port : 0;
}

f64 net_clock(const context &) { return clock_now(); }
} // namespace njin
