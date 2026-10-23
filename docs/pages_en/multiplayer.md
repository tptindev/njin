# Realtime multiplayer {#multiplayer}

This page is for games where everyone plays at once, tick by tick: shooters, racing, fighting, action co-op.
Everything is declared in `njin_multiplayer.h` and `njin_bits.h`. Underneath is [ENet](http://enet.bespin.org/)
(MIT) over UDP, inside the engine.

You should know first: @ref network (TCP, UDP, and why the game never waits on the network), @ref time
(`phase_fixed_update`).

## This page or TCP

| Game | Use |
|---|---|
| Turn-based, chat, lobby, lockstep RTS (only sends commands) | `tcp_*` (see @ref network), simple, every message is guaranteed to arrive |
| Realtime: position, input every tick | `net_host_*` on this page |

TCP guarantees order for **every** byte, so one lost packet makes everything after it wait (a stutter). Realtime
games need to mix both kinds: losing a position packet is fine since a newer one is already on its way, but a "hit"
must not be lost. ENet gives you both over the same UDP connection.

## Host, peer, and channel

Each machine creates a **net_host**. The machine acting as server sets `port`; a client leaves `port = 0` and calls
net_host_connect(). Each machine at the other end is a **net_peer**.

@include multiplayer.cpp

Every packet travels on a **channel** (`channel`) with a delivery mode (net_delivery):

| net_delivery | Guarantees | Use for |
|---|---|---|
| `reliable` | Guaranteed to arrive, in order within its channel | Joining a room, hits, picking up items, chat |
| `unreliable` | Can be lost; an old packet arriving after a newer one is dropped | Position, input every tick |
| `unordered` | Can be lost, arrives in any order | Small self-contained events: sounds, effects |

Channels don't wait on each other: a `reliable` packet lost on channel 1 doesn't hold back a position on channel 0.
Set the number of channels with `net_host_opts::channels`, the same on every machine.

## Reading packets right in the tick

The engine receives packets in `phase_pre_update` and sends whatever the game has queued in `phase_post_update`. An
arrived packet is held until the game reads it with net_host_poll(). This function pulls more packets off the
network itself when the queue is empty, so calling it in `phase_fixed_update` lets the simulation see input right in
that tick. After sending input, net_host_flush() pushes it out at once instead of waiting for the end of the frame.
Together these shave off up to a frame of latency at each end.

| Task | Function |
|---|---|
| Create, destroy | net_host_create(), net_host_destroy() |
| Connect, disconnect | net_host_connect(), net_peer_disconnect() |
| Read | net_host_poll(): `connected`, `disconnected`, `packet` |
| Send | net_peer_send(), net_host_broadcast(), net_host_flush() |
| Stats | net_peer_info() (ping, packet loss), net_host_info() (bandwidth), net_host_port(), net_clock() |

The `code` from net_host_connect() reaches the server in the `connected` event: use it to reject a different game
version. The `code` from net_peer_disconnect() reaches the other machine in `disconnected` (for example, a reason
for being kicked); 0 means lost contact, so the game's own reasons should start at 1.

## Small packets: njin_bits.h

Bandwidth is a real limit for networked games: 16 players, 60 ticks a second, 100 bytes per player comes out to
nearly 100 KB/s for each client. bit_writer writes every value in exactly the bits it needs:

| Write | Costs |
|---|---|
| write_bool() | 1 bit |
| write_int(v, 0, 63) | 6 bits |
| write_float(x, -2000, 2000, 0.01f): 1 cm within ±2 km | 19 bits instead of 32 |
| write_vec3(p, -500, 500, 0.01f) | 3 × 17 bits |
| write_angle(a, 10): 0.35° error | 10 bits |
| write_varint(): ids, counts | 1 byte for 0..127 |

bit_reader reads back **in the exact order and with the exact parameters** used to write. A packet from another
machine may be wrong or deliberately malicious, so bit_reader never reads past its data: reading past the end, a
number outside its range, or a string that's too long makes ok() become `false`, and every read after that returns
0. Read everything, check `ok()` once at the end, and drop the whole packet if it failed.

bit_writer is also useful outside networking: replays (recording input every tick), compact binary saves.

## Testing a bad network on one machine

Everything is smooth on a LAN; not so on the Internet. `net_host_opts::sim` delays and drops packets received right
on this machine:

```cpp
njin::net_host_opts opts;
opts.sim = {.latency = 0.1f, .jitter = 0.03f, .loss = 0.05f}; // 100-130 ms, 5% lost
```

Set it on both machines to get round-trip delay. `reliable` packets are never lost (ENet resends them), only
delayed. Keep it on throughout development, not just before release: network code written on a LAN often breaks the
moment there's 100 ms of latency.

## Safety

- `max_packet` (1 MB by default): a machine that sends a bigger packet is disconnected, with a warning. Set the
  same on every machine.
- `max_peers`: the maximum number of machines connected at once.
- `max_queued`: if the game doesn't read net_host_poll(), the oldest packet is dropped (`connected` and
  `disconnected` are never dropped), instead of letting memory grow without bound.
- `timeout`: if nothing is heard for more than this many seconds, the other machine is considered lost; also the
  maximum time to wait while connecting.

ENet doesn't encrypt: anyone on the same network can read the packets. Don't send passwords or payment information
over it.

## Not here

This is the transport layer: packets that get there, get there fast, and stay small. Input per tick, tick sync,
interpolation and component replication live on @ref netcode, built on top of this page; a simulation that runs the
same on every machine, sending only commands, is @ref lockstep. Rewinding time to check hits (lag compensation)
isn't there yet. Playing over the Internet without opening a router port (NAT traversal) isn't there yet either.
ENet only uses IPv4.
