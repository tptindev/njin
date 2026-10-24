// The script feature (njin_features.h): Lua 5.4 through sol2, hot reloaded.
#include "njin_ctx_impl.h"
#include "njin_script_impl.h"

namespace njin {
void feature_install_script(context &ctx) {
  ctx.script.emplace();
  ctx.hooks.watched_files = script_watched_files;
  ctx.hooks.reload_file = script_reload_file;
  ctx.hooks.modules_last.push_back(script_module());
}
} // namespace njin
