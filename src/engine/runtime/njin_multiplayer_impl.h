#pragma once
#include "njin_internal_only.h"

#include "_mod.h"

namespace njin {
struct context;

// Realtime multiplayer (njin_multiplayer.h), on ENet. Everything lives in
// njin_multiplayer_enet.cpp, apart from the rest of the runtime: ENet pulls in
// winsock, which clashes with raylib. ENet is not thread-safe and never
// blocks when serviced with a zero timeout, so it runs on the game thread;
// a host with net_host_opts::service_thread is also pumped on a thread of the
// service, every call taking its mutex.

class mp_service; // njin_multiplayer_enet.cpp

mp_service *mp_service_create();
void mp_service_destroy(mp_service *service);

// The context's service, made on first use when `create` is set. Defined in
// njin_network.cpp, which can see the context.
mp_service *mp_service_of(context &ctx, bool create);
const mp_service *mp_service_of(const context &ctx);

// Core module: receives in phase_pre_update, sends in phase_post_update.
mod_desc multiplayer_module();
} // namespace njin
