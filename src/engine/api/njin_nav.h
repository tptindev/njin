#pragma once
#include "_math.h"
#include "_tilemap.h"
#include "_types.h"
#include <span>
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
  /// Độ "tham" của A*: chi phí ước lượng tới đích nhân với số này. 1 (mặc
  /// định) cho đường ngắn nhất; lớn hơn (1.5 đến 3) xét ít ô hơn nhiều trên
  /// bản đồ lớn, đổi lại đường có thể dài hơn đường ngắn nhất một chút
  /// (không quá `greed` lần). Hợp với đám đông đi xa trong thành phố.
  f32 greed = 1.0f;
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

/// Trường hướng (flow field): mỗi ô của một njin::nav_grid biết đi hướng nào để
/// về đích gần nhất, tính một lần cho cả lưới. Cho rất nhiều con cùng đi về
/// một chỗ (tower defense, RTS, bầy quái đuổi người chơi): mỗi con chỉ đọc ô của
/// nó bằng nav_flow_dir(), thay vì mỗi con một lần nav_find_path().
///
/// Dựng bằng nav_flow_build(); dựng lại khi đích đổi ô hay lưới đổi (tường
/// mới, cửa mở). Một lần dựng tốn như một lần A* xét hết lưới.
struct nav_flow {
  vec2 origin{};                ///< Như nav_grid::origin của lưới đã dùng.
  vec2 cell_size{16.0f, 16.0f}; ///< Như nav_grid::cell_size.
  i32 width = 0;                ///< Số cột.
  i32 height = 0;               ///< Số hàng.
  /// Chi phí từ từng ô về đích gần nhất, theo hàng, cùng cách tính với
  /// nav_find_path() (chi phí vào ô, đi chéo nhân 1.41). Âm là không tới được.
  std::vector<f32> cost;
  /// Ô kế tiếp trên đường về đích (`y * width + x`); -1 ở ô đích và ô không tới được.
  std::vector<i32> next;
  std::vector<vec2> goals; ///< Các đích đã dùng.
};

/// Tùy chọn của nav_flow_build().
struct nav_flow_opts {
  bool diagonal = true;     ///< Đi chéo được, như nav_path_opts::diagonal.
  bool cut_corners = false; ///< Đi chéo qua góc vật cản, như nav_path_opts::cut_corners.
  /// Chỉ tính các ô có chi phí về đích tới mức này (0 là cả lưới): bầy quái chỉ
  /// đuổi trong một vùng quanh người chơi, rẻ hơn trên bản đồ lớn. Ô xa hơn là
  /// không tới được.
  f32 max_cost = 0.0f;
};

/// Dựng trường hướng về đích gần nhất trong `goals` (một hay nhiều chỗ: lâu
/// đài, mọi công trình của người chơi). Đích nằm ngoài lưới hay trên vật cản bị
/// bỏ qua. `flow` được dùng lại nên dựng mỗi giây không cấp phát lại bộ nhớ.
/// @code
/// njin::nav_flow_build(g.flow, g.nav, player_pos);  // khi người chơi sang ô khác
/// for (auto [e, tr, body] : reg.view<njin::transform, njin::topdown_body, enemy>().each())
///   body.input.move = njin::nav_flow_dir(g.flow, g.nav, tr.pos);
/// @endcode
/// @param flow Nhận kết quả.
/// @param grid Lưới.
/// @param goals Các đích, trong thế giới.
/// @param opts Tùy chọn.
void nav_flow_build(nav_flow &flow, const nav_grid &grid, std::span<const vec2> goals,
                    const nav_flow_opts &opts = {});

/// Như bản trên, với một đích.
/// @param flow Nhận kết quả.
/// @param grid Lưới.
/// @param goal Đích, trong thế giới.
/// @param opts Tùy chọn.
void nav_flow_build(nav_flow &flow, const nav_grid &grid, vec2 goal, const nav_flow_opts &opts = {});

/// Hướng cần đi từ `pos` để về đích, độ dài 1: tới tâm ô kế tiếp, hay xa hơn
/// vài ô khi đường thẳng tới đó không vướng vật cản (theo `grid`), nên đi xiên
/// mượt thay vì bậc thang. Trong ô đích là hướng thẳng tới điểm đích. `{0, 0}`
/// khi đã tới đích, ngoài lưới hay không tới được. Gán cho
/// `topdown_body::input.move`, như nav_steer().
/// @param flow Trường hướng từ nav_flow_build().
/// @param grid Lưới đã dùng để dựng nó.
/// @param pos Vị trí hiện tại.
/// @return Hướng đi.
vec2 nav_flow_dir(const nav_flow &flow, const nav_grid &grid, vec2 pos);

/// Chi phí từ ô chứa `pos` về đích gần nhất (nav_flow::cost).
/// @param flow Trường hướng.
/// @param pos Vị trí.
/// @return Chi phí; âm nếu không tới được hay ngoài lưới.
f32 nav_flow_cost(const nav_flow &flow, vec2 pos);
/// @}
} // namespace njin
