#pragma once
#include "_comps.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_log.h"

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
