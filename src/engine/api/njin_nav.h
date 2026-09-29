#pragma once
#include "_math.h"
#include "_tilemap.h"
#include "_types.h"
#include <utility>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_nav
/// @{

/// Lưới tìm đường: mỗi ô có một chi phí để đi vào, 0 là không đi được.
///
/// Thường dựng một lần khi nạp level bằng nav_grid_from_world(), rồi sửa từng
/// ô khi cửa mở hay tường vỡ bằng nav_set_cost().
struct nav_grid {
  vec2 origin{};            ///< Góc trên trái của ô (0, 0) trong thế giới.
  vec2 cell_size{16.0f, 16.0f}; ///< Kích thước một ô, đơn vị thế giới.
  i32 width = 0;            ///< Số cột.
  i32 height = 0;           ///< Số hàng.
  /// Chi phí đi vào từng ô, theo hàng: `cost[y * width + x]`. 0 là vật cản,
  /// 1 là bình thường, lớn hơn là khó đi (bùn, nước nông): đường đi tránh nếu
  /// có đường vòng rẻ hơn.
  std::vector<u8> cost;
};

/// Tạo một lưới trống, mọi ô có cùng chi phí.
/// @param origin Góc trên trái trong thế giới.
/// @param cell_size Kích thước ô.
/// @param width Số cột.
/// @param height Số hàng.
/// @param cost Chi phí mọi ô. 1 là đi được, 0 là vật cản.
/// @return Lưới.
nav_grid nav_grid_make(vec2 origin, vec2 cell_size, i32 width, i32 height, u8 cost = 1);

/// Dựng lưới phủ `area` từ vật cản đang có trong thế giới: mọi ô chồng lên
/// một collider **không phải trigger** (hộp, tròn, hay ô `collider_tiles`
/// khác `tile_none`) có `layer & mask != 0` là vật cản.
///
/// Dùng `mask` để chỉ lấy tường và bỏ qua nhân vật, quái:
/// `nav_grid_from_world(ctx, level_bounds(ctx, lv), {16, 16}, layer_walls)`.
/// @param ctx Context của engine.
/// @param area Vùng thế giới cần phủ.
/// @param cell_size Kích thước ô, thường bằng ô tilemap.
/// @param mask Lớp va chạm được coi là vật cản.
/// @return Lưới.
nav_grid nav_grid_from_world(const context &ctx, rect area, vec2 cell_size, u32 mask = 0xFFFFFFFFu);

/// Ô chứa một điểm trong thế giới (có thể nằm ngoài lưới).
/// @param grid Lưới.
/// @param pos Điểm.
/// @return Ô.
cell nav_cell_at(const nav_grid &grid, vec2 pos);

/// Tâm của một ô trong thế giới.
/// @param grid Lưới.
/// @param c Ô.
/// @return Tâm ô.
vec2 nav_cell_center(const nav_grid &grid, cell c);

/// Chi phí của một ô. Ô ngoài lưới là 0.
/// @param grid Lưới.
/// @param c Ô.
/// @return Chi phí, 0 là vật cản.
u8 nav_cost(const nav_grid &grid, cell c);

/// Đặt chi phí một ô. Ô ngoài lưới bị bỏ qua.
/// @param grid Lưới.
/// @param c Ô.
/// @param cost Chi phí, 0 là vật cản.
void nav_set_cost(nav_grid &grid, cell c, u8 cost);

/// Đánh dấu vật cản mọi ô chồng lên `area`: một cái rương vừa đặt xuống, một
/// cánh cửa vừa đóng.
/// @param grid Lưới.
/// @param area Vùng thế giới.
/// @param cost Chi phí gán cho các ô đó, mặc định 0 (vật cản).
void nav_set_area(nav_grid &grid, rect area, u8 cost = 0);

/// Tùy chọn của nav_find_path().
struct nav_path_opts {
  /// Đi chéo được. Tắt cho game chỉ đi 4 hướng.
  bool diagonal = true;
  /// Đi chéo qua góc của vật cản. Tắt (mặc định) thì chỉ đi chéo khi cả hai ô
  /// bên cạnh đều trống, nên quái không cạ góc tường.
  bool cut_corners = false;
  /// Rút gọn đường: bỏ các điểm giữa khi đi thẳng được từ điểm trước tới điểm
  /// sau, nên quái đi xiên thay vì đi bậc thang theo ô.
  bool smooth = true;
  /// Khi không tới được đích: vẫn trả về đường tới ô gần đích nhất đã tìm
  /// thấy (hàm trả về `false`). Hợp với quái đuổi theo người chơi đứng trên
  /// chỗ không tới được.
  bool partial = true;
  /// Số ô tối đa được xét, để một đích không tới được trên bản đồ lớn không
  /// làm chậm frame.
  i32 max_nodes = 20000;
};

/// Tìm đường ngắn nhất (A*) từ `from` đến `to`.
///
/// `out` được xóa rồi nhận các điểm cần đi qua, trong thế giới, không kèm
/// điểm xuất phát; điểm cuối là `to` khi tới được. Dùng với nav_steer().
/// @code
/// std::vector<njin::vec2> path;
/// if (njin::nav_find_path(g.nav, enemy_pos, player_pos, path)) { ... }
/// @endcode
/// @param grid Lưới.
/// @param from Điểm xuất phát.
/// @param to Điểm đích.
/// @param out Nhận đường đi.
/// @param opts Tùy chọn.
/// @return `true` nếu tới được đích. `false` nếu không (khi đó `out` có thể
/// chứa đường tới chỗ gần nhất, xem nav_path_opts::partial).
bool nav_find_path(const nav_grid &grid, vec2 from, vec2 to, std::vector<vec2> &out,
                   const nav_path_opts &opts = {});

/// Đoạn thẳng từ `a` tới `b` có đi qua ô vật cản nào không.
/// @param grid Lưới.
/// @param a Điểm đầu.
/// @param b Điểm cuối.
/// @return `true` nếu mọi ô nó đi qua đều đi được.
bool nav_line_clear(const nav_grid &grid, vec2 a, vec2 b);

/// Một con đường đang được đi theo, dùng với nav_steer().
struct nav_agent {
  std::vector<vec2> path; ///< Các điểm cần đi qua, từ nav_find_path().
  i32 next = 0;           ///< Điểm đang đi tới.
  f32 reach = 3.0f;       ///< Cách điểm bao xa thì coi là đã tới.

  /// Đặt đường mới và đi lại từ điểm đầu. @param p Đường đi.
  void set(std::vector<vec2> p) {
    path = std::move(p);
    next = 0;
  }
  /// Đã đi hết đường chưa. @return `true` nếu không còn điểm nào.
  bool done() const { return next >= (i32)path.size(); }
};

/// Hướng cần đi để theo đường, độ dài 1, hoặc `{0, 0}` khi đã tới cuối.
/// Tự chuyển sang điểm kế khi tới gần điểm hiện tại. Gán kết quả cho
/// `topdown_body::input.move`.
/// @param agent Đường đang đi.
/// @param pos Vị trí hiện tại.
/// @return Hướng đi.
vec2 nav_steer(nav_agent &agent, vec2 pos);
/// @}
} // namespace njin
