#pragma once
#include "_types.h"
#include <string>
#include <string_view>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_network
/// @{

/// A TCP connection, created by tcp_connect() or received through net_connected when
/// a server accepts another machine.
///
/// `id == 0` is an invalid handle. A closed connection is also treated as invalid; an
/// id is never reused.
struct net_conn {
  u32 id = 0; ///< 0 means invalid.
};

/// A TCP server that is listening, created by tcp_listen().
///
/// `id == 0` is an invalid handle. A closed server is also treated as invalid.
struct net_server {
  u32 id = 0; ///< 0 means invalid.
};

/// A UDP socket, created by udp_open().
///
/// `id == 0` is an invalid handle. A closed socket is also treated as invalid.
struct net_udp {
  u32 id = 0; ///< 0 means invalid.
};

/// A machine's address: an IP as text (`"192.168.1.5"`, `"::1"`) and a port.
struct net_endpoint {
  std::string host; ///< IP address (udp_send() also accepts a hostname).
  u16 port = 0;     ///< Port.
};

/// @name Events
/// The engine pushes these events into events() in `phase_pre_update`; they fire
/// after `phase_post_update` of the same frame, like every event that is `enqueue`d.
/// @{

/// A TCP connection that has opened: tcp_connect() succeeded, or a server just
/// accepted another machine (in that case `server` is that server).
struct net_connected {
  net_conn conn;       ///< The connection.
  net_server server;   ///< The server that accepted it; `id == 0` if this came from tcp_connect().
  net_endpoint remote; ///< The machine on the other end.
};

/// tcp_connect() failed to connect (wrong hostname, nobody listening, timed out).
/// The handle is already discarded; no need to call tcp_close().
struct net_connect_failed {
  net_conn conn;      ///< The connection.
  std::string reason; ///< Reason, in English, for logging.
};

/// An open connection was closed by the other side or failed. The handle is already
/// discarded. A connection the game itself closes with tcp_close() does not fire this
/// event.
struct net_disconnected {
  net_conn conn;      ///< The connection.
  std::string reason; ///< Reason, in English, for logging.
};

/// A message fully received on a TCP connection: exactly the bytes the other side
/// passed into one tcp_send() call, never cut or glued to another message.
struct net_message {
  net_conn conn;    ///< The connection.
  std::string data; ///< The message's bytes (may be binary).
};

/// A UDP packet that has been received.
struct net_datagram {
  net_udp socket;    ///< The socket that received it.
  net_endpoint from; ///< The machine that sent it; reply with udp_send() to this address.
  std::string data;  ///< The packet's bytes.
};
/// @}

/// How to open a TCP connection or server.
struct tcp_opts {
  /// The largest message accepted, in bytes. If the other side sends a bigger
  /// message the connection is closed (net_disconnected), so a stranger cannot make
  /// the game allocate arbitrarily.
  u32 max_message = 1u << 20;
  /// The maximum bytes allowed to wait to be sent on a connection. Past this,
  /// tcp_send() returns `false` and drops the message, instead of letting memory
  /// grow when the other side reads slowly.
  u32 max_pending = 8u << 20;
  /// Maximum time to wait to connect for tcp_connect(), in seconds.
  f32 connect_timeout = 10.0f;
  /// Send each message right away instead of batching it (disables Nagle's
  /// algorithm). Games need low latency more than large packets, so this defaults
  /// to on.
  bool no_delay = true;
};

/// Connects to a TCP server. The function returns right away; resolving the hostname
/// and connecting run on the engine's network thread, and the result arrives later
/// through net_connected or net_connect_failed.
/// @param ctx Engine context.
/// @param host Hostname or IP (`"localhost"`, `"example.com"`, `"10.0.0.2"`).
/// @param port Port.
/// @param opts Options.
/// @return The connection's handle, or an invalid handle if `host` is empty.
net_conn tcp_connect(context &ctx, const char *host, u16 port, const tcp_opts &opts = {});

/// Sends a message. The message is sent along with its length, so the receiver gets
/// it whole in one net_message. The function doesn't wait: the message is queued and
/// sent on the network thread. Can be called right after tcp_connect(), before the
/// connection has finished opening.
///
/// The other side must also use njin (or the same framing: 4 bytes of length,
/// little-endian, followed by the message's bytes).
/// @param ctx Engine context.
/// @param conn The connection.
/// @param data The bytes to send.
/// @return `false` if the handle is no longer open, the message is bigger than
/// `tcp_opts::max_message`, or the send queue is already full (`tcp_opts::max_pending`).
bool tcp_send(context &ctx, net_conn conn, std::string_view data);

/// Closes a connection. Messages already queued but not yet sent are dropped. Does
/// not fire net_disconnected. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param conn The connection.
void tcp_close(context &ctx, net_conn conn);

/// Whether a connection has finished opening and is not yet closed. This is the state
/// at the moment of the call, so it can become `true` before net_connected arrives
/// (the event arrives a frame later).
/// @param ctx Engine context.
/// @param conn The connection.
/// @return `true` if open.
bool tcp_connected(const context &ctx, net_conn conn);

/// The number of bytes waiting to be sent on a connection: to tell whether the
/// network is keeping up.
/// @param ctx Engine context.
/// @param conn The connection.
/// @return The byte count, 0 if the handle is invalid.
usize tcp_pending(const context &ctx, net_conn conn);

/// Opens a TCP server. Opening the port is done by the time the function returns;
/// each machine that connects arrives later through net_connected (with `server` set
/// to this server).
/// @param ctx Engine context.
/// @param port Port; 0 lets the OS pick a free one (see tcp_server_port()).
/// @param host Address to listen on: `nullptr` is every IPv4 address of the machine,
/// `"127.0.0.1"` is this machine only, `"::"` is IPv6.
/// @param opts Options for the connections the server accepts.
/// @return A handle, or an invalid handle if the port is already taken or the
/// address is wrong (a warning says why).
net_server tcp_listen(context &ctx, u16 port, const char *host = nullptr, const tcp_opts &opts = {});

/// The port a server is listening on.
/// @param ctx Engine context.
/// @param server The server.
/// @return The port, 0 if the handle is invalid.
u16 tcp_server_port(const context &ctx, net_server server);

/// Closes a server: it stops accepting new machines. Connections it already accepted
/// stay open. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param server The server.
void tcp_server_close(context &ctx, net_server server);

/// Opens a UDP socket. Each received packet arrives through net_datagram.
/// @param ctx Engine context.
/// @param port Port; 0 lets the OS pick one (enough for a machine that only sends
/// and waits for a reply).
/// @param host Address to listen on: `nullptr` is every IPv4 address, `"::"` is
/// IPv6.
/// @return A handle, or an invalid handle if the port is already taken or the
/// address is wrong.
net_udp udp_open(context &ctx, u16 port = 0, const char *host = nullptr);

/// Sends a UDP packet. The function doesn't wait. UDP makes no promise that a packet
/// will arrive, arrive in order, or arrive only once; a packet bigger than about
/// 1200 bytes can be dropped along the way.
/// @param ctx Engine context.
/// @param socket The socket.
/// @param to The receiving machine. `host` is an IP or hostname (a hostname is
/// resolved on the network thread).
/// @param data The bytes to send, up to 65507.
/// @return `false` if the handle is invalid or the packet is too big.
bool udp_send(context &ctx, net_udp socket, const net_endpoint &to, std::string_view data);

/// The port of a UDP socket.
/// @param ctx Engine context.
/// @param socket The socket.
/// @return The port, 0 if the handle is invalid.
u16 udp_port(const context &ctx, net_udp socket);

/// Closes a UDP socket. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param socket The socket.
void udp_close(context &ctx, net_udp socket);

/// This machine's own IPv4 addresses, except 127.0.0.1: to show other players what
/// to type in when joining a room ("192.168.1.20"). Asks the OS right away, which can
/// take a few milliseconds: call it once and keep the result, don't call it every
/// frame.
/// @return The addresses; empty if the machine has no network.
std::vector<std::string> net_local_addresses();

/// @}
} // namespace njin
