#pragma once
#include "_types.h"
#include "njin_multiplayer.h"
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_lockstep
/// @{

/// How to run a lockstep session (lockstep_host(), lockstep_join()). Every machine
/// must set the fields marked "the same" identically.
///
/// Lockstep: every machine runs **the same simulation**, tick after tick, and the
/// only thing sent between them is the player's **commands**, never state. Suits
/// games with huge numbers of moving things (RTS, city simulation) where sending
/// state would be too heavy. In exchange, the simulation must be **deterministic**:
/// the same commands at the same tick must produce the same result on every
/// machine.
struct lockstep_opts {
  /// The game's protocol version (the same). A machine with a different number is
  /// rejected (`left` with `code` lockstep_code_version).
  u32 version = 1;
  /// Maximum number of players, including the host, 2..32.
  u32 max_players = 4;
  /// Command delay, in ticks (the same): the host slots a command it receives while
  /// at tick T into tick T + `delay`. Smaller is more responsive; larger means a
  /// guest has to wait less often.
  u32 delay = 2;
  /// A guest keeps about this many closed ticks on hand before running them, so a
  /// small network stutter doesn't stall the game. It runs slightly faster or
  /// slower on its own (time_set_fixed_rate()) to hold this amount.
  u32 buffer = 4;
  /// The host stops and waits when a guest falls more than this many ticks behind it.
  u32 max_lag = 180;
  /// Compares state hashes (lockstep_hash()) every this many ticks (the same). 0 means never compare.
  u32 hash_interval = 60;
  /// The largest command, in bytes.
  u32 max_command = 4096;
  f32 timeout = 10.0f; ///< Same as net_host_opts::timeout.
  net_sim sim;         ///< Simulates a bad network on this machine, same as net_host_opts::sim.
  /// Services the network on a background thread, same as net_host_opts::service_thread:
  /// ping matches the real network instead of adding a frame or two.
  bool service_thread = false;
};

/// `code` for a `left` event when the host rejects because lockstep_opts::version differs.
constexpr u32 lockstep_code_version = 1;
/// `code` for a `left` event when the host is already full.
constexpr u32 lockstep_code_full = 2;
/// `code` for a `left` event when the game has already started: can't join partway through.
constexpr u32 lockstep_code_started = 3;
/// `code` for a `left` event when the host stops the session (lockstep_stop()).
constexpr u32 lockstep_code_stopped = 4;

/// This machine's role.
enum class lockstep_role : u8 {
  none,  ///< Not running any session.
  host,  ///< lockstep_host(): keeps the rhythm, slots everyone's commands into ticks.
  guest, ///< lockstep_join(): runs the ticks the host has closed.
};

/// The kind of a lockstep_event.
enum class lockstep_event_kind : u8 {
  /// A player joined the session. A guest that just joined gets `joined` for itself
  /// (lockstep_self()) and for every player already there, the host is player 0.
  joined,
  /// A player left the session (`code` is the reason, 0 is lost contact). On a
  /// guest, `player` 0 means the host left: the session ends, and lockstep_step()
  /// stops running any more ticks.
  left,
  /// The game starts, tick 0; `data` is whatever the host passed into lockstep_start() (seed, ...).
  started,
  /// `player`'s state hash at tick `tick` differs from the host's: the simulation
  /// has drifted. Each player reports this only once.
  desync,
  /// A lockstep_say() message from `player`, outside the simulation.
  message,
};

/// Something that happened in the session, read with lockstep_poll().
struct lockstep_event {
  lockstep_event_kind kind = lockstep_event_kind::message; ///< Kind.
  u32 player = 0;   ///< The player involved.
  u32 code = 0;     ///< `left`: the reason.
  u32 tick = 0;     ///< `desync`: the tick where it drifted.
  std::string data; ///< `started`, `message`: the bytes.
};

/// A player's command, run at the current tick.
struct lockstep_command {
  u32 player = 0;   ///< Who issued it.
  std::string data; ///< The bytes the game passed into lockstep_send().
};

/// Stats for the session.
struct lockstep_stats {
  u32 tick = 0;      ///< The tick that has run.
  u32 ready = 0;     ///< Guest: number of closed ticks not yet run.
  u32 lag = 0;       ///< Host: how many ticks behind the slowest guest is running.
  u32 stalls = 0;    ///< How many times lockstep_step() has had to wait, since the game started.
  u32 players = 0;   ///< Number of players, including this machine.
  f32 rtt = 0.0f;    ///< Guest: ping to the host, in seconds. Host: the largest ping.
};

/// Opens a room on `port`. This machine is player 0; others join with lockstep_join()
/// until lockstep_start() is called.
/// @param ctx Engine context.
/// @param port UDP port.
/// @param opts How to run it.
/// @return `false` if the port is already taken or a session is already running.
bool lockstep_host(context &ctx, u16 port, const lockstep_opts &opts = {});

/// Joins a room at `address:port`. The result arrives through lockstep_poll():
/// `joined`, or `left`.
/// @param ctx Engine context.
/// @param address The host's IP.
/// @param port Port.
/// @param opts How to run it.
/// @return `false` if the address is wrong or a session is already running.
bool lockstep_join(context &ctx, const char *address, u16 port, const lockstep_opts &opts = {});

/// Stops the session: disconnects every machine, resets the tick rate to 1. This
/// machine's simulation stays intact: the game can keep running solo.
/// @param ctx Engine context.
void lockstep_stop(context &ctx);

/// This machine's role. @param ctx Engine context. @return The role.
lockstep_role lockstep_role_of(const context &ctx);

/// This player's id: 0 on the host, the number the host assigned on a guest (0
/// before joining).
/// @param ctx Engine context. @return The id.
u32 lockstep_self(const context &ctx);

/// Every player currently in the session, including this machine, in ascending order.
/// @param ctx Engine context. @return The ids.
std::vector<u32> lockstep_players(const context &ctx);

/// Host: starts the game. Every machine receives `started` with `setup` (seed, saved
/// game...), then builds its world from it. After this, nobody else can join.
/// @param ctx Engine context.
/// @param setup The bytes for every machine.
/// @return `false` if this isn't the host or it has already started.
bool lockstep_start(context &ctx, std::string_view setup);

/// Whether the game has started yet (`started` has arrived). @param ctx Engine context.
/// @return `true` if it has.
bool lockstep_started(const context &ctx);

/// Reads the next thing that happened in the session, if any. Call it in a loop
/// until it returns `false`.
/// @param ctx Engine context.
/// @param out The thing that happened.
/// @return `true` if there was something.
bool lockstep_poll(context &ctx, lockstep_event &out);

/// Issues a command. Every machine runs it at the same tick, chosen by the host,
/// read through lockstep_commands(). The game must **not** apply the command itself
/// at the moment of the call: apply it when it arrives, like every other machine
/// does.
/// @param ctx Engine context.
/// @param command The bytes, up to lockstep_opts::max_command.
/// @return `false` if the game hasn't started, the session has ended, or the
/// command is too big.
bool lockstep_send(context &ctx, std::string_view command);

/// Call it at the start of every `phase_fixed_update`, before simulating. `true`:
/// run a tick right now, after applying lockstep_commands(). `false`: not yet (the
/// next tick's commands haven't arrived, or a guest has fallen too far behind): skip
/// this step, the simulation stands still.
///
/// The host only runs tick 1 once every guest has called this function for the
/// first time, meaning it has finished building its world. Outside a session, or
/// before the game has started, it returns `false`.
/// @param ctx Engine context.
/// @return `true` if a tick ran.
bool lockstep_step(context &ctx);

/// The commands for the tick lockstep_step() just ran, in the exact order on every
/// machine.
/// @param ctx Engine context.
/// @return The commands; empty if nobody issued one this tick.
const std::vector<lockstep_command> &lockstep_commands(const context &ctx);

/// The tick currently running: 1 at the first tick, incrementing by one each time
/// lockstep_step() returns `true`.
/// @param ctx Engine context. @return The tick.
u32 lockstep_tick(const context &ctx);

/// Whether this tick should compare state hashes (a multiple of lockstep_opts::hash_interval).
/// @param ctx Engine context. @return `true` if it should.
bool lockstep_hash_due(const context &ctx);

/// Reports the state hash of the tick that just ran, after that tick's simulation,
/// when lockstep_hash_due(). The host compares every machine's hash; if they
/// differ, every machine gets `desync`. Use lockstep_hasher to build the hash.
/// @param ctx Engine context.
/// @param hash The hash.
void lockstep_hash(context &ctx, u64 hash);

/// Sends a message to every other player, **outside the simulation**: cursor, where
/// someone is looking, chat. Arrives through the `message` event, not tied to any
/// tick.
/// @param ctx Engine context.
/// @param data The bytes.
/// @param delivery Delivery mode.
void lockstep_say(context &ctx, std::string_view data, net_delivery delivery = net_delivery::unreliable);

/// Stats for the session. @param ctx Engine context. @return The stats.
lockstep_stats lockstep_stats_of(const context &ctx);

/// Builds a state hash for lockstep_hash() (64-bit FNV-1a): feed it everything the
/// simulation has, in a fixed order. Floating-point numbers are hashed by their
/// exact bits.
struct lockstep_hasher {
  u64 value = 14695981039346656037ull; ///< The hash so far.

  /// Adds bytes. @param data The bytes. @param size Number of bytes.
  void add_bytes(const void *data, usize size) {
    const unsigned char *p = static_cast<const unsigned char *>(data);
    for (usize i = 0; i < size; ++i) {
      value ^= p[i];
      value *= 1099511628211ull;
    }
  }
  /// Adds a plain value (a number, enum, vec2...). For a struct with padding bytes,
  /// add each field separately: padding bytes are undefined. @param v The value.
  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void add(const T &v) {
    add_bytes(&v, sizeof(T));
  }
  /// Adds a string. @param s The string.
  void add(std::string_view s) {
    add(static_cast<u64>(s.size()));
    add_bytes(s.data(), s.size());
  }
};

/// @}
} // namespace njin
