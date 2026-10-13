// The network thread (njin_network.h), on asio coroutines. Kept apart from the
// rest of the runtime: asio pulls in winsock, and winsock and raylib declare
// clashing names (CloseWindow, Rectangle, ...) and cannot share a file.
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00 // Windows 10; asio asks for a target version
#endif
#include "njin_network_impl.h"

#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace njin {
using asio::awaitable;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;

namespace {
// Every await takes its error into `ec` instead of throwing: a dropped
// connection is normal here, not exceptional.
auto with(std::error_code &ec) { return asio::redirect_error(asio::use_awaitable, ec); }

// Reasons in plain English. The system's own text (ec.message()) is used only
// as a fallback: on Windows it comes in the user's language and code page.
std::string reason_of(const std::error_code &ec) {
  namespace e = asio::error;
  if (ec == e::eof)
    return "closed by peer";
  if (ec == e::connection_reset || ec == e::connection_aborted)
    return "connection reset";
  if (ec == e::connection_refused)
    return "connection refused";
  if (ec == e::timed_out)
    return "timed out";
  if (ec == e::host_not_found || ec == e::host_not_found_try_again || ec == e::no_data)
    return "host not found";
  if (ec == e::network_unreachable || ec == e::network_down)
    return "network unreachable";
  if (ec == e::host_unreachable)
    return "host unreachable";
  if (ec == e::address_in_use)
    return "address in use";
  if (ec == e::address_family_not_supported)
    return "address family not supported";
  if (ec == e::access_denied)
    return "access denied";
  return ec.message();
}

net_endpoint endpoint_of(const tcp::endpoint &ep) { return {ep.address().to_string(), ep.port()}; }
net_endpoint endpoint_of(const udp::endpoint &ep) { return {ep.address().to_string(), ep.port()}; }

// 4-byte little-endian length, then the bytes.
std::string framed(std::string_view data) {
  std::string out(4 + data.size(), '\0');
  const u32 n = (u32)data.size();
  for (int i = 0; i < 4; ++i)
    out[i] = (char)((n >> (8 * i)) & 0xffu);
  data.copy(out.data() + 4, data.size());
  return out;
}

// The address to bind: null is every IPv4 address.
bool bind_address(const char *host, asio::ip::address &out, std::string &error) {
  if (!host || !*host) {
    out = asio::ip::address_v4::any();
    return true;
  }
  std::error_code ec;
  out = asio::ip::make_address(host, ec);
  if (ec)
    error = std::string("not an IP address: ") + host;
  return !ec;
}
} // namespace

// One TCP connection. The socket, `out` and `closed` belong to the network
// thread; the game thread reads only the atomics.
struct tcp_link {
  u32 id = 0;
  u32 server = 0; // the server that accepted it, 0 for tcp_connect
  tcp_opts opts;
  tcp::socket sock;
  asio::steady_timer wake; // the writer sleeps on it until there is output
  std::deque<std::string> out;
  bool closed = false;
  bool connect_done = false;
  bool timed_out = false;
  std::atomic<bool> open{false};
  std::atomic<usize> pending{0};

  tcp_link(u32 id, const tcp_opts &opts, tcp::socket sock)
      : id(id), opts(opts), sock(std::move(sock)), wake(this->sock.get_executor()) {}
};

struct tcp_server_state {
  u32 id = 0;
  tcp_opts opts;
  tcp::acceptor acceptor;
  u16 port = 0; // set before it is shared, then constant
  bool closed = false;

  explicit tcp_server_state(asio::io_context &io) : acceptor(io) {}
};

struct udp_state {
  u32 id = 0;
  udp::socket sock;
  u16 port = 0; // set before it is shared, then constant
  bool closed = false;
  std::string buffer = std::string(65536, '\0');
  udp::endpoint from;

  explicit udp_state(asio::io_context &io) : sock(io) {}
};

class net_service {
public:
  // Destroyed last: every socket must go before its io_context.
  asio::io_context io{1};
  asio::executor_work_guard<asio::io_context::executor_type> work{io.get_executor()};
  std::thread thread;
  std::atomic<u32> next_id{1};

  // Guards the three tables and `events`, which both threads use.
  mutable std::mutex mu;
  std::unordered_map<u32, std::shared_ptr<tcp_link>> links;
  std::unordered_map<u32, std::shared_ptr<tcp_server_state>> servers;
  std::unordered_map<u32, std::shared_ptr<udp_state>> udps;
  std::vector<net_event> events;

  // Started last, once every member exists.
  net_service() { thread = std::thread([this] { io.run(); }); }

  ~net_service() {
    work.reset();
    io.stop();
    thread.join();
    // Sockets that only these tables hold die here, while the io_context
    // still exists; the ones held by unfinished coroutines die with it.
    std::lock_guard lock(mu);
    links.clear();
    servers.clear();
    udps.clear();
  }

  void push(net_event event) {
    std::lock_guard lock(mu);
    events.push_back(std::move(event));
  }

  template <class T> std::shared_ptr<T> find(const std::unordered_map<u32, std::shared_ptr<T>> &table, u32 id) const {
    if (id == 0)
      return nullptr;
    std::lock_guard lock(mu);
    const auto it = table.find(id);
    return it == table.end() ? nullptr : it->second;
  }

  template <class T> std::shared_ptr<T> take(std::unordered_map<u32, std::shared_ptr<T>> &table, u32 id) {
    std::lock_guard lock(mu);
    const auto it = table.find(id);
    if (it == table.end())
      return nullptr;
    std::shared_ptr<T> found = std::move(it->second);
    table.erase(it);
    return found;
  }

  // Network thread. Ends a link that failed or was closed by the peer, once:
  // a reader and a writer can both see the same failure.
  void fail(const std::shared_ptr<tcp_link> &l, const std::string &reason) {
    if (l->closed)
      return; // the game closed it: no event
    shut(*l);
    take(links, l->id);
    push(net_disconnected{{l->id}, reason});
  }

  static void shut(tcp_link &l) {
    l.closed = true;
    l.open = false;
    std::error_code ignored;
    l.sock.shutdown(tcp::socket::shutdown_both, ignored);
    l.sock.close(ignored);
    l.wake.cancel();
  }

  // Network thread: the connection is up, start reading and writing.
  void start(const std::shared_ptr<tcp_link> &l) {
    std::error_code ec;
    l->sock.set_option(tcp::no_delay(l->opts.no_delay), ec);
    const tcp::endpoint remote = l->sock.remote_endpoint(ec);
    if (ec) {
      fail(l, reason_of(ec));
      return;
    }
    l->open = true;
    // Queued before the reader starts, so net_connected always comes before
    // the first net_message of the link.
    push(net_connected{{l->id}, {l->server}, endpoint_of(remote)});
    asio::co_spawn(io, writer(l), asio::detached);
    asio::co_spawn(io, reader(l), asio::detached);
  }

  awaitable<void> connect(std::shared_ptr<tcp_link> l, std::string host, u16 port) {
    std::error_code ec;
    auto resolver = std::make_shared<tcp::resolver>(io);
    asio::steady_timer deadline(io);
    deadline.expires_after(
        std::chrono::duration_cast<asio::steady_timer::duration>(std::chrono::duration<f32>(l->opts.connect_timeout)));
    // Holds only shared pointers: it may run after this coroutine has ended.
    deadline.async_wait([l, resolver](const std::error_code &e) {
      if (e || l->connect_done)
        return;
      l->timed_out = true;
      resolver->cancel();
      std::error_code ignored;
      l->sock.close(ignored);
    });
    const auto found = co_await resolver->async_resolve(host, std::to_string(port), with(ec));
    if (!ec && !l->closed && !l->timed_out)
      co_await asio::async_connect(l->sock, found, with(ec));
    l->connect_done = true;
    deadline.cancel();
    if (l->closed)
      co_return; // tcp_close() came first
    if (l->timed_out)
      ec = asio::error::timed_out;
    if (ec) {
      l->closed = true;
      take(links, l->id);
      push(net_connect_failed{{l->id}, reason_of(ec)});
      co_return;
    }
    start(l);
  }

  awaitable<void> writer(std::shared_ptr<tcp_link> l) {
    std::error_code ec;
    std::string batch;
    while (!l->closed) {
      if (l->out.empty()) {
        l->wake.expires_at(asio::steady_timer::time_point::max());
        co_await l->wake.async_wait(with(ec)); // woken by a send or a close
        continue;
      }
      // Everything queued so far goes out in one write.
      batch.clear();
      for (const std::string &m : l->out)
        batch += m;
      l->out.clear();
      co_await asio::async_write(l->sock, asio::buffer(batch), with(ec));
      l->pending -= batch.size();
      if (ec) {
        fail(l, reason_of(ec));
        co_return;
      }
    }
  }

  awaitable<void> reader(std::shared_ptr<tcp_link> l) {
    std::error_code ec;
    unsigned char head[4];
    for (;;) {
      co_await asio::async_read(l->sock, asio::buffer(head), with(ec));
      if (ec) {
        fail(l, reason_of(ec));
        co_return;
      }
      const u32 size = (u32)head[0] | (u32)head[1] << 8 | (u32)head[2] << 16 | (u32)head[3] << 24;
      if (size > l->opts.max_message) {
        fail(l, "message of " + std::to_string(size) + " bytes is over max_message");
        co_return;
      }
      std::string data(size, '\0');
      if (size > 0)
        co_await asio::async_read(l->sock, asio::buffer(data), with(ec));
      if (ec) {
        fail(l, reason_of(ec));
        co_return;
      }
      if (l->closed)
        co_return;
      push(net_message{{l->id}, std::move(data)});
    }
  }

  awaitable<void> accept(std::shared_ptr<tcp_server_state> srv) {
    std::error_code ec;
    asio::steady_timer pause(io);
    for (;;) {
      tcp::socket sock(io);
      co_await srv->acceptor.async_accept(sock, with(ec));
      if (srv->closed || ec == asio::error::operation_aborted)
        co_return;
      if (ec) {
        // Out of sockets or the like: wait a moment instead of spinning.
        pause.expires_after(std::chrono::milliseconds(100));
        co_await pause.async_wait(with(ec));
        continue;
      }
      auto l = std::make_shared<tcp_link>(next_id++, srv->opts, std::move(sock));
      l->server = srv->id;
      {
        std::lock_guard lock(mu);
        links.emplace(l->id, l);
      }
      start(l);
    }
  }

  awaitable<void> receive(std::shared_ptr<udp_state> u) {
    std::error_code ec;
    for (;;) {
      const usize n = co_await u->sock.async_receive_from(asio::buffer(u->buffer), u->from, with(ec));
      if (u->closed || ec == asio::error::operation_aborted)
        co_return;
      // On Windows a packet sent to a closed port comes back as a reset on
      // the next receive; it is not the socket failing.
      if (ec)
        continue;
      push(net_datagram{{u->id}, endpoint_of(u->from), u->buffer.substr(0, n)});
    }
  }

  awaitable<void> send_to(std::shared_ptr<udp_state> u, std::string host, u16 port, std::shared_ptr<std::string> data) {
    std::error_code ec;
    udp::endpoint to;
    const asio::ip::address address = asio::ip::make_address(host, ec);
    if (!ec) {
      to = udp::endpoint(address, port);
    } else {
      // Resolved on each send: give udp_send() an IP for traffic that matters.
      udp::resolver resolver(io);
      const auto found = co_await resolver.async_resolve(host, std::to_string(port), with(ec));
      if (ec || found.empty())
        co_return; // UDP promises nothing; an unknown host is a lost packet
      // A socket bound to IPv4 can only reach IPv4, and so on.
      const bool v6 = u->sock.local_endpoint(ec).address().is_v6();
      to = found.begin()->endpoint();
      for (const auto &entry : found)
        if (entry.endpoint().address().is_v6() == v6) {
          to = entry.endpoint();
          break;
        }
    }
    if (!u->closed)
      co_await u->sock.async_send_to(asio::buffer(*data), to, with(ec));
  }
};

net_service *net_service_create() { return new net_service(); }
void net_service_destroy(net_service *service) { delete service; }

net_conn net_service_connect(net_service &s, const char *host, u16 port, const tcp_opts &opts) {
  auto l = std::make_shared<tcp_link>(s.next_id++, opts, tcp::socket(s.io));
  {
    std::lock_guard lock(s.mu);
    s.links.emplace(l->id, l);
  }
  asio::co_spawn(s.io, s.connect(l, host, port), asio::detached);
  return {l->id};
}

bool net_service_send(net_service &s, net_conn conn, std::string_view data) {
  const std::shared_ptr<tcp_link> l = s.find(s.links, conn.id);
  if (!l || data.size() > l->opts.max_message)
    return false;
  const usize size = 4 + data.size();
  if (l->pending + size > l->opts.max_pending)
    return false;
  l->pending += size;
  asio::post(s.io, [l, message = framed(data)]() mutable {
    if (l->closed)
      return;
    l->out.push_back(std::move(message));
    l->wake.cancel();
  });
  return true;
}

void net_service_close(net_service &s, net_conn conn) {
  const std::shared_ptr<tcp_link> l = s.take(s.links, conn.id);
  if (!l)
    return;
  l->open = false;
  asio::post(s.io, [l] { net_service::shut(*l); });
}

bool net_service_connected(const net_service &s, net_conn conn) {
  const std::shared_ptr<tcp_link> l = s.find(s.links, conn.id);
  return l && l->open;
}

usize net_service_pending(const net_service &s, net_conn conn) {
  const std::shared_ptr<tcp_link> l = s.find(s.links, conn.id);
  return l ? l->pending.load() : 0;
}

net_server net_service_listen(net_service &s, u16 port, const char *host, const tcp_opts &opts, std::string &error) {
  asio::ip::address address;
  if (!bind_address(host, address, error))
    return {};
  // Opened here on the game thread: nothing else holds it yet, and a taken
  // port is reported at once.
  auto srv = std::make_shared<tcp_server_state>(s.io);
  srv->opts = opts;
  const tcp::endpoint where(address, port);
  std::error_code ec;
  srv->acceptor.open(where.protocol(), ec);
#if !defined(_WIN32)
  // Lets a restarted server take its port back while old connections linger.
  // Not on Windows, where the same option lets two servers share the port.
  if (!ec)
    srv->acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
#endif
  if (!ec)
    srv->acceptor.bind(where, ec);
  if (!ec)
    srv->acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (!ec)
    srv->port = srv->acceptor.local_endpoint(ec).port();
  if (ec) {
    error = reason_of(ec);
    return {};
  }
  srv->id = s.next_id++;
  {
    std::lock_guard lock(s.mu);
    s.servers.emplace(srv->id, srv);
  }
  asio::co_spawn(s.io, s.accept(srv), asio::detached);
  return {srv->id};
}

u16 net_service_server_port(const net_service &s, net_server server) {
  const std::shared_ptr<tcp_server_state> srv = s.find(s.servers, server.id);
  return srv ? srv->port : 0;
}

void net_service_server_close(net_service &s, net_server server) {
  const std::shared_ptr<tcp_server_state> srv = s.take(s.servers, server.id);
  if (!srv)
    return;
  asio::post(s.io, [srv] {
    srv->closed = true;
    std::error_code ignored;
    srv->acceptor.close(ignored);
  });
}

net_udp net_service_udp_open(net_service &s, u16 port, const char *host, std::string &error) {
  asio::ip::address address;
  if (!bind_address(host, address, error))
    return {};
  auto u = std::make_shared<udp_state>(s.io);
  const udp::endpoint where(address, port);
  std::error_code ec;
  u->sock.open(where.protocol(), ec);
  if (!ec)
    u->sock.bind(where, ec);
  if (!ec)
    u->port = u->sock.local_endpoint(ec).port();
  if (ec) {
    error = reason_of(ec);
    return {};
  }
  u->id = s.next_id++;
  {
    std::lock_guard lock(s.mu);
    s.udps.emplace(u->id, u);
  }
  asio::co_spawn(s.io, s.receive(u), asio::detached);
  return {u->id};
}

bool net_service_udp_send(net_service &s, net_udp socket, const net_endpoint &to, std::string_view data) {
  const std::shared_ptr<udp_state> u = s.find(s.udps, socket.id);
  if (!u || data.size() > 65507 || to.host.empty())
    return false;
  auto bytes = std::make_shared<std::string>(data);
  std::error_code ec;
  const asio::ip::address address = asio::ip::make_address(to.host, ec);
  if (ec) {
    // A name: resolved on the network thread first.
    asio::co_spawn(s.io, s.send_to(u, to.host, to.port, std::move(bytes)), asio::detached);
    return true;
  }
  // An IP, the common case for game traffic: one send, no coroutine frame.
  asio::post(s.io, [u, bytes = std::move(bytes), to = udp::endpoint(address, to.port)] {
    if (!u->closed)
      u->sock.async_send_to(asio::buffer(*bytes), to, [u, bytes](const std::error_code &, usize) {});
  });
  return true;
}

u16 net_service_udp_port(const net_service &s, net_udp socket) {
  const std::shared_ptr<udp_state> u = s.find(s.udps, socket.id);
  return u ? u->port : 0;
}

void net_service_udp_close(net_service &s, net_udp socket) {
  const std::shared_ptr<udp_state> u = s.take(s.udps, socket.id);
  if (!u)
    return;
  asio::post(s.io, [u] {
    u->closed = true;
    std::error_code ignored;
    u->sock.close(ignored);
  });
}

void net_service_drain(net_service &s, std::vector<net_event> &out) {
  std::lock_guard lock(s.mu);
  out.swap(s.events);
  s.events.clear();
}
} // namespace njin
