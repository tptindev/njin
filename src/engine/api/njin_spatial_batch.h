#pragma once
#include "_types.h"
#include <span>
#include <utility>
#include <vector>

namespace njin {

/// Lưới nhóm instance trên mặt phẳng. Dùng cùng đơn vị cho origin, cell_size,
/// bounds và bán kính. Không chứa GPU handle hoặc trạng thái camera toàn cục.
struct batch_grid2d {
  vec2 origin{};
  vec2 cell_size{1, 1};
  i32 cols = 0, rows = 0;
  i32 count() const;
  /// Ô chứa tâm; điểm ngoài lưới được kẹp vào ô biên. -1 nếu lưới không hợp lệ.
  i32 cell_at(vec2 p) const;
};

/// Vùng nhìn bảo thủ trên mặt phẳng, do game dựng từ camera/frustum.
/// Mỗi camera giữ một giá trị riêng. Không phải kiểm tra frustum 3D chính xác.
struct batch_view2d {
  vec2 lo{}, hi{};
  vec2 focus{};
  /// Khoảng vật thể có thể nhô khỏi ô chứa tâm của nó; phải bao mọi instance.
  f32 overhang = 0;
};
/// Ô giao với bounds, kể cả phần nhô ra. Ô/lưới/view không hợp lệ trả false.
bool batch_cell_visible(const batch_grid2d &grid, const batch_view2d &view, i32 cell);
/// Khoảng cách từ focus đến hình chữ nhật ô < radius. Không áp dụng visibility.
bool batch_cell_detailed(const batch_grid2d &grid, const batch_view2d &view, i32 cell, f32 radius);

/// Khoảng instance [first, second), truyền cho draw_instanced3d bằng first và
/// second-first. Không sở hữu buffer; thứ tự buffer phải giống offsets.
using instance_range = std::pair<u32, u32>;
/// Instance được xếp liên tiếp theo nhóm: offsets có wanted.size()+1 phần tử,
/// tăng không giảm, phần tử cuối là điểm kết thúc. Ô rỗng được phép. Gộp các
/// khoảng nhìn thấy liền nhau rồi trừ excluded (không cần sắp xếp, có thể chồng).
/// Khoảng loại rỗng/đảo bị bỏ; khoảng vượt buffer được cắt. Dữ liệu nhóm sai
/// trả mảng rỗng. Hàm thuần, dùng được cho nhiều camera và trên worker thread.
std::vector<instance_range> batch_instance_ranges(std::span<const u32> offsets,
    std::span<const u8> wanted, std::span<const instance_range> excluded = {});

} // namespace njin
