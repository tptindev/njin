#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace njin {
struct context;

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
/// (dừng, chạy từng frame, tua chậm) và quay cửa sổ game thành file GIF (xem @ref debug_recording).
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
///
/// **Log:** khi một inspector đang kết nối, log đi sang inspector và **không in ra
/// console của game** nữa; chưa có inspector (hoặc nó đã ngắt) thì log vẫn ra
/// console như thường. Các dòng đã ra trước đó, kể cả lúc mở cửa sổ (tối đa 2000
/// dòng, chỉ tính từ create()), được gửi cho inspector ngay khi nó kết nối.
/// @param ctx Context của engine.
/// @param desc Cổng và nhịp gửi.
/// @return `false` nếu cổng đang bị chiếm (ví dụ một bản game khác đang chạy).
bool debug_server_start(context &ctx, const debug_server_desc &desc = {});

/// Đóng cổng debug và ngắt inspector đang kết nối. @param ctx Context của engine.
void debug_server_stop(context &ctx);

/// Có inspector nào đang kết nối không. @param ctx Context của engine.
/// @return `true` nếu có.
bool debug_server_connected(const context &ctx);

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
void debug_watch(context &ctx, const char *name, json_value value);

/// Như debug_watch() cho một vec2. @param ctx Context của engine.
/// @param name Tên hiển thị. @param value Giá trị.
void debug_watch(context &ctx, const char *name, vec2 value);

/// Một tài liệu trong danh sách của một nguồn (debug_doc_source()): định danh
/// để hỏi nội dung, và tên hiện trong danh sách.
struct debug_doc_entry {
  std::string id;    ///< Định danh, game tự đặt (chỉ số, mã...).
  std::string label; ///< Tên hiện trong danh sách của inspector.
};

/// Đăng ký một nguồn tài liệu văn bản cho cửa sổ "Documents" của inspector:
/// sổ tay của từng NPC, nhật ký nhiệm vụ, bất cứ thứ gì game muốn đọc như một
/// trang chữ (markdown được hiện nhẹ: tiêu đề, gạch đầu dòng). Inspector chỉ
/// hỏi khi cần: danh sách khi mở nguồn, nội dung khi chọn một tài liệu, rồi
/// hỏi lại vài giây một lần khi tài liệu còn mở. Hai hàm chạy trên luồng của
/// game, lúc engine đọc lệnh của inspector. Đăng ký lại cùng tên thì thay
/// nguồn cũ. Không có inspector thì không làm gì.
/// @code
/// njin::debug_doc_source(ctx, "Quests",
///   [] { return std::vector<njin::debug_doc_entry>{{"1", "Tìm chìa khoá"}}; },
///   [](const std::string &id) { return "# Nhiệm vụ " + id + "\n\n- Tìm chìa khoá ở hầm."; });
/// @endcode
/// @param ctx Context của engine.
/// @param name Tên nguồn, hiện trong inspector.
/// @param list Hàm trả danh sách tài liệu.
/// @param get Hàm trả nội dung một tài liệu theo định danh.
void debug_doc_source(context &ctx, const char *name, std::function<std::vector<debug_doc_entry>()> list,
                      std::function<std::string(const std::string &id)> get);

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
void debug_component(context &ctx, entt::id_type type, const char *name, debug_component_fn fn,
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
void debug_component(context &ctx, const char *name, Fn fn) {
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
  u32 post_passes = 0;     ///< Số pass toàn màn hình của hậu kỳ dựng sẵn và của ánh sáng.
  u32 lights = 0;          ///< Đèn đã vẽ vào bản đồ ánh sáng (njin_light.h).
  u32 models3d = 0;        ///< Model 3D đã vẽ (draw_model(), njin::model3d).
  u32 models3d_culled = 0; ///< Model 3D bị bỏ qua vì nằm ngoài tầm nhìn camera (vẫn đổ bóng).
  /// Vật 3D (model, hình khối, hình SDF) bị bỏ vì vật khác che hẳn
  /// (render3d_options::occlusion; vẫn đổ bóng).
  u32 occluded3d = 0;
  u32 instances3d = 0;        ///< Instance đã vẽ bởi draw_instanced3d() với njin::instanced3d_desc.
  u32 instances3d_culled = 0; ///< Instance mà lệnh đó bỏ vì ngoài tầm nhìn hay bị che.
  u32 batches3d = 0;          ///< Lệnh instanced do render3d_options::batch gom (lần vẽ của camera).
  u32 batched3d = 0;          ///< Lệnh vẽ 3D đã gom vào các lệnh đó.
};

/// Số liệu vẽ của frame vừa rồi. Đọc trong `phase_post_render` (hoặc frame sau)
/// thì có số của cả frame; đọc trước đó thì thiếu phần chưa vẽ.
/// @param ctx Context của engine.
/// @return Số liệu.
render_info render_info_get(const context &ctx);
/// @}
} // namespace njin
