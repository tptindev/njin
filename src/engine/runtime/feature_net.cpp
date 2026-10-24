// The net feature (njin_features.h): sockets on asio, multiplayer on ENet,
// netcode and lockstep.
#include "njin_ctx_impl.h"
#include "njin_lockstep_impl.h"
#include "njin_multiplayer_impl.h"
#include "njin_netcode_impl.h"
#include "njin_network_impl.h"

namespace njin {
void feature_install_net(context &ctx) {
  ctx.network.emplace();
  for (const mod_desc &m : {network_module(), multiplayer_module(), netcode_module(), lockstep_module()})
    ctx.hooks.modules_mid.push_back(m);
}
} // namespace njin
