#pragma once
#include "_types.h"
#include <string>

namespace njin {
struct context;

/// @addtogroup grp_reload
/// @{

/// A resource that was just reloaded, sent through events() after the reload.
///
/// The resource's handle does not change, so there is usually nothing to do; this event
/// is for games that want to react (write a log, recompute something based on the image size).
struct asset_reloaded {
  std::string path;    ///< Path of the file that changed.
  bool shader = false; ///< `true` for a shader, `false` for a texture.
  bool ok = true;      ///< `false` if the reload failed (for example a shader failed to compile) and the old version was kept.
  bool script = false; ///< `true` for a Lua script (njin_script.h); `shader` is then `false`.
};

/// Turns hot reload on or off: edit an image, shader or Lua script file while the game is
/// running, save it, and the game uses the new version immediately, with no restart.
///
/// When on, the engine checks the modification time of every loaded texture, shader and
/// script file a few times per second. A changed file is reloaded **into the same old handle**, so
/// sprites, tilemaps and post shaders using it change right away. A script attached to entities
/// gets its functions replaced while the data in `self` stays (see script_attach()). A file that just changed is
/// reloaded on the next check, once it has stopped changing, so a file an editor
/// is still half-writing is not read by mistake.
///
/// A shader that fails to compile **keeps the old version** and the compiler's error is written to the
/// log, so saving a broken shader does not break the game; once fixed, the new version goes in immediately.
///
/// Off by default: checking files costs a little time each pass, and a released game
/// does not need it. Usually enabled in debug builds:
/// @code
/// #ifndef NDEBUG
///   njin::hot_reload_enable(*ctx, true);
/// #endif
/// @endcode
/// @param ctx Engine context.
/// @param on On or off.
/// @param interval Time between two checks, in real seconds.
void hot_reload_enable(context &ctx, bool on, f32 interval = 0.25f);

/// Whether hot reload is on. @param ctx Engine context. @return `true` if on.
bool hot_reload_enabled(const context &ctx);

/// Checks every file right now and reloads the ones that changed, without waiting. Works
/// even when hot reload is off, for example bound to the F5 key.
/// @param ctx Engine context.
/// @return Number of resources reloaded successfully.
i32 hot_reload_now(context &ctx);
/// @}
} // namespace njin
