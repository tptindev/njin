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
#include "njin_render.h"
#include "njin_reload.h"
#include "njin_scene.h"
#include "njin_settings.h"
#include "njin_timer.h"
#include "njin_ui.h"
#include "njin_ui_layout.h"
#include "njin_version.h"
#include "njin_window.h"

namespace njin {
/// Handle mờ của engine. Tạo bằng njin_create(), giải phóng bằng
/// njin_destroy().
///
/// Chỉ dùng qua các hàm nhận `njin_ctx &`. Mọi system đều nhận nó.
struct njin_ctx;

/// @addtogroup grp_core
/// @{

/// Mở cửa sổ và trả về context của engine. Không bao giờ trả về nullptr.
///
/// Module lõi của engine (camera) đã được đăng ký sẵn. Đăng ký module của game
/// bằng njin_mod_register() trước khi gọi njin_run().
/// @param cfg Cấu hình cửa sổ và vòng lặp.
/// @return Context của engine. Không bao giờ là nullptr.
njin_ctx *njin_create(const njin_cfg &cfg);

/// Chạy vòng lặp chính cho đến khi cửa sổ đóng.
///
/// Gọi các phase theo thứ tự của sys_phase. Gọi lần thứ hai bị bỏ qua.
/// @param ctx Context từ njin_create().
void njin_run(njin_ctx &ctx);

/// Giải phóng mọi tài nguyên của engine và đóng cửa sổ. nullptr bị bỏ qua.
/// @param ctx Context từ njin_create(), hoặc nullptr.
void njin_destroy(njin_ctx *ctx);
/// @}
} // namespace njin
