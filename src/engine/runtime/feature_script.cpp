// The script feature (njin_features.h): Lua 5.4 through sol2, hot reloaded.
#include "njin_ctx_get.h"
#include "njin_script_impl.h"

namespace njin {
void feature_install_script(context &ctx) {
  ctx_ref<script_state>(ctx).emplace();
  ctx_ref<feature_hooks>(ctx).watched_files = script_watched_files;
  ctx_ref<feature_hooks>(ctx).reload_file = script_reload_file;
  ctx_ref<feature_hooks>(ctx).modules_last.push_back(script_module());
}
} // namespace njin
