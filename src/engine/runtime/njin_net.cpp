// Kept apart from the rest of the runtime: winsock and raylib declare
// clashing names (CloseWindow, Rectangle, ...) and cannot share a file.
#include "njin_net.h"
#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace njin {
namespace {
bool started() {
#if defined(_WIN32)
  static const bool ok = [] {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
#else
  return true;
#endif
}

socket_t raw(net_socket s) { return (socket_t)s.fd; }

bool would_block() {
#if defined(_WIN32)
  const int e = WSAGetLastError();
  return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
  return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
}

void set_nonblocking(socket_t s) {
#if defined(_WIN32)
  u_long on = 1;
  ioctlsocket(s, FIONBIO, &on);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  // Messages are small and latency matters more than packing.
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
}

void close_raw(socket_t s) {
#if defined(_WIN32)
  closesocket(s);
#else
  ::close(s);
#endif
}

#if defined(_WIN32)
constexpr socket_t bad_socket = INVALID_SOCKET;
#else
constexpr socket_t bad_socket = -1;
#endif
} // namespace

net_socket net_listen(u16 port) {
  if (!started())
    return {};
  const socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == bad_socket)
    return {};
#if !defined(_WIN32)
  // Lets a restarted game rebind at once instead of waiting out TIME_WAIT.
  // Not on Windows, where SO_REUSEADDR would let two games share the port.
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, (const sockaddr *)&addr, sizeof addr) != 0 || listen(s, 1) != 0) {
    close_raw(s);
    return {};
  }
  set_nonblocking(s);
  return net_socket{(i64)s};
}

net_socket net_accept(net_socket listener) {
  if (!listener.valid())
    return {};
  const socket_t s = accept(raw(listener), nullptr, nullptr);
  if (s == bad_socket)
    return {};
  set_nonblocking(s);
  return net_socket{(i64)s};
}

net_socket net_connect_start(const char *host, u16 port) {
  if (!started() || host == nullptr)
    return {};
  const socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == bad_socket)
    return {};
  set_nonblocking(s);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    close_raw(s);
    return {};
  }
  if (connect(s, (const sockaddr *)&addr, sizeof addr) != 0 && !would_block()) {
    close_raw(s);
    return {};
  }
  return net_socket{(i64)s};
}

i32 net_connect_poll(net_socket &sock) {
  if (!sock.valid())
    return -1;
  fd_set write_set, error_set;
  FD_ZERO(&write_set);
  FD_ZERO(&error_set);
  FD_SET(raw(sock), &write_set);
  FD_SET(raw(sock), &error_set);
  timeval zero{0, 0};
  const int n = select((int)raw(sock) + 1, nullptr, &write_set, &error_set, &zero);
  if (n == 0)
    return 0;
  int err = 0;
#if defined(_WIN32)
  int len = sizeof err;
#else
  socklen_t len = sizeof err;
#endif
  getsockopt(raw(sock), SOL_SOCKET, SO_ERROR, (char *)&err, &len);
  if (n < 0 || FD_ISSET(raw(sock), &error_set) || err != 0) {
    net_close(sock);
    return -1;
  }
  return 1;
}

void net_close(net_socket &sock) {
  if (sock.valid())
    close_raw(raw(sock));
  sock = {};
}

bool net_link::send(const std::string &line) {
  if (!sock.valid())
    return false;
  if (out.size() + line.size() + 1 > max_pending) {
    dropped++;
    return false;
  }
  out += line;
  out.push_back('\n');
  return true;
}

bool net_link::pump() {
  if (!sock.valid())
    return false;
  while (!out.empty()) {
    const int n = ::send(raw(sock), out.data(), (int)std::min<usize>(out.size(), 1u << 20), 0);
    if (n > 0) {
      out.erase(0, (usize)n);
      continue;
    }
    if (n < 0 && would_block())
      break;
    close();
    return false;
  }
  char buf[16384];
  while (true) {
    const int n = recv(raw(sock), buf, sizeof buf, 0);
    if (n > 0) {
      in.append(buf, (usize)n);
      continue;
    }
    if (n < 0 && would_block())
      break;
    close(); // 0: the peer closed; <0: an error
    return false;
  }
  return true;
}

bool net_link::next(std::string &line) {
  const usize nl = in.find('\n');
  if (nl == std::string::npos)
    return false;
  line.assign(in, 0, nl);
  in.erase(0, nl + 1);
  return true;
}

void net_link::close() {
  net_close(sock);
  out.clear();
  in.clear();
}
} // namespace njin
