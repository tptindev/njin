// The nav3d feature (njin_features.h): navmeshes and crowd agents on Recast
// and Detour.
#include "njin_ctx_get.h"
#include "njin_nav3d_impl.h"

namespace njin {
void feature_install_nav3d(context &ctx) {
  ctx_ref<nav3d_store>(ctx).emplace();
  ctx_ref<feature_hooks>(ctx).modules_mid.push_back(nav3d_module());
}
} // namespace njin
