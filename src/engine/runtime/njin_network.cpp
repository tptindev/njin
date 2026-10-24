#include "njin_network_impl.h"
#include "njin_lockstep_impl.h"
#include "njin_multiplayer_impl.h"
#include "njin_netcode_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_ecs.h"
#include "njin_log.h"

namespace njin {
namespace {
net_service &service(context &ctx) {
  if (!ctx_ref<network_store>(ctx)->service)
    ctx_ref<network_store>(ctx)->service = net_service_create();
  return *ctx_ref<network_store>(ctx)->service;
}

// Queries never start the network thread: with no service, no handle is valid.
const net_service *service_if_any(const context &ctx) { return ctx_ref<network_store>(ctx)->service; }

void pump_network(context &ctx) {
  network_store &store = ctx_ref<network_store>(ctx);
  if (!store.service)
    return;
  net_service_drain(*store.service, store.drained);
  entt::dispatcher &dispatcher = events(ctx);
  for (net_event &event : store.drained)
    std::visit([&](auto &e) { dispatcher.enqueue(std::move(e)); }, event);
  store.drained.clear();
}

void setup(context &ctx) { ecs_register(ctx, phase_pre_update, pump_network, "network"); }
} // namespace

network_store::~network_store() {
  if (netcode) // before the hosts: it only names them
    netcode_state_destroy(netcode);
  if (lockstep) // the same
    lockstep_state_destroy(lockstep);
  if (multiplayer)
    mp_service_destroy(multiplayer);
  if (service)
    net_service_destroy(service);
}

mp_service *mp_service_of(context &ctx, bool create) {
  if (!ctx_ref<network_store>(ctx)->multiplayer && create)
    ctx_ref<network_store>(ctx)->multiplayer = mp_service_create();
  return ctx_ref<network_store>(ctx)->multiplayer;
}

const mp_service *mp_service_of(const context &ctx) { return ctx_ref<network_store>(ctx)->multiplayer; }

mod_desc network_module() { return mod_desc{.name = "njin.network", .setup = setup}; }

net_conn tcp_connect(context &ctx, const char *host, u16 port, const tcp_opts &opts) {
  if (!host || !*host) {
    NJIN_WARN("tcp_connect: no host");
    return {};
  }
  return net_service_connect(service(ctx), host, port, opts);
}

bool tcp_send(context &ctx, net_conn conn, std::string_view data) {
  return ctx_ref<network_store>(ctx)->service && net_service_send(*ctx_ref<network_store>(ctx)->service, conn, data);
}

void tcp_close(context &ctx, net_conn conn) {
  if (ctx_ref<network_store>(ctx)->service)
    net_service_close(*ctx_ref<network_store>(ctx)->service, conn);
}

bool tcp_connected(const context &ctx, net_conn conn) {
  const net_service *s = service_if_any(ctx);
  return s && net_service_connected(*s, conn);
}

usize tcp_pending(const context &ctx, net_conn conn) {
  const net_service *s = service_if_any(ctx);
  return s ? net_service_pending(*s, conn) : 0;
}

net_server tcp_listen(context &ctx, u16 port, const char *host, const tcp_opts &opts) {
  std::string error;
  const net_server server = net_service_listen(service(ctx), port, host, opts, error);
  if (server.id == 0)
    NJIN_WARN("tcp_listen: cannot listen on %s:%u: %s", host ? host : "*", (unsigned)port, error.c_str());
  return server;
}

u16 tcp_server_port(const context &ctx, net_server server) {
  const net_service *s = service_if_any(ctx);
  return s ? net_service_server_port(*s, server) : 0;
}

void tcp_server_close(context &ctx, net_server server) {
  if (ctx_ref<network_store>(ctx)->service)
    net_service_server_close(*ctx_ref<network_store>(ctx)->service, server);
}

net_udp udp_open(context &ctx, u16 port, const char *host) {
  std::string error;
  const net_udp socket = net_service_udp_open(service(ctx), port, host, error);
  if (socket.id == 0)
    NJIN_WARN("udp_open: cannot open %s:%u: %s", host ? host : "*", (unsigned)port, error.c_str());
  return socket;
}

bool udp_send(context &ctx, net_udp socket, const net_endpoint &to, std::string_view data) {
  return ctx_ref<network_store>(ctx)->service && net_service_udp_send(*ctx_ref<network_store>(ctx)->service, socket, to, data);
}

u16 udp_port(const context &ctx, net_udp socket) {
  const net_service *s = service_if_any(ctx);
  return s ? net_service_udp_port(*s, socket) : 0;
}

void udp_close(context &ctx, net_udp socket) {
  if (ctx_ref<network_store>(ctx)->service)
    net_service_udp_close(*ctx_ref<network_store>(ctx)->service, socket);
}
} // namespace njin
