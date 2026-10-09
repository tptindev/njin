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
  /// Số ô vuông tối đa được dựng lại mỗi frame khi vật cản (navmesh3d_add_obstacle())
  /// thêm, dời hay bỏ. Ô còn lại chờ frame sau, nên một vật cản lớn rải việc ra vài frame.
  i32 obstacle_tiles_per_frame = 4;
};

/// Số loại vùng: vùng 0 đến 15. Vùng 0 là mặt đất thường, mọi chỗ chưa đánh dấu.
/// Số còn lại game tự đặt nghĩa (đường, cỏ, đầm lầy, nước nông, cửa...).
constexpr i32 nav3d_max_areas = 16;

/// Cách tìm đường đánh giá các vùng: chi phí đi qua mỗi vùng và vùng nào cấm.
///
/// Đường đi chọn tổng (quãng đường × chi phí) nhỏ nhất: chi phí 1 là bình thường, 4
/// là đi một mét ở đó tốn như bốn mét đường thường (đường vòng qua đường cái thay vì
/// lội đầm). Vùng có bit trong `excluded` không bao giờ được đi qua (cửa khóa). Đổi
/// bộ lọc không cần dựng lại navmesh.
struct nav3d_filter {
  /// Chi phí mỗi mét của từng vùng, chỉ số là số vùng. Phải lớn hơn 0.
  f32 cost[nav3d_max_areas]{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                            1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  /// Các vùng cấm, bit `1 << vùng`. 0 (mặc định) là không cấm vùng nào.
  u16 excluded = 0;
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
/// @param area Vùng của mặt đi được trên lưới này, 0..15 (xem nav3d_filter). Khi hai
/// mặt trùng độ cao, vùng số lớn hơn thắng.
void navmesh3d_add_mesh(context &ctx, navmesh3d_handle handle, const vec3 *positions, u32 vertex_count,
                        const u32 *indices = nullptr, u32 index_count = 0, u8 area = 0);

/// Thêm các tam giác của một model, đặt ở vị trí, góc xoay (độ) và tỉ lệ như
/// draw_model(). Model được đọc lại mỗi lần dựng, nên phải còn sống đến lúc đó.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param model Model.
/// @param position Vị trí.
/// @param rotation Góc xoay, độ, như njin::transform3d::rotation.
/// @param scale Tỉ lệ.
/// @param area Vùng của mặt đi được trên model, 0..15.
void navmesh3d_add_model(context &ctx, navmesh3d_handle handle, model_handle model, vec3 position,
                         vec3 rotation = {0.0f, 0.0f, 0.0f}, vec3 scale = {1.0f, 1.0f, 1.0f}, u8 area = 0);

/// Thêm một hình hộp: bục, bàn, vật cản, tường.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param center Tâm.
/// @param size Cỡ.
/// @param rotation Góc xoay, độ.
/// @param area Vùng của mặt trên hộp, 0..15.
void navmesh3d_add_box(context &ctx, navmesh3d_handle handle, vec3 center, vec3 size,
                       vec3 rotation = {0.0f, 0.0f, 0.0f}, u8 area = 0);

/// Thêm một địa hình (njin_world3d.h). Độ cao được đọc lại mỗi lần dựng, nên sau
/// terrain3d_edit() chỉ cần navmesh3d_rebuild() vùng đã sửa.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param terrain Địa hình.
/// @param area Vùng của mặt địa hình, 0..15 (từng khoảng riêng thì đánh dấu bằng
/// navmesh3d_add_area()).
void navmesh3d_add_terrain(context &ctx, navmesh3d_handle handle, terrain3d_handle terrain, u8 area = 0);

/// Thêm một lối tắt mà tác tử đi được dù không liền mặt đất: nhảy qua khe, leo
/// thang, nhảy xuống bục. Hai đầu phải nằm trên navmesh (cách nó không quá
/// `radius`).
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param from Đầu đi.
/// @param to Đầu đến.
/// @param both_ways Đi được cả chiều ngược lại.
/// @param radius Khoảng tìm navmesh quanh hai đầu, mét.
/// @param area Vùng của lối tắt, 0..15: cho nó chi phí riêng (nhảy thì đắt) hay cấm nó.
void navmesh3d_add_link(context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, bool both_ways = true,
                        f32 radius = 0.5f, u8 area = 0);

/// Đánh dấu một khối đứng là một vùng (0..15): mọi chỗ đi được bên trong hộp xoay quanh
/// trục đứng (đường cái, đầm lầy, khung cửa). Khối đánh dấu sau thắng khối trước chỗ
/// chồng nhau. Nếu navmesh đã dựng, các ô vuông khối chạm vào được dựng lại ngay.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param center Tâm khối.
/// @param size Cỡ khối: `x` và `z` theo mặt đất, `y` là tầm cao phủ tới.
/// @param yaw Góc xoay quanh trục đứng, độ.
/// @param area Vùng, 0..15.
/// @return Số của khối, để bỏ bằng navmesh3d_remove_area(); 0 nếu từ chối.
i32 navmesh3d_add_area(context &ctx, navmesh3d_handle handle, vec3 center, vec3 size, f32 yaw, u8 area);

/// Bỏ một khối đã đánh dấu (chỗ đó trở về vùng của hình học bên dưới) và dựng lại các ô
/// vuông nó chạm vào. Số không có bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param id Số do navmesh3d_add_area() trả về.
void navmesh3d_remove_area(context &ctx, navmesh3d_handle handle, i32 id);

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

/// Một navmesh mới cùng hình học, vùng đánh dấu, lối tắt, vật cản và bộ lọc với
/// `source`, nhưng dựng theo `desc` (thường là cỡ tác tử khác): mỗi cỡ tác tử cần
/// navmesh riêng, vì khoảng chừa quanh tường được tính lúc dựng. Chưa dựng: gọi
/// navmesh3d_build(). Thêm hình học, vùng hay vật cản về sau thì thêm cho từng navmesh.
/// @code
/// const auto small = njin::navmesh3d_create(ctx, {.agent_radius = 0.3f});
/// // ... thêm hình học vào small ...
/// const auto large = njin::navmesh3d_clone(ctx, small, {.agent_radius = 1.0f, .agent_height = 3.0f});
/// njin::navmesh3d_build(ctx, small);
/// njin::navmesh3d_build(ctx, large);
/// @endcode
/// @param ctx Context của engine.
/// @param source Navmesh để chép hình học.
/// @param desc Cách dựng navmesh mới.
/// @return Handle mới, hoặc handle không hợp lệ nếu `source` không có hay `desc` sai.
navmesh3d_handle navmesh3d_clone(context &ctx, navmesh3d_handle source, const navmesh3d_desc &desc);

/// Đặt bộ lọc số `index` (0..15) của navmesh. Bộ lọc 0 là bộ lọc mặc định: mọi hàm tìm
/// đường không chọn bộ lọc và mọi tác tử không đặt `filter` dùng nó. Tác tử đang đi
/// theo bộ lọc này tìm lại đường ngay. Không cần dựng lại navmesh.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param index Số bộ lọc, 0..15.
/// @param filter Bộ lọc.
void navmesh3d_set_filter(context &ctx, navmesh3d_handle handle, i32 index, const nav3d_filter &filter);

/// Bộ lọc số `index` của navmesh.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param index Số bộ lọc, 0..15.
/// @return Bộ lọc, hoặc bộ lọc mặc định nếu handle hay số không hợp lệ.
nav3d_filter navmesh3d_filter(const context &ctx, navmesh3d_handle handle, i32 index);

/// Một vật cản di động trên navmesh: thùng bị đẩy, xe đậu, cửa đóng. Chỗ nó đứng không
/// còn đi được (cách nó một khoảng `agent_radius`), mà không cần thêm hình học.
struct nav3d_obstacle_desc {
  vec3 position{};               ///< Tâm.
  vec3 size{1.0f, 2.0f, 1.0f};   ///< Cỡ hộp; với hình trụ chỉ dùng `y` (chiều cao).
  f32 yaw = 0.0f;                ///< Góc xoay quanh trục đứng, độ.
  f32 radius = 0.0f;             ///< Lớn hơn 0 là hình trụ bán kính này thay vì hộp.
};

/// Thêm một vật cản. Các ô vuông nó chạm vào được dựng lại ở các frame sau (tối đa
/// `obstacle_tiles_per_frame` ô mỗi frame), rồi tác tử tìm đường vòng qua nó.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param desc Vật cản.
/// @return Số của vật cản, 0 nếu từ chối.
i32 navmesh3d_add_obstacle(context &ctx, navmesh3d_handle handle, const nav3d_obstacle_desc &desc);

/// Dời một vật cản: dựng lại các ô vuông chỗ cũ và chỗ mới.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param id Số do navmesh3d_add_obstacle() trả về.
/// @param position Tâm mới.
/// @param yaw Góc xoay mới, độ.
/// @return `false` nếu không có vật cản này.
bool navmesh3d_move_obstacle(context &ctx, navmesh3d_handle handle, i32 id, vec3 position, f32 yaw = 0.0f);

/// Bỏ một vật cản: chỗ đó đi được lại sau khi các ô vuông được dựng lại.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param id Số do navmesh3d_add_obstacle() trả về.
void navmesh3d_remove_obstacle(context &ctx, navmesh3d_handle handle, i32 id);

/// Số ô vuông còn chờ dựng lại vì vật cản. 0 là navmesh đã khớp với mọi vật cản.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @return Số ô vuông.
i32 navmesh3d_pending_tiles(const context &ctx, navmesh3d_handle handle);

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

/// Như bản trên, theo bộ lọc số `filter` (navmesh3d_set_filter()): chi phí từng vùng,
/// vùng cấm.
/// @param ctx Context của engine.
/// @param handle Navmesh.
/// @param from Điểm đi.
/// @param to Điểm đến.
/// @param out Nhận các điểm (được xóa trước).
/// @param filter Số bộ lọc, 0..15.
/// @return Như bản trên.
bool navmesh3d_path(const context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, std::vector<vec3> &out,
                    i32 filter);

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
  /// Số bộ lọc (navmesh3d_set_filter()) tác tử tìm đường theo, 0..15.
  i32 filter = 0;
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

/// Đổi bộ lọc tác tử tìm đường theo (người lính có chìa khóa đi được qua cửa khóa).
/// Tác tử đang có đích tìm lại đường ngay.
/// @param ctx Context của engine.
/// @param agent Tác tử.
/// @param filter Số bộ lọc, 0..15.
void nav3d_agent_set_filter(context &ctx, nav3d_agent_handle agent, i32 filter);

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
