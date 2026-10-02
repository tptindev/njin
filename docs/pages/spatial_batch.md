# Batch instance theo vùng {#spatial_batch}

`njin_spatial_batch.h` tách việc chọn khoảng instance khỏi GPU và camera của game.
Buffer được dựng theo thứ tự nhóm; `offsets[c]` là instance đầu của nhóm c,
`offsets.back()` là điểm kết thúc. Một nhóm có thể rỗng. Cùng buffer có thể
được vẽ từ nhiều camera với các mask và khoảng loại trừ khác nhau.

```cpp
#include "njin_spatial_batch.h"

njin::batch_grid2d grid{{-100, -100}, {20, 20}, 10, 10};
njin::batch_view2d view{{-40, -30}, {40, 30}, {0, 0}, 8};
std::vector<njin::u8> wanted(grid.count());
for (njin::i32 c = 0; c < grid.count(); ++c)
    wanted[c] = njin::batch_cell_visible(grid, view, c) &&
                njin::batch_cell_detailed(grid, view, c, 50);

// offsets: 101 phần tử, mô tả buffer đã xếp theo 100 ô.
// Bỏ một đối tượng đang được vẽ riêng (ví dụ nhà cắt mở).
const njin::instance_range excluded[] = {{15, 21}};
const auto ranges = njin::batch_instance_ranges(offsets, wanted, excluded);
for (const auto &[from, to] : ranges)
    njin::draw_instanced3d(ctx, model, buffer, from, to - from);
```

Game sở hữu buffer, đặt instance vào nhóm bằng `grid.cell_at(center)` và giữ
các offset tương ứng. API không tự sắp lại instance, tải model, upload buffer
hay quyết định LOD mesh. Muốn dùng LOD khác nhau thì tính mask/ranges cho mỗi
cấp; muốn mọi chi tiết thì chỉ kiểm tra visibility, không kiểm tra radius.

`view.lo/hi` là hình chữ nhật bảo thủ dựng từ camera/frustum trên mặt phẳng
của game. Đây là broad phase, không thay thế frustum 3D hoặc occlusion culling.
`overhang` phải bao phần geometry nhô khỏi ô chứa tâm; quá nhỏ sẽ làm vật thể
biến mất ở mép màn hình. Origin, cell_size, focus, bounds và radius dùng cùng
đơn vị tùy game, không tự chuyển mét hoặc scale render.

Mỗi camera giữ `batch_view2d` riêng. Các hàm đọc dữ liệu const, không có camera
hoặc cache toàn cục. Có thể gọi trên worker thread nếu dữ liệu đầu vào không
được sửa đồng thời. Tái sử dụng mask qua frame để tránh cấp phát lại.

Các khoảng trả về tăng dần, không chồng nhau, gộp tối đa khi liền kề. Excluded
không cần sắp xếp; các khoảng chồng nhau được hợp trước khi trừ. Offset giảm
hoặc mask sai kích thước trả rỗng; excluded rỗng/đảo bị bỏ. Không vượt điểm
kết thúc offset, nhưng caller vẫn phải bảo đảm offsets khớp buffer thực.

Sandtable sử dụng API này trong `city/render_lod.cpp`, giữ nguyên các chính
sách camera, detail radius và kính của game. Kiểm tra không mở cửa sổ:

```
cmake --build build-release --target njin_spatial_batch_check
build-release/bin/njin_spatial_batch_check.exe
```

Kiểm tra so với oracle từng instance trên 10.000 trường hợp; đối chiếu 2.000
quyết định visibility/detail với công thức cũ của Sandtable; kiểm tra origin
âm, biên ô, overhang, dữ liệu lỗi và camera độc lập. Việc tách API tự nó
không chứng minh GPU nhanh hơn: giữ cùng seed/view và đo GPU timing từng pass
khi thay chính sách culling/LOD.
