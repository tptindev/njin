#pragma once
#include "_math.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_debug
/// @{

/// @name Gizmo
/// Hình vẽ để debug: đường, mũi tên, khung, nhãn chữ, trong thế giới 2D hoặc 3D.
///
/// Gọi được ở mọi phase (cả `phase_update` và `phase_fixed_update`), không cần
/// nằm trong lúc vẽ: engine gom lại và vẽ đè lên trên cùng của thế giới (không bị
/// hình nào che), đường mảnh 1 pixel, chữ cỡ cố định trên màn hình. Gizmo 2D vẽ
/// qua camera 2D; gizmo 3D vẽ ở end_3d() qua camera của lần vẽ 3D đó (không có
/// begin_3d() thì không hiện). Khi njin_inspector đang nối, gizmo cũng hiện trong
/// ô World của nó.
///
/// `duration` là số giây gizmo còn hiện: 0 (mặc định) là chỉ frame này, nên gọi
/// lại mỗi frame; lớn hơn để giữ lại một vết (đường đạn, điểm va chạm).
///
/// @code
/// njin::gizmo_arrow(ctx, pos, pos + velocity * 0.2f, njin::colors::yellow);
/// njin::gizmo_text(ctx, pos + njin::vec2{0, -20}, "đang nhảy");
/// njin::gizmo_box3d(ctx, enemy_pos, {1, 2, 1}, njin::colors::red);
/// njin::gizmo_line3d(ctx, muzzle, hit, njin::colors::yellow, 1.0f); // giữ 1 giây
/// @endcode
/// @{

/// Đoạn thẳng 2D.
/// @param ctx Context của engine.
/// @param a Điểm đầu, tọa độ thế giới.
/// @param b Điểm cuối.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_line(njin_ctx &ctx, vec2 a, vec2 b, rgba color = colors::green, f32 duration = 0.0f);

/// Mũi tên 2D từ `from` tới `to`.
/// @param ctx Context của engine.
/// @param from Gốc.
/// @param to Mũi.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_arrow(njin_ctx &ctx, vec2 from, vec2 to, rgba color = colors::green, f32 duration = 0.0f);

/// Khung hình chữ nhật 2D.
/// @param ctx Context của engine.
/// @param r Hình chữ nhật, tọa độ thế giới.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_rect(njin_ctx &ctx, rect r, rgba color = colors::green, f32 duration = 0.0f);

/// Đường tròn 2D.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param radius Bán kính.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_circle(njin_ctx &ctx, vec2 center, f32 radius, rgba color = colors::green, f32 duration = 0.0f);

/// Một điểm 2D: dấu chữ thập nhỏ, cỡ cố định trên màn hình.
/// @param ctx Context của engine.
/// @param p Vị trí.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_point(njin_ctx &ctx, vec2 p, rgba color = colors::green, f32 duration = 0.0f);

/// Nhãn chữ tại một điểm của thế giới 2D, cỡ cố định trên màn hình.
/// @param ctx Context của engine.
/// @param pos Vị trí góc trên trái của chữ.
/// @param text Chữ (UTF-8). nullptr bị bỏ qua.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_text(njin_ctx &ctx, vec2 pos, const char *text, rgba color = colors::white, f32 duration = 0.0f);

/// Đoạn thẳng 3D.
/// @param ctx Context của engine.
/// @param a Điểm đầu.
/// @param b Điểm cuối.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_line3d(njin_ctx &ctx, vec3 a, vec3 b, rgba color = colors::green, f32 duration = 0.0f);

/// Mũi tên 3D từ `from` tới `to`.
/// @param ctx Context của engine.
/// @param from Gốc.
/// @param to Mũi.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_arrow3d(njin_ctx &ctx, vec3 from, vec3 to, rgba color = colors::green, f32 duration = 0.0f);

/// Khung hộp 3D, các cạnh song song với trục.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param size Kích thước theo x, y, z.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_box3d(njin_ctx &ctx, vec3 center, vec3 size, rgba color = colors::green, f32 duration = 0.0f);

/// Khung cầu 3D: ba đường tròn lớn.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param radius Bán kính.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_sphere3d(njin_ctx &ctx, vec3 center, f32 radius, rgba color = colors::green, f32 duration = 0.0f);

/// Ba trục tọa độ tại `pos`: x đỏ, y xanh lá, z xanh dương.
/// @param ctx Context của engine.
/// @param pos Gốc.
/// @param size Độ dài mỗi trục.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_axes3d(njin_ctx &ctx, vec3 pos, f32 size = 1.0f, f32 duration = 0.0f);

/// Một điểm 3D: dấu chữ thập nhỏ, cỡ cố định trên màn hình.
/// @param ctx Context của engine.
/// @param p Vị trí.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_point3d(njin_ctx &ctx, vec3 p, rgba color = colors::green, f32 duration = 0.0f);

/// Nhãn chữ gắn vào một điểm 3D, cỡ cố định trên màn hình. Không hiện khi điểm ở
/// sau camera.
/// @param ctx Context của engine.
/// @param pos Vị trí.
/// @param text Chữ (UTF-8). nullptr bị bỏ qua.
/// @param color Màu.
/// @param duration Số giây còn hiện. 0 là chỉ frame này.
void gizmo_text3d(njin_ctx &ctx, vec3 pos, const char *text, rgba color = colors::white, f32 duration = 0.0f);

/// Bật hay tắt việc vẽ gizmo. Mặc định bật. Khi tắt, các lệnh `gizmo_*` gần như
/// không tốn gì; dùng để có một phím bật tắt, hoặc tắt trong bản phát hành.
/// @param ctx Context của engine.
/// @param visible `true` để vẽ.
void gizmos_set_visible(njin_ctx &ctx, bool visible);

/// Gizmo có đang được vẽ không.
/// @param ctx Context của engine.
/// @return Giá trị đặt bởi gizmos_set_visible().
bool gizmos_visible(const njin_ctx &ctx);
/// @}
/// @}
} // namespace njin
