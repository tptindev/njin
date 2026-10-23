# Lockstep: the same simulation on every machine {#lockstep}

@ref netcode sends **state**: the server simulates, and the client gets back each thing's position. That suits a
shooter with a few dozen things moving. A game with thousands of moving things (RTS, city simulation, management)
would make sending state too heavy. Lockstep does the opposite: every machine runs **the same simulation**, tick
after tick, and the only thing sent between them is the player's **commands**. Two thousand townsfolk walking
around costs not a single byte on the network. Everything is declared in `njin_lockstep.h`.

You should know first: @ref multiplayer (net_delivery, `net_sim`), @ref time (`phase_fixed_update`), bit_writer.

## Model

- The **host** (lockstep_host()) keeps the rhythm. It slots everyone's commands (including its own) into a tick: a
  command received while it's at tick T runs at tick T + `delay`. Once a tick finishes running, it tells every
  machine: "this tick is closed, here are its commands".
- A **guest** (lockstep_join()) only runs a tick once that tick is closed. It runs slightly faster or slower on its
  own (time_set_fixed_rate()) to always keep about `buffer` closed ticks in hand, so a small network stutter
  doesn't stall the game.
- Every `hash_interval` ticks, every machine produces a **state hash** (lockstep_hash()); the host compares them,
  and if they differ every machine gets a `desync` event. A desync is a bug in the game (see below), not in the
  network: commands travel over a reliable channel and are never lost.

@include lockstep.cpp

## One tick

In `phase_fixed_update`, before simulating:

1. lockstep_step(): `false` means skip this step, the world stands still (commands haven't arrived yet, or a guest
   has fallen more than `max_lag` behind).
2. Apply lockstep_commands() for the tick, in the exact order returned: that order is the same on every machine.
   `player` says who issued it.
3. Run the tick's simulation.
4. When lockstep_hash_due(): call lockstep_hash() with the world's hash, built with lockstep_hasher.

**Never** change the world the instant a player presses a button. A button press calls lockstep_send(); the world
changes when the command comes back, at the tick the host picked, on every machine at once. Single-player should
take the same path too (write the command to bytes, read it straight back and apply it): then the multiplayer path
gets exercised in every single game.

A command that's out but hasn't run yet means the world hasn't changed. If the game picks something based on the
state at the moment of the press (the nearest free worker to assign a job to), remember what's already been
assigned while a command is still in flight, so two commands issued back to back don't pick the same thing.

## Room and start

- The host accepts players until lockstep_start(). After that nobody else can join (`left` with
  lockstep_code_started): joining mid-game would need copying the whole world over, which the game has to do
  itself if it wants that.
- lockstep_start() sends `setup` (seed, map, difficulty...) to every machine in the `started` event. Every machine
  builds its world from it. The host only runs tick 1 once every guest has called lockstep_step() for the first
  time, meaning it has finished building.
- The host leaves (`left` with `player` 0 on a guest): the session ends. The world on a guest stays intact,
  identical to the host's up to the last tick: the game can call lockstep_stop() and keep running solo.
- lockstep_say() sends something **outside the simulation** (mouse cursor, where someone is looking, chat) to
  everyone; it arrives through the `message` event.
- net_local_addresses() (@ref network) gives the machine's IPv4 addresses, to show the other player what to type
  in.

## Keeping the simulation deterministic

Same exe, same seed, same commands at the same tick: the result must match bit for bit. Things that commonly cause
drift:

| Causes drift | Replace with |
|---|---|
| Simulation reads the camera, window size, or whatever is on screen | Simulate the same regardless of what the screen shows |
| `delta()`, real time, frame count | Only `fixed_delta()` and the tick |
| RNG shared with UI or audio | A separate RNG for the simulation; UI has its own |
| RNG not reset when a new game starts | Reset from the seed every time one starts |
| Changing the world in `phase_update`, in drawing code | Only inside the tick, from a command |
| Iterating an `unordered_map` by pointer, reading an uninitialized variable | A fixed order |
| Threads running in parallel whose result depends on order | A result that doesn't depend on thread scheduling |

A state hash should include things that change often (positions, money, health): a drift anywhere then shows up
quickly. lockstep_hasher hashes floating-point numbers by their exact bits; for a struct with padding bytes, add
each field separately.

## Testing

Turn on `lockstep_opts::sim` (latency, jitter, loss) on both machines. The engine's harness runs two processes with
40 ms latency, 30 ms jitter and 10% loss, each side sending random commands a few times a second: by tick 900 both
have the same state after more than 4600 commands; flipping one bit at tick 300 makes both machines get `desync`
at exactly tick 300. lockstep_stats_of() gives the tick, how many closed ticks are in hand, how many times it had
to wait, and ping.

## Full example: Turf

Turf's co-op mode (`njin_turf --host`, `njin_turf --join IP`) runs a city of 2000 townsfolk, vehicles and gangs on
every machine with this page: commands are jobs assigned to your men, the clock's speed, requesting men from the
station; the other player's cursor travels through lockstep_say().

## Not yet there

Joining mid-game (copying the world to a newcomer), resuming a saved game, rewinding on a desync (rollback). The
host is a player itself: if it leaves, the session ends. Playing over the Internet without opening a router port
(NAT traversal) hasn't arrived either.
