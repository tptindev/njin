#pragma once
#include "_math.h"
#include "_tilemap.h"
#include "_types.h"
#include <vector>

namespace njin {

/// @addtogroup grp_place
/// @{

/// Ô không có vật nào, xem place_grid::owner.
constexpr u32 place_none = 0;

/// Lưới để đặt vật chiếm nhiều ô: công trình trong game xây làng, cây trồng
/// trong nông trại, đồ trong túi kiểu lưới. Mỗi ô nhớ vật nào đang chiếm nó.
///
/// Vật là một khối `w` x `h` ô có góc trên trái ở ô `at`; xoay 90 độ thì đổi
/// `w` và `h`. Mỗi vật mang một số `id` khác place_none do game chọn, ví dụ
/// chỉ số trong mảng công trình cộng 1.
///
/// Lưới này không chặn đường đi: muốn quái đi vòng công trình thì đánh dấu thêm
/// vùng place_rect() của nó trên njin::nav_grid bằng nav_set_area().
/// @code
/// njin::place_grid village = njin::place_grid_make({0, 0}, {32, 32}, 40, 40);
/// // Đang kéo một nhà 3x3: bắt theo ô dưới chuột, tô xanh hay đỏ.
/// const njin::cell at = njin::place_snap(village, njin::scr2w(ctx, njin::mouse_pos(ctx)), 3, 3);
/// const bool ok = njin::place_fits(village, at, 3, 3);
/// njin::draw_rect(ctx, njin::place_rect(village, at, 3, 3), ok ? green : red);
/// if (ok && njin::mouse_pressed(ctx, njin::mouse_left))
///   njin::place_put(village, at, 3, 3, house_id);
/// @endcode
struct place_grid {
  vec2 origin{};                ///< Góc trên trái của ô (0, 0) trong thế giới.
  vec2 cell_size{16.0f, 16.0f}; ///< Kích thước một ô, đơn vị thế giới.
  i32 width = 0;                ///< Số cột.
  i32 height = 0;               ///< Số hàng.
  /// Vật đang chiếm từng ô, theo hàng: `owner[y * width + x]`. place_none là trống.
  std::vector<u32> owner;
};

/// Tạo một lưới trống.
/// @param origin Góc trên trái trong thế giới.
/// @param cell_size Kích thước ô.
/// @param width Số cột.
/// @param height Số hàng.
/// @return Lưới.
place_grid place_grid_make(vec2 origin, vec2 cell_size, i32 width, i32 height);

/// Ô chứa một điểm trong thế giới (có thể nằm ngoài lưới).
/// @param grid Lưới.
/// @param pos Điểm.
/// @return Ô.
cell place_cell_at(const place_grid &grid, vec2 pos);

/// Ô góc trên trái cho một vật `w` x `h` có tâm gần `pos` nhất: bắt vật đang
/// kéo theo ô, để chuột nằm giữa vật chứ không ở góc. Có thể nằm ngoài lưới;
/// kiểm tra bằng place_fits().
/// @param grid Lưới.
/// @param pos Điểm, thường là chuột trong thế giới.
/// @param w Số cột của vật.
/// @param h Số hàng của vật.
/// @return Ô góc trên trái.
cell place_snap(const place_grid &grid, vec2 pos, i32 w, i32 h);

/// Vùng thế giới của một vật `w` x `h` đặt ở `at`: để vẽ, để đặt sprite, hay để
/// đánh dấu trên njin::nav_grid.
/// @param grid Lưới.
/// @param at Ô góc trên trái.
/// @param w Số cột.
/// @param h Số hàng.
/// @return Hình chữ nhật trong thế giới.
rect place_rect(const place_grid &grid, cell at, i32 w, i32 h);

/// Vật nào đang chiếm ô `c`.
/// @param grid Lưới.
/// @param c Ô.
/// @return `id` của vật, hoặc place_none nếu ô trống hay nằm ngoài lưới.
u32 place_owner(const place_grid &grid, cell c);

/// Một vật `w` x `h` đặt được ở `at` không: nằm trọn trong lưới, và mọi ô đều
/// trống hoặc đang thuộc `ignore`. Truyền `id` của vật đang được dời chỗ vào
/// `ignore`, để nó không vướng chính chỗ cũ của mình.
/// @param grid Lưới.
/// @param at Ô góc trên trái.
/// @param w Số cột, ít nhất 1.
/// @param h Số hàng, ít nhất 1.
/// @param ignore Vật được coi như không có.
/// @return `true` nếu đặt được.
bool place_fits(const place_grid &grid, cell at, i32 w, i32 h, u32 ignore = place_none);

/// Đặt vật `id` vào các ô `w` x `h` từ `at`, nếu place_fits() (với `ignore` là
/// `id`). Đặt lại một `id` đã có trong lưới là dời nó: chỗ cũ được trả lại.
/// @param grid Lưới.
/// @param at Ô góc trên trái.
/// @param w Số cột.
/// @param h Số hàng.
/// @param id Vật, khác place_none.
/// @return `true` nếu đã đặt; `false` thì lưới không đổi.
bool place_put(place_grid &grid, cell at, i32 w, i32 h, u32 id);

/// Bỏ vật `id` khỏi lưới: mọi ô của nó thành trống.
/// @param grid Lưới.
/// @param id Vật.
/// @return Số ô đã trả lại, 0 nếu vật không có trong lưới.
i32 place_remove(place_grid &grid, u32 id);

/// Tìm chỗ trống đầu tiên cho một vật `w` x `h`, quét từng hàng từ trên xuống,
/// mỗi hàng từ trái sang: nhặt đồ vào túi, đặt sẵn công trình vừa mua.
/// @param grid Lưới.
/// @param w Số cột.
/// @param h Số hàng.
/// @param out Nhận ô góc trên trái khi tìm thấy.
/// @return `true` nếu tìm thấy.
bool place_find_free(const place_grid &grid, i32 w, i32 h, cell &out);
/// @}
} // namespace njin
