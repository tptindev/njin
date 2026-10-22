#pragma once
#include "njin_internal_only.h"

#include "_mod.h"
#include "_types.h"
#include "njin_network.h"
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace njin {
// TCP and UDP for games (njin_network.h). The sockets live on one network
// thread running an asio io_context, each connection a pair of coroutines
// (reader and writer). That code is in njin_network_asio.cpp, apart from the
// rest of the runtime: asio pulls in winsock, which clashes with raylib. This
// header is the boundary: it names no asio type.
//
// The network thread never touches the context. It queues what happened into
// the service, and the network module moves it into events() each frame.

using net_event = std::variant<net_connected, net_connect_failed, net_disconnected, net_message, net_datagram>;

class net_service; // njin_network_asio.cpp
class mp_service;  // njin_multiplayer_enet.cpp
struct netcode_state; // njin_netcode.cpp
struct lockstep_state; // njin_lockstep.cpp

net_service *net_service_create();
void net_service_destroy(net_service *service);

net_conn net_service_connect(net_service &s, const char *host, u16 port, const tcp_opts &opts);
bool net_service_send(net_service &s, net_conn conn, std::string_view data);
void net_service_close(net_service &s, net_conn conn);
bool net_service_connected(const net_service &s, net_conn conn);
usize net_service_pending(const net_service &s, net_conn conn);

// Opening a port is done on the network thread but waited for, so a taken
// port is reported at once. `error` gets the reason on failure.
net_server net_service_listen(net_service &s, u16 port, const char *host, const tcp_opts &opts, std::string &error);
u16 net_service_server_port(const net_service &s, net_server server);
void net_service_server_close(net_service &s, net_server server);

net_udp net_service_udp_open(net_service &s, u16 port, const char *host, std::string &error);
bool net_service_udp_send(net_service &s, net_udp socket, const net_endpoint &to, std::string_view data);
u16 net_service_udp_port(const net_service &s, net_udp socket);
void net_service_udp_close(net_service &s, net_udp socket);

// Takes every event queued since the last call, oldest first.
void net_service_drain(net_service &s, std::vector<net_event> &out);

struct network_store {
  // Started on first use: a game that never touches the network runs no
  // network thread.
  net_service *service = nullptr;
  std::vector<net_event> drained; // reused each frame
  // Realtime hosts (njin_multiplayer.h), also made on first use.
  mp_service *multiplayer = nullptr;
  // The netcode session (njin_netcode.h) and its registered components.
  netcode_state *netcode = nullptr;
  // The lockstep session (njin_lockstep.h).
  lockstep_state *lockstep = nullptr;

  network_store() = default;
  ~network_store();
  network_store(const network_store &) = delete;
  network_store &operator=(const network_store &) = delete;
};

// Core module: moves network events into events() in phase_pre_update.
mod_desc network_module();
} // namespace njin
