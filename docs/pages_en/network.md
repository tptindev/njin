# TCP and UDP networking {#network}

This page connects several machines together: one machine hosts a room (server), the others join it (client), and
they exchange positions, turns, chat messages back and forth. Everything is declared in `njin_network.h`. Underneath
is [asio](https://think-async.com/Asio/) (the standalone flavor, no Boost needed), inside the engine.

You should know first: @ref ecs (events through `events(ctx)`) and @ref json (packing data to send).

## No function waits on the network

The network is slow and drops out, while the game still has to draw a full 60 frames a second. So every socket runs
on **one network thread** of its own (asio, written with C++20 coroutines). The game never waits:

- tcp_connect(), tcp_send(), udp_send() return right away; the real work happens afterward, on the network thread.
- What happens on the network (connected, a message arrived, dropped) arrives as **events**. The engine pushes them
  into `events(ctx)` in `phase_pre_update`; they fire after `phase_post_update` of the same frame, like every event
  that is `enqueue`d.

The network thread only starts once the game calls its first network function; a game that doesn't use the network
has no such thread. When the context is destroyed, every socket is closed and the thread stops.

| Event | When |
|---|---|
| net_connected | A TCP connection has opened: tcp_connect() succeeded, or a server just accepted a machine (`server` non-zero) |
| net_connect_failed | tcp_connect() failed to connect: wrong hostname, nobody listening, `connect_timeout` elapsed |
| net_disconnected | An open connection was closed by the other side or failed |
| net_message | A whole TCP message has been received |
| net_datagram | A UDP packet has been received |

`reason` on the two failure events is short English text (`"connection refused"`, `"timed out"`, `"closed by peer"`),
meant for logging.

## TCP: whole messages, in order

TCP is guaranteed to arrive, and in order. Use it for anything that must not be lost: joining a room, turns, chat,
purchases.

TCP itself is a byte stream with no boundaries: two sends can stick together into one, and one send can arrive split
into two pieces. njin sends the length of each message along with it (4 bytes, little-endian), so **every
tcp_send() arrives at the other end as exactly one net_message**, never cut or glued to another. So the other side
must also be njin (or read the same framing).

@include network.cpp

| Function | Does |
|---|---|
| tcp_listen() | Opens a server. Returns an invalid handle right away if the port is already taken |
| tcp_connect() | Connects to a server. The hostname is resolved on the network thread |
| tcp_send() | Queues a message to send. Works even before the connection has finished opening |
| tcp_close(), tcp_server_close() | Closes a connection, a server |
| tcp_connected(), tcp_pending() | Whether it's open yet; how many bytes are waiting to be sent |
| tcp_server_port() | The real port of a server opened with port 0 |

A connection the game itself closes with tcp_close() does **not** fire net_disconnected: the game already knows.
Closing a server stops it accepting new machines, but the ones already in stay connected.

### Limits so a stranger can't break the game

`tcp_opts` sets two limits per connection:

- `max_message` (1 MB by default): if the other side sends a bigger message, the connection is closed. Without this
  limit, a machine claiming "this message is 4 GB long" could make the game allocate that much memory.
- `max_pending` (8 MB by default): if the other side reads too slowly and the send queue fills up, tcp_send()
  returns `false` and drops the message, instead of letting memory grow without bound.

A server listens on every address of the machine when `host` is `nullptr`, meaning other machines on the LAN (and
the Internet, if the router forwards the port) can connect. For testing on a single machine, listen on
`"127.0.0.1"`. A received message is data from another machine: check it before trusting it (json_parse() returning
`false`, numbers outside a valid range...).

## UDP: fast, but packets can be lost

UDP sends loose packets with no connection beforehand. A packet **can be lost, arrive out of order, or arrive
twice**, but is never held back by an earlier lost packet. Use it for anything resent continuously, where losing one
packet is fine: a player's position every frame, finding a room on the LAN.

```cpp
const njin::net_udp sock = njin::udp_open(ctx, 7778); // port 0 if you only send and wait for a reply
njin::udp_send(ctx, sock, {"192.168.1.20", 7778}, "ping");

void on_datagram(njin::context &ctx, const njin::net_datagram &e) {
  if (e.data == "ping")
    njin::udp_send(ctx, e.socket, e.from, "pong"); // reply to whoever sent it
}
```

Keep each packet under about 1200 bytes: a bigger one gets fragmented along the way, and losing one fragment loses
the whole packet. udp_send() refuses packets over 65507 bytes. `host` should be an IP address: a hostname gets
re-resolved on every send.

## IPv4 and IPv6

`host` on tcp_listen() and udp_open() defaults to every **IPv4** address. Pass `"::"` to listen on IPv6. An IPv4 UDP
socket can only send to IPv4 addresses; tcp_connect() tries every address of a hostname in turn until one connects.

net_local_addresses() gives this machine's own IPv4 addresses (except 127.0.0.1), to show other players what to
type in when joining a room. It asks the OS right away: call it once and keep the result.

## Not here

This page is the transport layer: getting bytes from one machine to another. Realtime games (a position every tick,
together with not losing anything on one connection) are covered in @ref multiplayer. Anything above that is up to
the game's own genre: state sync, client-side prediction, correcting drift, anti-cheat. There's no HTTP here either
(leaderboards, login).
