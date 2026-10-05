#pragma once
#include "_types.h"
#include <span>
#include <utility>
#include <vector>

namespace njin {

/// @addtogroup grp_instancing
/// @{

/// Lưới chia mặt đất thành ô, để nhóm instance theo ô chứa tâm của chúng.
///
/// Mặt phẳng là của game: x, z của thế giới với cảnh 3D, x, y với cảnh 2D. Mọi
/// số (origin, cell_size, vùng nhìn, bán kính) dùng chung một đơn vị, của game.
/// Lưới chỉ là số, không giữ bộ đệm hay camera nào. Xem @ref spatial_batch.
struct batch_grid2d {
  vec2 origin{};          ///< Góc nhỏ nhất (x, y nhỏ nhất) của ô 0.
  vec2 cell_size{1, 1};   ///< Kích thước một ô theo hai trục, lớn hơn 0.
  i32 cols = 0;           ///< Số cột (theo trục thứ nhất).
  i32 rows = 0;           ///< Số hàng (theo trục thứ hai).
  /// Số ô, `cols * rows`. 0 nếu lưới không hợp lệ (kích thước hay số ô không
  /// dương, số không hữu hạn, quá nhiều ô).
  /// @return Số ô.
  i32 count() const;
  /// Ô chứa điểm `p`: `hàng * cols + cột`. Điểm ngoài lưới thuộc ô biên gần nhất.
  /// @param p Điểm trên mặt phẳng, thường là tâm của instance.
  /// @return Chỉ số ô, -1 nếu lưới không hợp lệ hay `p` không hữu hạn.
  i32 cell_at(vec2 p) const;
};

/// Phần mặt phẳng một camera thấy, do game dựng từ camera của mình mỗi frame.
///
/// Mỗi camera (màn hình chính, minimap, gương) giữ một vùng riêng. Đây là phép lọc
/// thô theo hình chữ nhật, không thay cho kiểm tra frustum 3D hay che khuất: hình
/// chữ nhật phải **bao trọn** những gì camera thấy, rộng quá chỉ tốn thêm việc vẽ.
struct batch_view2d {
  vec2 lo{};    ///< Góc nhỏ nhất của hình chữ nhật nhìn thấy.
  vec2 hi{};    ///< Góc lớn nhất; không nhỏ hơn `lo` ở trục nào.
  vec2 focus{}; ///< Điểm đo khoảng cách cho batch_cell_detailed(), thường là vị trí camera.
  /// Vật nhô ra khỏi ô chứa tâm nó xa nhất bao nhiêu (bán kính lớn nhất của
  /// instance). Nhỏ quá thì vật ở mép màn hình biến mất. Không âm.
  f32 overhang = 0;
};

/// Ô `cell` có lọt vào vùng nhìn không: hình chữ nhật của ô, nới thêm `overhang`,
/// có chạm hình chữ nhật `lo` .. `hi` không.
/// @param grid Lưới.
/// @param view Vùng nhìn của camera.
/// @param cell Chỉ số ô.
/// @return false cả khi ô, lưới hay vùng nhìn không hợp lệ.
bool batch_cell_visible(const batch_grid2d &grid, const batch_view2d &view, i32 cell);

/// Ô `cell` có ở gần không: khoảng cách từ `view.focus` đến hình chữ nhật của ô
/// nhỏ hơn `radius`. Dùng để chọn mức chi tiết (hình mịn ở gần, hình ít mặt ở xa).
/// Hàm không xét vùng nhìn; kết hợp với batch_cell_visible() nếu cần.
/// @param grid Lưới.
/// @param view Vùng nhìn, chỉ dùng `focus`.
/// @param cell Chỉ số ô.
/// @param radius Bán kính vùng gần, lớn hơn 0.
/// @return false cả khi ô, lưới, `focus` hay `radius` không hợp lệ.
bool batch_cell_detailed(const batch_grid2d &grid, const batch_view2d &view, i32 cell, f32 radius);

/// Một đoạn instance `[first, second)` trong bộ đệm: vẽ bằng
/// `draw_instanced3d(ctx, mesh, buffer, first, second - first)`.
using instance_range = std::pair<u32, u32>;

/// Các đoạn instance cần vẽ, từ những ô được chọn.
///
/// Bộ đệm phải xếp instance theo ô: mọi instance của ô 0, rồi ô 1, ... `offsets[c]`
/// là instance đầu của ô `c`, `offsets[c + 1]` là chỗ ô đó hết, nên `offsets` có
/// `wanted.size() + 1` phần tử, không giảm; ô rỗng được phép. Các ô được chọn liền
/// nhau gộp thành một đoạn, rồi các đoạn trong `excluded` bị cắt bỏ.
///
/// Hàm không đổi gì ngoài kết quả, nên gọi được cho nhiều camera và trên thread khác,
/// miễn là không ai sửa dữ liệu vào cùng lúc.
/// @param offsets Chỗ bắt đầu của từng ô trong bộ đệm, cộng chỗ kết thúc.
/// @param wanted Mỗi ô một số: khác 0 là vẽ ô đó.
/// @param excluded Các đoạn không vẽ (ví dụ một vật đang được vẽ riêng), theo thứ
/// tự bất kỳ, có thể chồng nhau. Đoạn rỗng hay ngược bị bỏ qua.
/// @return Các đoạn tăng dần, không chồng nhau, không vượt `offsets.back()`. Rỗng
/// nếu `offsets` sai kích thước hay có chỗ giảm.
std::vector<instance_range> batch_instance_ranges(std::span<const u32> offsets,
    std::span<const u8> wanted, std::span<const instance_range> excluded = {});

/// @}

} // namespace njin
