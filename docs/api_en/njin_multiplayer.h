#pragma once
#include "_types.h"
#include "njin_network.h"
#include <string>
#include <string_view>

namespace njin {
struct context;

/// @addtogroup grp_multiplayer
/// @{

/// A realtime networking machine, created by net_host_create(): accepts incoming
/// machines (server), connects to another machine (client), or both.
///
/// `id == 0` is an invalid handle. A destroyed host is also treated as invalid.
struct net_host {
  u32 id = 0; ///< 0 means invalid.
};

/// A machine at the other end of a net_host.
///
/// `id == 0` is an invalid handle. A disconnected machine is also treated as
/// invalid; an id is never reused.
struct net_peer {
  u32 id = 0; ///< 0 means invalid.
};

/// How a packet is delivered.
enum class net_delivery : u8 {
  /// Guaranteed to arrive, in order within its channel; resent automatically if
  /// lost. For things that must not be lost: joining a room, hits, picking up
  /// items, chat.
  reliable,
  /// Can be lost; a packet that arrives later than a newer one (same channel) is
  /// dropped. For state resent continuously, where only the latest matters:
  /// position, input every tick.
  unreliable,
  /// Can be lost, arrives in any order, no packet is dropped for arriving late.
  /// For small events that are each self-contained: footsteps, effects.
  unordered,
};

/// Simulates a bad network on this machine itself, to test the game with latency and
/// packet loss when there's only one machine around. Applies to **received
/// packets**: set it on both machines to get round-trip delay. `reliable` packets
/// are never lost (ENet resends them), only delayed.
struct net_sim {
  f32 latency = 0.0f; ///< Extra latency, in seconds.
  f32 jitter = 0.0f;  ///< Extra random latency, 0..jitter seconds. Unreliable packets can arrive out of order.
  f32 loss = 0.0f;    ///< Fraction of unreliable packets dropped, 0..1.
};

/// How to create a host.
struct net_host_opts {
  /// Port to accept incoming machines on. 0 lets the OS pick a free one: enough for
  /// a machine that only connects out (client).
  u16 port = 0;
  /// Address to listen on: `nullptr` is every IPv4 address of the machine,
  /// `"127.0.0.1"` is this machine only. ENet only uses IPv4.
  const char *host = nullptr;
  /// Maximum number of machines connected at once (both incoming and outgoing), 1..4095.
  u32 max_peers = 32;
  /// Number of channels, 1..255. Packets on different channels don't wait on each
  /// other: a `reliable` packet lost on channel 0 doesn't hold back a position sent
  /// on channel 1.
  u32 channels = 2;
  /// Compress packets (ENet's range coder). **Every machine must set this the same.**
  bool compress = false;
  /// The largest packet accepted, in bytes. Bigger than this and the sending machine
  /// is disconnected (`disconnected`, with a warning), so a stranger cannot make the
  /// game allocate arbitrarily. Should be set the same on every machine:
  /// net_peer_send() only checks the sending machine's own limit.
  u32 max_packet = 1u << 20;
  /// Maximum receive and send bandwidth, bytes per second; 0 is unlimited. ENet
  /// paces packets accordingly instead of flooding the link.
  u32 in_bandwidth = 0;
  u32 out_bandwidth = 0; ///< See `in_bandwidth`.
  /// If nothing is heard from a machine for more than this many seconds, it's
  /// considered lost (net_host_event `disconnected`, `code` 0). Also the maximum
  /// time net_host_connect() waits.
  f32 timeout = 10.0f;
  /// Maximum events waiting while the game hasn't read them (net_host_poll()). Past
  /// this, the oldest packet is dropped, with a warning.
  u32 max_queued = 16384;
  net_sim sim; ///< Simulates a bad network; off by default.
  /// Services the host on a background thread about every millisecond, instead of
  /// only once a frame: incoming packets and acknowledgements (ACKs) are received
  /// and answered right away, without waiting for the next frame. Ping
  /// (net_peer_stats::rtt) then stays close to the real network (a few ms on a LAN)
  /// instead of adding a frame or two. Queued packets can also go out before the
  /// end of the frame. Events are still only read with net_host_poll(), on the
  /// game's thread.
  bool service_thread = false;
};

/// The kind of a net_host_event.
enum class net_host_event_kind : u8 {
  connected,    ///< A machine has connected (to us, or we connected to it).
  disconnected, ///< A machine has disconnected, failed to connect, or was lost.
  packet,       ///< A packet has arrived.
};

/// Something that happened on a host, read with net_host_poll().
struct net_host_event {
  net_host_event_kind kind = net_host_event_kind::packet; ///< Kind.
  net_peer peer;                                          ///< The machine involved.
  /// `connected`: the number the other machine passed into net_host_connect().
  /// `disconnected`: the number the other machine passed into net_peer_disconnect();
  /// 0 also means lost contact or a failed connection.
  u32 code = 0;
  u8 channel = 0;   ///< `packet`: the channel.
  std::string data; ///< `packet`: the bytes.
  f64 time = 0.0;   ///< Arrival time, seconds on net_clock().
};

/// Stats for a machine at the other end.
struct net_peer_stats {
  f32 rtt = 0.0f;          ///< Average round-trip time, in seconds (ping).
  f32 rtt_variance = 0.0f; ///< How much rtt varies, in seconds.
  f32 packet_loss = 0.0f;  ///< Fraction of reliable packets that had to be resent, 0..1.
  net_endpoint address;    ///< The address.
};

/// Stats for a host.
struct net_host_stats {
  u32 peers = 0;               ///< Number of machines currently connected.
  f32 sent_per_second = 0.0f;  ///< Bytes sent per second (measured once a second, including ENet's packet headers).
  f32 received_per_second = 0.0f; ///< Bytes received per second.
  u64 sent_total = 0;          ///< Total bytes sent.
  u64 received_total = 0;      ///< Total bytes received.
};

/// Creates a realtime networking host (ENet over UDP).
///
/// The engine services the host every frame: receiving packets in `phase_pre_update`,
/// sending whatever the game has queued in `phase_post_update` (plus more on the
/// background thread if net_host_opts::service_thread is set). An arrived packet is
/// held until the game reads it with net_host_poll().
/// @param ctx Engine context.
/// @param opts How to create it.
/// @return A handle, or an invalid handle if the port is already taken or a
/// parameter is wrong (a warning says why).
net_host net_host_create(context &ctx, const net_host_opts &opts = {});

/// Destroys a host: disconnects every machine at once (without waiting for their
/// acknowledgement) and closes the socket. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param host The host.
void net_host_destroy(context &ctx, net_host host);

/// Connects to another host. The function returns right away; the result arrives
/// through net_host_poll(): `connected`, or `disconnected` if it failed to connect.
///
/// `address` should be an IP (`"192.168.1.20"`). A hostname (`"localhost"`) has to be
/// resolved right away, which can slow down that frame.
/// @param ctx Engine context.
/// @param host Our host.
/// @param address The machine to connect to.
/// @param port Port.
/// @param code A number sent along, which the other side sees in
/// `net_host_event::code` (for example, the game's version).
/// @return A handle for the other machine, or an invalid handle if the address is
/// wrong or there's no room.
net_peer net_host_connect(context &ctx, net_host host, const char *address, u16 port, u32 code = 0);

/// Reads the next thing that happened on a host, if any. Call it in a loop until it
/// returns `false`.
///
/// The function pulls new packets off the network itself when the queue is empty, so
/// it can be called in any phase. Calling it in `phase_fixed_update` lets the
/// simulation see the packet right in that tick, with no extra frame of delay.
/// @param ctx Engine context.
/// @param host The host.
/// @param out The thing that happened.
/// @return `true` if there was something.
bool net_host_poll(context &ctx, net_host host, net_host_event &out);

/// Sends a packet to a machine. The packet is queued and sent at the end of the
/// frame (or right away if net_host_flush() is called).
/// @param ctx Engine context.
/// @param peer The receiving machine.
/// @param data The bytes.
/// @param delivery Delivery mode.
/// @param channel Channel, less than `net_host_opts::channels`.
/// @return `false` if the machine is no longer connected, the channel is wrong, or
/// the packet is too big.
bool net_peer_send(context &ctx, net_peer peer, std::string_view data, net_delivery delivery = net_delivery::reliable,
                   u8 channel = 0);

/// Sends a packet to every machine connected to the host.
/// @param ctx Engine context.
/// @param host The host.
/// @param data The bytes.
/// @param delivery Delivery mode.
/// @param channel Channel.
/// @return `false` if the handle or channel is wrong.
bool net_host_broadcast(context &ctx, net_host host, std::string_view data,
                        net_delivery delivery = net_delivery::reliable, u8 channel = 0);

/// Sends every packet the host has queued right away, instead of waiting for the end
/// of the frame: for example, right after sending input in `phase_fixed_update`.
/// @param ctx Engine context.
/// @param host The host.
void net_host_flush(context &ctx, net_host host);

/// Disconnects a machine. Packets already queued are still sent first; the other
/// machine receives `disconnected` with `code`. The handle is discarded right away;
/// we get no more events for this machine.
/// @param ctx Engine context.
/// @param peer The machine.
/// @param code A number sent along (for example, a reason for being kicked). Should
/// be non-zero: 0 also means lost contact.
void net_peer_disconnect(context &ctx, net_peer peer, u32 code = 0);

/// Whether this machine is still connected.
/// @param ctx Engine context.
/// @param peer The machine.
/// @return `true` if it has finished connecting and hasn't disconnected.
bool net_peer_connected(const context &ctx, net_peer peer);

/// Stats for a machine: ping, packet loss, address.
/// @param ctx Engine context.
/// @param peer The machine.
/// @return The stats; all 0 if the handle is invalid.
net_peer_stats net_peer_info(const context &ctx, net_peer peer);

/// Stats for a host: number of machines, bandwidth.
/// @param ctx Engine context.
/// @param host The host.
/// @return The stats; all 0 if the handle is invalid.
net_host_stats net_host_info(const context &ctx, net_host host);

/// The port the host is using (the one the OS picked when `port` was 0).
/// @param ctx Engine context.
/// @param host The host.
/// @return The port, 0 if the handle is invalid.
u16 net_host_port(const context &ctx, net_host host);

/// The network's clock: real seconds, increasing steadily, unaffected by
/// time_set_scale() and never paused. `net_host_event::time` is measured with this
/// clock.
/// @param ctx Engine context.
/// @return Seconds.
f64 net_clock(const context &ctx);

/// @}
} // namespace njin
