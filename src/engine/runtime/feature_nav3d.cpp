// The nav3d feature (njin_features.h): navmeshes and crowd agents on Recast
// and Detour.
#include "njin_ctx_impl.h"
#include "njin_nav3d_impl.h"

namespace njin {
void feature_install_nav3d(context &ctx) {
  ctx.nav3d.emplace();
  ctx.hooks.modules_mid.push_back(nav3d_module());
}
} // namespace njin
