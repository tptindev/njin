#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>
#include <functional>
#include <type_traits>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_debug
/// @{

/// Cách mở cổng debug, dùng với debug_server_start().
struct debug_server_desc {
  u16 port = 7779;           ///< Cổng TCP, chỉ nghe trên 127.0.0.1.
  f32 snapshot_hz = 10.0f;   ///< Số lần mỗi giây gửi danh sách entity và giá trị theo dõi.
  i32 max_entities = 4000;   ///< Số entity tối đa trong mỗi lần gửi. Thừa thì cắt, inspector có báo.
};

/// Mở cổng debug để chương trình **njin_inspector** (một process riêng) kết
/// nối vào: FPS, biểu đồ thời gian frame, danh sách entity và component, khung
/// collider vẽ trên bản đồ, log, giá trị theo dõi, và điều khiển thời gian
/// (dừng, chạy từng frame, tua chậm).
///
/// Game **không vẽ gì thêm**: mọi giao diện debug nằm ở cửa sổ inspector. Chỉ
/// nghe trên 127.0.0.1, nên máy khác không kết nối được; không có inspector nào
/// kết nối thì chi phí gần như bằng không. Không bao giờ chặn game: inspector
/// chậm thì dữ liệu bị bỏ bớt chứ game không đứng.
///
/// Thường chỉ bật trong bản debug:
/// @code
/// #ifndef NDEBUG
///   njin::debug_server_start(*ctx);
/// #endif
/// @endcode
/// @param ctx Context của engine.
/// @param desc Cổng và nhịp gửi.
/// @return `false` nếu cổng đang bị chiếm (ví dụ một bản game khác đang chạy).
bool debug_server_start(njin_ctx &ctx, const debug_server_desc &desc = {});

/// Đóng cổng debug và ngắt inspector đang kết nối. @param ctx Context của engine.
void debug_server_stop(njin_ctx &ctx);

/// Có inspector nào đang kết nối không. @param ctx Context của engine.
/// @return `true` nếu có.
bool debug_server_connected(const njin_ctx &ctx);

/// Đặt một giá trị để xem trực tiếp trong bảng "Watches" của inspector: vận
/// tốc nhân vật, trạng thái AI, số quái còn sống. Gọi mỗi frame hay mỗi khi
/// đổi; giá trị cuối cùng được gửi theo nhịp `snapshot_hz`. Không có inspector
/// thì không làm gì.
/// @code
/// njin::debug_watch(ctx, "player.velocity", vel);
/// njin::debug_watch(ctx, "enemies alive", (njin::i32)enemies.size());
/// @endcode
/// @param ctx Context của engine.
/// @param name Tên hiển thị.
/// @param value Giá trị: số, bool, chuỗi, hoặc njin::json_value bất kỳ.
void debug_watch(njin_ctx &ctx, const char *name, json_value value);

/// Như debug_watch() cho một vec2. @param ctx Context của engine.
/// @param name Tên hiển thị. @param value Giá trị.
void debug_watch(njin_ctx &ctx, const char *name, vec2 value);

/// Hàm chuyển một component thành JSON để inspector hiện giá trị của nó.
using debug_component_fn = std::function<json_value(const entt::registry &, entt::entity)>;

/// Đăng ký cách hiện một loại component. Engine đã có sẵn cho mọi component
/// của njin. Component của game không đăng ký vẫn hiện tên trong inspector,
/// chỉ không có giá trị.
/// @param ctx Context của engine.
/// @param type Định danh kiểu của EnTT, `entt::type_hash<T>::value()`.
/// @param name Tên hiển thị.
/// @param fn Hàm chuyển sang JSON.
/// @param bytes Kích thước một component (`sizeof`), để bảng Memory của
/// inspector tính bộ nhớ. 0 là chưa biết.
void debug_component(njin_ctx &ctx, entt::id_type type, const char *name, debug_component_fn fn,
                     std::size_t bytes = 0);

/// Đăng ký cách hiện component `T` của game bằng một hàm nhận `const T &`.
/// @code
/// struct health { njin::i32 hp = 3, max = 3; };
/// njin::debug_component<health>(ctx, "health", [](const health &h) {
///   return njin::json_value::make_object().set("hp", h.hp).set("max", h.max);
/// });
/// @endcode
/// @tparam T Kiểu component.
/// @tparam Fn Hàm hoặc lambda `json_value(const T &)`.
/// @param ctx Context của engine.
/// @param name Tên hiển thị.
/// @param fn Hàm chuyển sang JSON.
template <class T, class Fn>
void debug_component(njin_ctx &ctx, const char *name, Fn fn) {
  debug_component(ctx, entt::type_hash<T>::value(), name,
                  [fn](const entt::registry &reg, entt::entity e) -> json_value {
                    if constexpr (std::is_empty_v<T>) {
                      return json_value::make_object(); // a tag has no fields
                    } else {
                      return fn(reg.get<T>(e));
                    }
                  },
                  std::is_empty_v<T> ? 0 : sizeof(T));
}

/// Những gì frame vừa rồi đã vẽ, cho màn hình debug của chính game. njin_inspector
/// hiện đúng các số này ở cửa sổ Performance.
struct render_info {
  u32 sprites = 0;         ///< Sprite đã vẽ.
  u32 sprites_culled = 0;  ///< Sprite bị bỏ qua vì nằm ngoài camera.
  u32 tile_chunks = 0;     ///< Chunk tilemap đã vẽ.
  u32 emitters = 0;        ///< Emitter hạt đã vẽ.
  u32 emitters_culled = 0; ///< Emitter bị bỏ qua vì nằm ngoài camera.
  u32 particles = 0;       ///< Hạt đã vẽ, cả CPU và GPU.
  u32 particles_gpu = 0;   ///< Trong đó vẽ bằng GPU.
  u32 instanced_calls = 0; ///< Lệnh vẽ instanced (mỗi emitter GPU một lệnh).
  /// Số lệnh vẽ **ước tính**. Raylib không báo số thật, nên con số này đếm các
  /// lần đổi texture hoặc blend mode, và mỗi lệnh instanced. Không tính UI và chữ.
  u32 draw_calls = 0;
  u32 post_passes = 0;     ///< Số pass toàn màn hình của hậu kỳ dựng sẵn.
};

/// Số liệu vẽ của frame vừa rồi. Đọc trong `phase_post_render` (hoặc frame sau)
/// thì có số của cả frame; đọc trước đó thì thiếu phần chưa vẽ.
/// @param ctx Context của engine.
/// @return Số liệu.
render_info render_info_get(const njin_ctx &ctx);
/// @}
} // namespace njin
