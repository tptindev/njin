#pragma once

#include "_types.h"
#include <string>
#include <vector>

namespace njin {
// Minimal non-blocking TCP for the debug link between a game and the
// inspector. Kept in its own translation unit: winsock and raylib declare
// clashing names and cannot share a file. Nothing here ever blocks.
struct net_socket {
  i64 fd = -1;
  bool valid() const { return fd >= 0; }
};

// Listens on 127.0.0.1 only: the debug link is never reachable from another
// machine. Returns an invalid socket if the port is taken.
net_socket net_listen(u16 port);
// Takes one pending connection, or returns an invalid socket if none.
net_socket net_accept(net_socket listener);

// Starts connecting to host:port without waiting. Poll with net_connect_poll.
net_socket net_connect_start(const char *host, u16 port);
// 1 connected, 0 still connecting, -1 failed (the socket is then closed).
i32 net_connect_poll(net_socket &sock);

void net_close(net_socket &sock);

// A newline-delimited message stream over a socket. Each message is one line
// (compact JSON). Outgoing data queues and drains as the socket accepts it;
// once more than `max_pending` bytes wait, new messages are dropped instead of
// stalling the sender, so a slow reader can never freeze the game.
struct net_link {
  net_socket sock;
  std::string out;
  std::string in;
  usize max_pending = 8u << 20;
  u64 dropped = 0; // messages dropped because `out` was full

  bool connected() const { return sock.valid(); }
  // Queues one message; `line` must not contain a newline.
  bool send(const std::string &line);
  // Sends what the socket accepts and reads what arrived. Returns false when
  // the peer has gone (the link is then closed).
  bool pump();
  // Takes the next complete received line, if any.
  bool next(std::string &line);
  void close();
};
} // namespace njin
