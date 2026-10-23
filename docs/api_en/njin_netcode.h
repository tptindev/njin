#pragma once
#include "_types.h"
#include "njin_bits.h"
#include "njin_multiplayer.h"
#include <entt/entity/registry.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace njin {
struct context;

/// @addtogroup grp_netcode
/// @{

/// How to run a networked session (netcode_host(), netcode_join()). The server and
/// every client must set the fields marked "the same" identically.
struct netcode_opts {
  /// The game's protocol version (the same). A client with a different number is
  /// rejected (`left` with `code` netcode_code_version).
  u32 version = 1;
  /// Maximum number of clients (server).
  u32 max_clients = 16;
  /// Input delay, in ticks: input set at tick T is used at tick T + `input_delay`
  /// (the same). Trades a bit of responsiveness for input arriving at the server in
  /// time even on a slow network.
  u32 input_delay = 0;
  /// Each input packet carries the input of this many of the most recent ticks, so
  /// losing several packets in a row still loses no input. 1..32.
  u32 input_redundancy = 8;
  /// A client's input should reach the server this many ticks early. The client runs
  /// slightly faster or slower on its own (time_set_fixed_rate()) to hold this
  /// margin.
  u32 input_margin = 2;
  /// The server sends a snapshot every this many ticks (2 at 60 Hz is 30 snapshots a
  /// second).
  u32 snapshot_interval = 2;
  /// Budget per snapshot for a client, in bytes. Higher-priority entities are sent
  /// first; an entity that doesn't fit accumulates priority and goes in a later
  /// snapshot. Keep it under about 1200 so a snapshot is one UDP packet.
  u32 snapshot_budget = 1100;
  /// The client draws an interpolated entity this many seconds behind the newest
  /// snapshot. 0 picks it automatically: 2 snapshot intervals plus 30 ms.
  f32 interp_delay = 0.0f;
  f32 timeout = 10.0f; ///< Same as net_host_opts::timeout.
  net_sim sim;         ///< Simulates a bad network on this machine, same as net_host_opts::sim.
};

/// `code` for a `left` event when the server rejects because `netcode_opts::version` differs.
constexpr u32 netcode_code_version = 1;
/// `code` for a `left` event when the server is already full.
constexpr u32 netcode_code_full = 2;

/// This machine's role.
enum class netcode_role : u8 {
  none,   ///< Not running any session.
  server, ///< netcode_host().
  client, ///< netcode_join().
};

/// The kind of a netcode_event.
enum class netcode_event_kind : u8 {
  joined,    ///< Server: a player joined. Client: successfully joined the server (`player` is our own id).
  left,      ///< Server: a player left. Client: lost the server (`code` says why).
  message,   ///< A game message (netcode_send()).
  spawned,   ///< Client: a replicated entity just appeared (`entity`).
  despawned, ///< Client: a replicated entity was just destroyed (`entity` is no longer valid).
};

/// Something that happened in a session, read with netcode_poll().
struct netcode_event {
  netcode_event_kind kind = netcode_event_kind::message; ///< Kind.
  u32 player = 0;      ///< The player involved (id assigned by the server; 0 is the server).
  u32 code = 0;        ///< `left`: the reason (netcode_code_version, netcode_code_full, 0 is lost contact...).
  std::string data;    ///< `message`: the bytes.
  entt::entity entity = entt::null; ///< `spawned`, `despawned`: the entity on this machine.
  u32 net_id = 0;      ///< `spawned`, `despawned`: the entity's shared id across every machine.
};

/// Stats for a session.
struct netcode_stats {
  u32 tick = 0;              ///< The tick being simulated (netcode_tick()).
  f32 render_tick = 0.0f;    ///< Client: the (server) tick currently being drawn for interpolated entities.
  i32 input_margin = 0;      ///< Client: how many ticks early input arrives at the server (negative is late).
  f32 fixed_rate = 1.0f;     ///< Client: the tick rate currently running (time_fixed_rate()).
  u32 missing_inputs = 0;    ///< Server: how many times a client's input didn't arrive in time for its tick.
  u32 snapshot_bytes = 0;    ///< Size of the latest snapshot (server: the largest among its clients).
  u32 entities = 0;          ///< Number of replicated entities on this machine.
  f32 rtt = 0.0f;            ///< Client: ping to the server, in seconds.
};

/// Starts acting as server on `port`. The server simulates the real world; clients
/// send input and get state back. Each machine runs only one session at a time.
/// @param ctx Engine context.
/// @param port UDP port.
/// @param opts How to run it.
/// @return `false` if the port is already taken or a session is already running.
bool netcode_host(context &ctx, u16 port, const netcode_opts &opts = {});

/// Joins a server at `address:port`. The result arrives through netcode_poll():
/// `joined`, or `left`.
/// @param ctx Engine context.
/// @param address The server's IP.
/// @param port Port.
/// @param opts How to run it.
/// @return `false` if the address is wrong or a session is already running.
bool netcode_join(context &ctx, const char *address, u16 port, const netcode_opts &opts = {});

/// Stops the session: disconnects every machine, destroys every replicated entity on
/// the client, and resets the tick rate to 1. Component registrations
/// (netcode_component()) are kept.
/// @param ctx Engine context.
void netcode_stop(context &ctx);

/// This machine's role. @param ctx Engine context. @return The role.
netcode_role netcode_role_of(const context &ctx);

/// The tick being simulated. Increments by one at the start of each
/// `phase_fixed_update`. On a synced client, it runs slightly ahead of the server so
/// input arrives in time.
/// @param ctx Engine context.
/// @return The tick.
u32 netcode_tick(const context &ctx);

/// Whether the client has received a tick from the server yet (before that, no input
/// is sent).
/// @param ctx Engine context.
/// @return `true` on the server, and on a client once synced.
bool netcode_synced(const context &ctx);

/// This player's id: the id the server assigned to this client, 0 on the server.
/// @param ctx Engine context.
/// @return The id.
u32 netcode_self(const context &ctx);

/// Sets this machine's input for the current tick (plus `input_delay`). Call it in
/// `phase_fixed_update`, once per tick. Should be small: use bit_writer.
/// @param ctx Engine context.
/// @param input The bytes, up to 255.
void netcode_set_input(context &ctx, std::string_view input);

/// A player's input for the current tick. The server reads each client's input;
/// every machine reads its own input with `player` 0 (to simulate its own character
/// right away, without waiting on the server).
/// @param ctx Engine context.
/// @param player The player; 0 is this machine.
/// @param out The input. When this tick's input hasn't arrived yet, it's the most
/// recent input before it.
/// @return `true` if this is really this tick's input; `false` if it's an old input
/// repeated, or there's nothing yet (`out` is empty).
bool netcode_input(context &ctx, u32 player, std::string &out);

/// Marks an entity to be replicated to clients (server). Every component it has that
/// was registered with netcode_component() gets sent; adding or removing a component
/// afterward does too. Destroying the entity destroys it on clients as well.
/// @param ctx Engine context.
/// @param entity The entity.
/// @param owner The player controlling it (0 is nobody): on that machine, `predicted`
/// components aren't overwritten, so the game can simulate them itself.
/// @return The shared id across every machine, 0 if this isn't the server.
u32 netcode_replicate(context &ctx, entt::entity entity, u32 owner = 0);

/// Stops replicating an entity (server): clients destroy their copy, the entity on
/// the server stays.
/// @param ctx Engine context.
/// @param entity The entity.
void netcode_unreplicate(context &ctx, entt::entity entity);

/// The shared id of a replicated entity. @param ctx Engine context.
/// @param entity The entity. @return The id, 0 if not replicated.
u32 netcode_id(const context &ctx, entt::entity entity);

/// The entity on this machine with shared id `net_id`. @param ctx Engine context.
/// @param net_id The id. @return The entity, or `entt::null`.
entt::entity netcode_entity(const context &ctx, u32 net_id);

/// The player controlling a replicated entity. @param ctx Engine context.
/// @param entity The entity. @return The player's id, 0 if nobody.
u32 netcode_owner(const context &ctx, entt::entity entity);

/// Whether this machine controls an entity. @param ctx Engine context.
/// @param entity The entity. @return `true` if the owner is netcode_self().
bool netcode_is_mine(const context &ctx, entt::entity entity);

/// Server: a function that decides whether an entity is sent to a player (for
/// example, only what's nearby). A player's own entity is always sent. `nullptr`
/// sends everything.
/// @param ctx Engine context.
/// @param fn `fn(ctx, player, entity)`.
void netcode_set_relevance(context &ctx, std::function<bool(context &, u32, entt::entity)> fn);

/// Server: an entity's priority with a player (1 by default). On each snapshot, an
/// entity that changed but wasn't sent accumulates priority; a snapshot sends the
/// entities with the highest totals first, until `snapshot_budget` runs out. Nearby
/// things should score higher than distant ones.
/// @param ctx Engine context.
/// @param fn `fn(ctx, player, entity)`.
void netcode_set_priority(context &ctx, std::function<f32(context &, u32, entt::entity)> fn);

/// Sends a game message. A client sends to the server (`player` is ignored); the
/// server sends to player `player`.
/// @param ctx Engine context.
/// @param player The recipient (server).
/// @param data The bytes.
/// @param delivery Delivery mode.
/// @return `false` if it couldn't be sent.
bool netcode_send(context &ctx, u32 player, std::string_view data, net_delivery delivery = net_delivery::reliable);

/// Server: sends a message to every player.
/// @param ctx Engine context.
/// @param data The bytes.
/// @param delivery Delivery mode.
void netcode_send_all(context &ctx, std::string_view data, net_delivery delivery = net_delivery::reliable);

/// Reads the next thing that happened in the session. Call it in a loop until it
/// returns `false`.
/// @param ctx Engine context.
/// @param out The thing that happened.
/// @return `true` if there was something.
bool netcode_poll(context &ctx, netcode_event &out);

/// Stats for the session. @param ctx Engine context. @return The stats.
netcode_stats netcode_info(const context &ctx);

/// How to replicate a component type, for netcode_component().
template <class T> struct netcode_component_desc {
  /// Writes the component. Use bit_writer at just enough precision.
  void (*write)(const T &value, bit_writer &w) = nullptr;
  /// Reads back exactly what `write` wrote. `value` is the current value (or T{}).
  void (*read)(T &value, bit_reader &r) = nullptr;
  /// If present: on the client, another player's entity is drawn smoothly by
  /// interpolating between two snapshots (t from 0 to 1) instead of jumping with
  /// each snapshot.
  T (*lerp)(const T &a, const T &b, f32 t) = nullptr;
  /// On the machine controlling the entity, the server doesn't overwrite this
  /// component: the game simulates it itself right away, from input. The
  /// server-confirmed value is read with netcode_confirmed().
  bool predicted = false;
};

/// @cond NJIN_INTERNAL
struct netcode_component_erased {
  u32 type = 0;
  std::function<bool(const entt::registry &, entt::entity, bit_writer &)> write;
  std::function<void(entt::registry &, entt::entity, std::string_view)> read;
  std::function<void(entt::registry &, entt::entity)> remove;
  std::function<void(entt::registry &, entt::entity, std::string_view, std::string_view, f32)> lerp;
  bool predicted = false;
};
void netcode_register_component(context &ctx, netcode_component_erased desc);
bool netcode_confirmed_bytes(const context &ctx, entt::entity entity, u32 type, std::string &out, u32 &tick);
/// @endcond

/// Registers a component type for replication. Call it on **every machine, in the
/// same order**, before netcode_host() or netcode_join() (usually in the module's
/// `setup`). Up to 31 types.
/// @code
/// struct pos { njin::vec2 v; };
/// njin::netcode_component<pos>(ctx, {
///   .write = [](const pos &p, njin::bit_writer &w) { w.write_vec2(p.v, -2000, 2000, 0.01f); },
///   .read = [](pos &p, njin::bit_reader &r) { p.v = r.read_vec2(-2000, 2000, 0.01f); },
///   .lerp = [](const pos &a, const pos &b, njin::f32 t) { return pos{a.v + (b.v - a.v) * t}; },
/// });
/// @endcode
/// @param ctx Engine context.
/// @param desc How to write, read, and interpolate it.
template <class T> void netcode_component(context &ctx, const netcode_component_desc<T> &desc) {
  netcode_component_erased e;
  e.type = entt::type_hash<T>::value();
  e.predicted = desc.predicted;
  const auto write = desc.write;
  const auto read = desc.read;
  e.write = [write](const entt::registry &reg, entt::entity en, bit_writer &w) {
    const T *c = reg.try_get<T>(en);
    if (!c)
      return false;
    write(*c, w);
    return true;
  };
  e.read = [read](entt::registry &reg, entt::entity en, std::string_view bytes) {
    T value{};
    if (const T *c = reg.try_get<T>(en))
      value = *c;
    bit_reader r(bytes);
    read(value, r);
    if (r.ok())
      reg.emplace_or_replace<T>(en, value);
  };
  e.remove = [](entt::registry &reg, entt::entity en) { reg.remove<T>(en); };
  if (const auto lerp = desc.lerp)
    e.lerp = [read, lerp](entt::registry &reg, entt::entity en, std::string_view a, std::string_view b, f32 t) {
      T va{}, vb{};
      bit_reader ra(a), rb(b);
      read(va, ra);
      read(vb, rb);
      if (ra.ok() && rb.ok())
        reg.emplace_or_replace<T>(en, lerp(va, vb, t));
    };
  netcode_register_component(ctx, std::move(e));
}

/// The latest server-confirmed value of a `predicted` component on this machine's
/// entity: compare it against what the game simulated itself to correct drift.
/// @param ctx Engine context.
/// @param entity The entity.
/// @param out The value.
/// @param tick The server tick the value belongs to.
/// @param read The read function, same as `netcode_component_desc::read`.
/// @return `false` if there isn't one yet.
template <class T>
bool netcode_confirmed(const context &ctx, entt::entity entity, T &out, u32 &tick, void (*read)(T &, bit_reader &)) {
  std::string bytes;
  if (!netcode_confirmed_bytes(ctx, entity, entt::type_hash<T>::value(), bytes, tick))
    return false;
  bit_reader r(bytes);
  T value{};
  read(value, r);
  if (!r.ok())
    return false;
  out = value;
  return true;
}

/// @}
} // namespace njin
