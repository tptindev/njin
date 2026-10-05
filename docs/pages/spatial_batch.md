# Chỉ vẽ phần camera thấy: instance theo ô {#spatial_batch}

draw_instanced3d() vẽ cả một khúc bộ đệm bằng một lệnh, nhưng card đồ họa vẫn xử lý từng instance trong khúc đó,
kể cả những cái nằm sau lưng camera. Với một khu rừng hay một thành phố hàng chục nghìn vật mà camera chỉ thấy một
góc, phần lớn công sức ấy bỏ phí. `njin_spatial_batch.h` giải quyết bằng cách chia mặt đất thành ô: xếp instance
trong bộ đệm theo ô **một lần**, rồi mỗi frame chọn những ô cần vẽ và nhận lại các khúc bộ đệm tương ứng. Cũng cách
đó chọn được mức chi tiết: ô gần vẽ hình mịn, ô xa vẽ hình ít mặt.

Cần biết trước: @ref graphics_3d, phần Instancing. Cách này dùng được cả với draw_instanced() của 2D, vì nó cũng nhận
instance đầu và số instance.

## Ví dụ: 40.000 cây

@include spatial_batch.cpp

## Ba bước

| Bước | Khi nào | Làm gì |
|---|---|---|
| 1. Lưới | Một lần | njin::batch_grid2d: góc `origin`, kích thước ô `cell_size`, số cột `cols` và hàng `rows` |
| 2. Xếp bộ đệm | Khi nạp, hoặc khi vật thêm bớt | Mỗi instance thuộc ô chứa tâm nó (`grid.cell_at()`). Ghi bộ đệm theo ô: mọi instance của ô 0, rồi ô 1... `offsets[c]` là instance đầu của ô `c`, phần tử cuối là tổng số instance |
| 3. Chọn ô | Mỗi frame, mỗi camera | Một mảng `wanted`, mỗi ô một số (khác 0 là vẽ), rồi batch_instance_ranges() trả về các khúc `[first, second)` cho draw_instanced3d() |

Ô liền nhau được chọn thì gộp thành một khúc, nên một vùng nhìn thấy thường chỉ tốn vài lệnh vẽ. Ô rỗng không sao.

Kích thước ô là sự đánh đổi: ô nhỏ thì vẽ sát vùng nhìn hơn nhưng nhiều khúc hơn (nhiều lệnh vẽ); ô to thì ngược lại.
Bắt đầu với ô cỡ vài lần vật lớn nhất, rồi đo.

## Vùng nhìn

njin::batch_view2d mô tả phần mặt đất một camera thấy, trên cùng mặt phẳng với lưới: x, z của thế giới với cảnh 3D,
x, y với cảnh 2D. Mọi số dùng chung đơn vị của game.

| Trường | Nghĩa |
|---|---|
| `lo`, `hi` | Hình chữ nhật nhìn thấy. Phải **bao trọn** những gì camera thấy: rộng quá chỉ vẽ thừa, hẹp quá thì vật ở mép màn hình biến mất |
| `overhang` | Vật nhô khỏi ô chứa tâm nó xa nhất bao nhiêu (bán kính lớn nhất của instance) |
| `focus` | Điểm đo khoảng cách cho mức chi tiết, thường là vị trí camera |

batch_cell_visible() cho biết ô có chạm vùng nhìn không. batch_cell_detailed() cho biết ô có cách `focus` dưới một bán
kính không, và không xét vùng nhìn: kết hợp hai hàm như ví dụ để có hai mức chi tiết, mỗi mức một mảng `wanted`.

Đây là phép lọc thô theo hình chữ nhật, không phải kiểm tra frustum 3D hay che khuất: vật trong hình chữ nhật mà nằm
ngoài khung hình vẫn được vẽ. Mỗi camera (màn hình chính, minimap, gương) giữ một vùng nhìn riêng và dùng chung bộ đệm.

## Bỏ bớt một khúc

Tham số thứ ba của batch_instance_ranges() là các khúc **không** vẽ, ví dụ một ngôi nhà đang được vẽ riêng ở dạng cắt
mở:

@code
const njin::instance_range excluded[] = {{house_first, house_first + house_count}};
for (const auto &[from, to] : njin::batch_instance_ranges(offsets, wanted, excluded))
  njin::draw_instanced3d(ctx, house_model, buffer, from, to - from);
@endcode

Các khúc bỏ có thể chồng nhau và không cần sắp xếp.

## Những gì nó không làm

- Không giữ hay sửa bộ đệm: game tự xếp instance, tự gọi instance_buffer_upload() và giữ `offsets` khớp với bộ đệm.
- Không chọn hình cho từng mức chi tiết: game quyết định vẽ gì cho mỗi mảng `wanted`.
- Dữ liệu sai thì trả về rỗng thay vì đoán: `offsets` không có đúng `wanted.size() + 1` phần tử hay có chỗ giảm.

Các hàm chỉ đọc dữ liệu vào và không giữ gì giữa các lần gọi, nên gọi được cho nhiều camera, và trên thread khác
miễn là không ai sửa dữ liệu vào cùng lúc. Giữ mảng `wanted` qua các frame để khỏi cấp phát lại.
