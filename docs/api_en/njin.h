#pragma once
#include "_collide.h"
#include "_comps.h"
#include "njin_3d.h"
#include "_math.h"
#include "_random.h"
#include "_tilemap.h"
#include "_tween.h"
#include "njin_cfg.h"
#include "njin_collision.h"
#include "njin_anim.h"
#include "njin_anim3d.h"
#include "njin_atlas.h"
#include "njin_audio.h"
#include "njin_bindings.h"
#include "njin_body.h"
#include "njin_camera.h"
#include "njin_ctx.h"
#include "njin_debug.h"
#include "njin_dialog.h"
#include "njin_draw.h"
#include "njin_file.h"
#include "njin_fx.h"
#include "njin_gizmo.h"
#include "njin_i18n.h"
#include "njin_input.h"
#include "njin_json.h"
#include "njin_light.h"
#include "njin_level.h"
#include "njin_log.h"
#include "njin_nav.h"
#include "njin_particles.h"
#include "njin_physics3d.h"
#include "njin_post.h"
#include "njin_post3d.h"
#include "njin_prefab.h"
#include "njin_procgen.h"
#include "njin_render.h"
#include "njin_reload.h"
#include "njin_scene.h"
#include "njin_script.h"
#include "njin_settings.h"
#include "njin_spatial.h"
#include "njin_spatial_batch.h"
#include "njin_timer.h"
#include "njin_ui.h"
#include "njin_ui_layout.h"
#include "njin_version.h"
#include "njin_window.h"
#include "njin_world3d.h"

namespace njin {
/// Opaque engine handle. Created by create(), freed by
/// destroy().
///
/// Use it only through functions that take `context &`. Every system receives it.
struct context;

/// @addtogroup grp_core
/// @{

/// Opens the window and returns the engine context. Never returns nullptr.
///
/// The engine's core module (camera) is already registered. Register the game's modules
/// with mod_register() before calling run().
/// @param cfg Window and loop configuration.
/// @return Engine context. Never nullptr.
context *create(const config &cfg);

/// Runs the main loop until the window closes.
///
/// Calls the phases in the order of sys_phase. A second call is ignored.
/// @param ctx Context from create().
void run(context &ctx);

/// Frees every engine resource and closes the window. nullptr is ignored.
/// @param ctx Context from create(), or nullptr.
void destroy(context *ctx);
/// @}
} // namespace njin
