#pragma once
#include "_math.h"
#include "_types.h"
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_nav3d
/// @{

/// Cách dựng một navmesh 3D bằng navmesh3d_create(): cỡ của tác tử đi trên nó và
/// độ mịn khi dựng.
///
/// Navmesh là các đa giác phủ chỗ đi được của hình học tĩnh (sàn, dốc, bậc thang,
/// địa hình), đã chừa ra một khoảng `agent_radius` quanh tường và vật cản. Nó chia
/// thành các ô vuông cạnh `tile_size` mét, nên dựng lại được một vùng
/// (navmesh3d_rebuild()) mà không phải dựng lại cả bản đồ.
///
/// Điểm của đường đi và vị trí tác tử nằm trên mặt đất: độ cao lấy theo lưới chi tiết
/// của navmesh, và trên địa hình đã thêm bằng navmesh3d_add_terrain() thì theo đúng
/// terrain3d_height(). Trên hình học khác (model, hộp nghiêng) có thể sai vài cm.
struct navmesh3d_desc {
  f32 agent_radius = 0.4f;  ///< Bán kính tác tử, mét: chỗ đi được cách tường bấy nhiêu.
  f32 agent_height = 1.8f;  ///< Chiều cao tác tử: chui được qua chỗ trần cao hơn số này.
  f32 agent_climb = 0.4f;   ///< Bậc cao nhất bước lên được, mét.
  f32 max_slope = 45.0f;    ///< Dốc nhất đi được, độ.
  /// Cỡ ô khi dựng, mét, theo chiều ngang: nhỏ thì mép đa giác sát tường hơn nhưng
  /// dựng lâu hơn. Khoảng một phần ba `agent_radius` là thường.
  f32 cell_size = 0.15f;
  f32 cell_height = 0.1f;   ///< Cỡ ô khi dựng theo chiều đứng, mét.
  f32 tile_size = 16.0f;    ///< Cạnh mỗi ô vuông của navmesh, mét.
  f32 region_min = 8.0f;    ///< Mảng đi được nhỏ hơn (tính bằng ô, theo cạnh) bị bỏ: chỗ đứng lọt thỏm trên bàn.
  f32 edge_max_error = 1.3f; ///< Mép đa giác lệch khỏi mép thật tối đa bấy nhiêu ô.
  /// Góc (x, y, z nhỏ nhất) và góc kia của vùng dựng. Hai góc bằng nhau (mặc định)
  /// là lấy theo hình học đã thêm khi dựng lần đầu.
  vec3 bounds_min{0.0f, 0.0f, 0.0f};
  vec3 bounds_max{0.0f, 0.0f, 0.0f}; ///< Xem `bounds_min`.
  i32 max_agents = 128;     ///< Số tác tử tối đa đi cùng lúc (nav3d_agent_add()).
};

/// Tạo một navmesh trống. Thêm hình học bằng navmesh3d_add_mesh(),
/// navmesh3d_add_model(), navmesh3d_add_box(), navmesh3d_add_terrain(), rồi dựng bằng
/// navmesh3d_build().
/// @param ctx Context của engine.
/// @param desc Cách dựng.
/// @return Handle, hoặc handle không hợp lệ nếu `desc` sai (cảnh báo nói vì sao).
navmesh3d_handle navmesh3d_create(context &ctx, const navmesh3d_desc &desc = {});

/// Hủy navmesh cùng mọi tác tử của nó. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Navmesh.
void navmesh3d_destroy(context &ctx, navmesh3d_handle handle);

/// Thêm một lưới tam giác (trong thế giới) làm hình học: sàn, tường, cầu thang.
/// Được chép lại.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param positions Các đỉnh.
/// @param vertex_count Số đỉnh.
/// @param indices Ba chỉ số một tam giác; nullptr là mỗi ba đỉnh liên tiếp một tam giác.
/// @param index_count Số chỉ số, bội của 3.
void navmesh3d_add_mesh(context &ctx, navmesh3d_handle handle, const vec3 *positions, u32 vertex_count,
                        const u32 *indices = nullptr, u32 index_count = 0);

/// Thêm các tam giác của một model, đặt ở vị trí, góc xoay (độ) và tỉ lệ như
/// draw_model(). Model được đọc lại mỗi lần dựng, nên phải còn sống đến lúc đó.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param model Model.
/// @param position Vị trí.
/// @param rotation Góc xoay, độ, như njin::transform3d::rotation.
/// @param scale Tỉ lệ.
void navmesh3d_add_model(context &ctx, navmesh3d_handle handle, model_handle model, vec3 position,
                         vec3 rotation = {0.0f, 0.0f, 0.0f}, vec3 scale = {1.0f, 1.0f, 1.0f});

/// Thêm một hình hộp: bục, bàn, vật cản, tường.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param center Tâm.
/// @param size Cỡ.
/// @param rotation Góc xoay, độ.
void navmesh3d_add_box(context &ctx, navmesh3d_handle handle, vec3 center, vec3 size,
                       vec3 rotation = {0.0f, 0.0f, 0.0f});

/// Thêm một địa hình (njin_world3d.h). Độ cao được đọc lại mỗi lần dựng, nên sau
/// terrain3d_edit() chỉ cần navmesh3d_rebuild() vùng đã sửa.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param terrain Địa hình.
void navmesh3d_add_terrain(context &ctx, navmesh3d_handle handle, terrain3d_handle terrain);

/// Thêm một lối tắt mà tác tử đi được dù không liền mặt đất: nhảy qua khe, leo
/// thang, nhảy xuống bục. Hai đầu phải nằm trên navmesh (cách nó không quá
/// `radius`).
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param from Đầu đi.
/// @param to Đầu đến.
/// @param both_ways Đi được cả chiều ngược lại.
/// @param radius Khoảng tìm navmesh quanh hai đầu, mét.
void navmesh3d_add_link(context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, bool both_ways = true,
                        f32 radius = 0.5f);

/// Bỏ mọi hình học và lối tắt đã thêm (navmesh đã dựng vẫn giữ đến lần dựng sau).
/// @param ctx Context của engine.
/// @param handle Navmesh.
void navmesh3d_clear_geometry(context &ctx, navmesh3d_handle handle);

/// Dựng toàn bộ navmesh từ hình học đã thêm. Mất từ vài mili giây đến vài giây tùy
/// cỡ bản đồ và `cell_size`: gọi lúc nạp màn, không phải mỗi frame.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @return `true` nếu có ít nhất một chỗ đi được.
bool navmesh3d_build(context &ctx, navmesh3d_handle handle);

/// Dựng lại các ô vuông chạm vào vùng `[min, max]`, từ hình học hiện tại: sau khi
/// sửa địa hình, mở cửa, đặt vật cản mới (thêm nó bằng navmesh3d_add_box()). Tác
/// tử đang đi tự tìm lại đường.
/// @param ctx Context của engine.
/// @param handle Navmesh đã dựng.
/// @param min Góc nhỏ của vùng.
/// @param max Góc lớn của vùng.
/// @return Số ô vuông đã dựng lại.
i32 navmesh3d_rebuild(context &ctx, navmesh3d_handle handle, vec3 min, vec3 max);

/// Tìm đường từ `from` đến `to`: các điểm gấp khúc, đầu là điểm gần `from` nhất
/// trên navmesh, cuối là điểm gần `to` nhất. Không đến được `to` thì đường dừng ở
/// chỗ gần nó nhất mà đến được (so điểm cuối với `to` để biết). Trên địa hình, đoạn
/// nào cắt qua đồi thì có thêm điểm giữa để đường bám mặt đất (cách không quá 5 cm).
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param from Điểm đi.
/// @param to Điểm đến.
/// @param out Nhận các điểm (được xóa trước).
/// @return `true` nếu tìm được đường (dù chỉ đến gần `to`); `false` nếu `from` hay
/// `to` không gần navmesh.
bool navmesh3d_path(const context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, std::vector<vec3> &out);

/// Điểm gần `p` nhất trên navmesh, tìm trong hộp nửa cỡ `extents` quanh `p`.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param p Điểm.
/// @param out Nhận điểm tìm được.
/// @param extents Nửa cỡ hộp tìm, mét.
/// @return `true` nếu có.
bool navmesh3d_nearest(const context &ctx, navmesh3d_handle handle, vec3 p, vec3 &out,
                       vec3 extents = {2.0f, 4.0f, 2.0f});

/// Kết quả của navmesh3d_raycast().
struct nav3d_ray {
  bool hit = false;   ///< Gặp mép navmesh (tường) trước khi tới đích.
  vec3 point{};       ///< Chỗ dừng: chỗ gặp mép, hoặc đích nếu không gặp.
  vec3 normal{};      ///< Pháp tuyến ngang của mép gặp phải.
  f32 fraction = 1.0f; ///< Phần đường đi được, 0..1.
};

/// Đi thẳng trên mặt navmesh từ `from` về phía `to` và dừng ở mép đầu tiên: có nhìn
/// thấy nhau theo mặt đất không, lao tới được bao xa.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param from Điểm đi (gần navmesh).
/// @param to Điểm đến.
/// @return Kết quả; `point` của lần không trúng là `to` chiếu lên navmesh.
nav3d_ray navmesh3d_raycast(const context &ctx, navmesh3d_handle handle, vec3 from, vec3 to);

/// Một điểm ngẫu nhiên trên navmesh (các chỗ đi được có xác suất theo diện tích).
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param out Nhận điểm.
/// @return `true` nếu có.
bool navmesh3d_random_point(context &ctx, navmesh3d_handle handle, vec3 &out);

/// Một điểm ngẫu nhiên trên navmesh, cách `center` không quá `radius` và đi tới được
/// từ đó: chỗ quái đi lang thang quanh ổ.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param center Tâm.
/// @param radius Bán kính, mét.
/// @param out Nhận điểm.
/// @return `true` nếu có.
bool navmesh3d_random_point_near(context &ctx, navmesh3d_handle handle, vec3 center, f32 radius, vec3 &out);

/// Vẽ các đa giác của navmesh bằng gizmo (njin_gizmo.h) trong frame này: mép đa
/// giác, mép ngoài đậm hơn, và các lối tắt.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param color Màu.
void navmesh3d_draw_debug(context &ctx, navmesh3d_handle handle, rgba color = {0.2f, 0.8f, 1.0f, 1.0f});

/// Một tác tử đi trên navmesh cùng các tác tử khác: tự tìm đường, tránh nhau, tách
/// nhau ra khi đứng sát.
struct nav3d_agent_desc {
  vec3 position{};          ///< Chỗ đặt (được kéo về navmesh).
  f32 radius = 0.4f;        ///< Bán kính, mét.
  f32 height = 1.8f;        ///< Chiều cao, mét.
  f32 max_speed = 3.5f;     ///< Tốc độ tối đa, mét mỗi giây.
  f32 max_accel = 10.0f;    ///< Gia tốc tối đa, mét mỗi giây bình phương.
  f32 separation = 2.0f;    ///< Độ mạnh của việc tách khỏi tác tử bên cạnh. 0 là tắt.
  bool avoid = true;        ///< Né tác tử khác trên đường đi.
  /// Tới cách đích bấy nhiêu mét là đã đến (nav3d_agent_arrived()). 0 là bằng `radius`.
  f32 arrive_distance = 0.0f;
  /// Nhân vật (njin_physics3d.h) do tác tử này lái: mỗi frame vận tốc của tác tử
  /// thành vận tốc ngang của nhân vật (character3d_set_velocity(); engine tự cho nó
  /// rơi theo physics3d_gravity() khi không đứng trên sàn), và chỗ nhân vật thật sự
  /// đến (bị đẩy, va chạm) thành chỗ của tác tử. Game đừng đặt vận tốc cho nhân vật
  /// này nữa. Không hợp lệ là tác tử tự đi, không có va chạm vật lý.
  character3d_handle character{};
};

/// Thêm một tác tử. Nó đứng yên đến khi có đích (nav3d_agent_set_target()). Mọi tác
/// tử được engine cập nhật trong `phase_post_update` theo delta().
/// @param ctx Context của engine.
/// @param navmesh Navmesh đã dựng.
/// @param desc Tác tử.
/// @return Handle, hoặc handle không hợp lệ nếu navmesh chưa dựng, đã đủ
/// `max_agents`, hay `position` không gần navmesh.
nav3d_agent_handle nav3d_agent_add(context &ctx, navmesh3d_handle navmesh, const nav3d_agent_desc &desc);

/// Bỏ một tác tử. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param agent Tác tử.
void nav3d_agent_remove(context &ctx, nav3d_agent_handle agent);

/// Đặt đích: tác tử tìm đường và đi tới đó (đích được kéo về chỗ gần nhất trên
/// navmesh). Gọi lại khi đích di chuyển (đuổi theo người chơi) là đủ, không tốn gì
/// thêm khi đích đổi ít.
/// @param ctx Context của engine.
/// @param agent Tác tử.
/// @param target Đích.
/// @return `false` nếu đích không gần navmesh.
bool nav3d_agent_set_target(context &ctx, nav3d_agent_handle agent, vec3 target);

/// Bỏ đích: tác tử dừng lại (vẫn tách khỏi tác tử khác).
/// @param ctx Context của engine.
/// @param agent Tác tử.
void nav3d_agent_stop(context &ctx, nav3d_agent_handle agent);

/// Đặt tác tử sang chỗ khác ngay (dịch chuyển tức thời), bỏ đích.
/// @param ctx Context của engine.
/// @param agent Tác tử.
/// @param position Chỗ mới (được kéo về navmesh).
/// @return `false` nếu chỗ đó không gần navmesh.
bool nav3d_agent_teleport(context &ctx, nav3d_agent_handle agent, vec3 position);

/// Vị trí hiện tại của tác tử (trên mặt navmesh).
/// @param ctx Context của engine.
/// @param agent Tác tử.
/// @return Vị trí, hoặc (0, 0, 0) nếu handle không hợp lệ.
vec3 nav3d_agent_position(const context &ctx, nav3d_agent_handle agent);

/// Vận tốc hiện tại của tác tử: hướng nó đang đi, để quay mặt và chọn animation.
/// @param ctx Context của engine.
/// @param agent Tác tử.
/// @return Vận tốc, mét mỗi giây.
vec3 nav3d_agent_velocity(const context &ctx, nav3d_agent_handle agent);

/// Tác tử đã tới đích chưa (cách đích không quá `arrive_distance`).
/// @param ctx Context của engine.
/// @param agent Tác tử.
/// @return `true` nếu đã tới, `false` nếu đang đi hay không có đích.
bool nav3d_agent_arrived(const context &ctx, nav3d_agent_handle agent);

/// Số tác tử đang có trên một navmesh.
/// @param ctx Context của engine.
/// @param navmesh Navmesh.
/// @return Số tác tử.
i32 nav3d_agent_count(const context &ctx, navmesh3d_handle navmesh);
/// @}
} // namespace njin
