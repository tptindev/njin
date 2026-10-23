# Netcode: tick, input, interpolation, replication {#netcode}

@ref multiplayer carries packets. This page handles the hard part on top, for realtime games where the server
decides everything (shooters, action, multiplayer top-down): input travels tagged with a tick, the two machines
keep the same rhythm, other players are drawn smoothly, and the server's world shows up on the client on its own.
Everything is declared in `njin_netcode.h`. The ideas are drawn from
[Lightyear](https://github.com/cBournhonesque/lightyear) (Bevy) and Glenn Fiedler's articles.

You should know first: @ref multiplayer (net_delivery, `net_sim`), @ref ecs (EnTT), @ref time (`phase_fixed_update`).

## Model

- The **server** simulates the real world in `phase_fixed_update`, each step being a **tick**.
- The **client** sends its input for each tick, gets back a state **snapshot**, and:
  - simulates its own character right away (doesn't wait for the server: press and it moves),
  - draws everything else slightly behind, interpolating between two snapshots for smoothness.

@include netcode.cpp

## Input per tick

In `phase_fixed_update`, the client calls netcode_set_input() every tick; the server reads netcode_input() for
each player for that exact tick. Input should be small (use bit_writer): movement direction, buttons, look angle.

- Each input packet carries the input of the last `input_redundancy` ticks (8 by default). Losing several packets
  in a row still loses no input; input identical to the previous tick costs only 1 bit.
- If a tick's input hasn't arrived yet, netcode_input() returns `false` and the most recent input before it (the
  character keeps going as before). netcode_info() counts these in `missing_inputs`.
- `input_delay` uses input a few ticks late: slightly less responsive, but input arrives in time even on a slow
  network. Good for fighting games, RTS.
- Every machine reads its own input with `player` 0. A server that also plays (listen server) does the same.

## Tick sync

Input for tick T must reach the server **before** the server simulates tick T. So the client runs **ahead** of the
server by about half a round trip plus a safety margin. The server reports back how many ticks early a client's
input arrives, and the client adjusts itself:

- A small drift: runs up to 4% faster or slower (time_set_fixed_rate()) until input arrives exactly `input_margin`
  ticks early (2 by default). The player doesn't notice.
- A big drift (right after joining, after a spike): jumps ticks once.

The server reports the **smallest** margin seen among recent packets, so when the network gets jittery the client
runs further ahead on its own. Every machine must run the same `config::fixed_hz`; the client warns if it differs.

## Interpolation

A component with `lerp` is drawn on the client a bit behind the newest snapshot, interpolated between two
snapshots. The delay picks itself: the widest gap among recently received snapshots (several in a row lost) plus
the jitter in arrival time, measured over the last 2 seconds. It rises gradually as the network worsens (render
time slows by up to 10%, never jumping), and falls slowly as the network improves. `netcode_opts::interp_delay`
fixes it if you want a constant value.

## Replicating components

Register each component type once on **every machine, in the same order**, then the server marks an entity with
netcode_replicate(). The entity shows up on the client (`spawned` event), its components get updated, and
destroying it on the server destroys it on the client too (`despawned`). The client only adds its own things
(sprite, sound) to that entity.

| Field of netcode_component_desc | Meaning |
|---|---|
| `write`, `read` | Writes and reads with bit_writer, at just enough precision |
| `lerp` | If present, another player's entity is interpolated |
| `predicted` | On the machine controlling the entity, the server doesn't overwrite it: the game simulates it itself, comparing against netcode_confirmed() to correct drift |

A snapshot only carries **what changed** compared to the snapshot the client has confirmed receiving: an entity
standing still costs nothing. Each snapshot fits within `snapshot_budget` bytes (1100 by default, just enough for
one UDP packet):

- netcode_set_relevance(): which entities are sent to which players (only what's nearby, in the same room...). A
  player's own entity is always sent.
- netcode_set_priority(): an entity that has changed but hasn't been sent accumulates priority; a snapshot sends
  the highest totals first. Distant things still get updated, just less often, never forgotten entirely.

## Messages and events

netcode_send() sends the game's own messages (chat, a purchase, "ready"); netcode_poll() reads everything that
happens in a session: `joined`, `left` (`code` is netcode_code_version, netcode_code_full, or 0 for lost contact),
`message`, `spawned`, `despawned`. Call it in a loop until it returns `false`, usually in the server's
`phase_fixed_update`.

## Testing

Turn on `netcode_opts::sim` on both machines. The engine's harness runs the server and client as two processes
with 150 ms latency, 60 ms jitter, 20% loss each way: input still arrives in full, tick sync holds a 2-tick margin,
and an interpolated entity drifts from its real path by an average of about 0.05 units (the entity moves 2 units
per tick). netcode_info() gives the tick, margin, rate, snapshot size, ping.

Note: `rtt` from netcode_info() is ENet's real ping, not including the fake latency from `sim`.

## Full example: njin_fps

`njin_fps --host` and `njin_fps --join IP` (see @ref samples) use everything on this page in a first-person
shooter: input is each tick's movement keys, look angle and fire button; the host moves every player with the
exact same movement function as single-player, checks shots against targets, and keeps score; the client moves
itself right away according to its own input.

The client corrects drift by **rewinding**: it keeps the input and state of every tick. When netcode_confirmed()
for position (and fall speed, replicated in a second `predicted` component) at some old tick differs from what it
had guessed, it takes the host's state at that tick and replays the inputs from there to now. Both sides round
position to 1 cm after every step, same as the snapshot, so when nothing unusual happens there's never a correction
to make. At 150 ms latency, 50 ms jitter and 15% loss, the client only has to correct once, right when it joins.

## Not yet there

Engine-driven rollback (the whole world, physics too): for now the game rewinds its own character itself, like
njin_fps. Lag compensation (the server rewinding time to check hits) and predictive spawning on the client haven't
arrived either.
