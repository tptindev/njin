#pragma once
#include "_collide.h"
#include "_comps.h"
#include "_math.h"
#include "_random.h"
#include "_tilemap.h"
#include "_tween.h"
#include "njin_cfg.h"
#include "njin_collision.h"
#include "njin_anim.h"
#include "njin_atlas.h"
#include "njin_bits.h"
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
#include "njin_post.h"
#include "njin_prefab.h"
#include "njin_procgen.h"
#include "njin_quality.h"
#include "njin_render.h"
#include "njin_reload.h"
#include "njin_scene.h"
#include "njin_settings.h"
#include "njin_spatial.h"
#include "njin_spatial_batch.h"
#include "njin_spline.h"
#include "njin_timer.h"
#include "njin_ui.h"
#include "njin_ui_layout.h"
#include "njin_version.h"
#include "njin_window.h"
// Header của các feature (njin_use() trong cmake/njin.cmake): chỉ những feature
// game đã bật.
#if defined(NJIN_FEATURE_3D) || defined(NJIN_ENGINE_INTERNAL)
#include "njin_3d.h"
#include "njin_anim3d.h"
#include "njin_post3d.h"
#include "njin_world3d.h"
#endif
#if defined(NJIN_FEATURE_PHYSICS3D) || defined(NJIN_ENGINE_INTERNAL)
#include "njin_physics3d.h"
#endif
#if defined(NJIN_FEATURE_NAV3D) || defined(NJIN_ENGINE_INTERNAL)
#include "njin_nav3d.h"
#endif
#if defined(NJIN_FEATURE_VIDEO) || defined(NJIN_ENGINE_INTERNAL)
#include "njin_video.h"
#endif
#if defined(NJIN_FEATURE_NET) || defined(NJIN_ENGINE_INTERNAL)
#include "njin_network.h"
#include "njin_multiplayer.h"
#include "njin_netcode.h"
#include "njin_lockstep.h"
#endif
#if defined(NJIN_FEATURE_SCRIPT) || defined(NJIN_ENGINE_INTERNAL)
#include "njin_script.h"
#endif

namespace njin {
/// Handle mờ của engine. Tạo bằng create(), giải phóng bằng
/// destroy().
///
/// Chỉ dùng qua các hàm nhận `context &`. Mọi system đều nhận nó.
struct context;

/// @addtogroup grp_core
/// @{

/// Mở cửa sổ và trả về context của engine. Không bao giờ trả về nullptr.
///
/// Module lõi của engine (camera) đã được đăng ký sẵn. Đăng ký module của game
/// bằng mod_register() trước khi gọi run().
/// @param cfg Cấu hình cửa sổ và vòng lặp.
/// @return Context của engine. Không bao giờ là nullptr.
context *create(const config &cfg);

/// Chạy vòng lặp chính cho đến khi cửa sổ đóng.
///
/// Gọi các phase theo thứ tự của sys_phase. Gọi lần thứ hai bị bỏ qua.
/// @param ctx Context từ create().
void run(context &ctx);

/// Giải phóng mọi tài nguyên của engine và đóng cửa sổ. nullptr bị bỏ qua.
/// @param ctx Context từ create(), hoặc nullptr.
void destroy(context *ctx);
/// @}
} // namespace njin
