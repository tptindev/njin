# Công cụ debug: njin_inspector {#debug}

**njin_inspector** là một chương trình riêng, chạy cạnh game. Nó kết nối vào game qua
`127.0.0.1` và hiện mọi thứ cần để dò lỗi trong cửa sổ của nó. **Game không vẽ thêm gì**:
muốn xem thì nhìn sang cửa sổ inspector, không thì cứ chơi bình thường.

| Bảng | Có gì |
|---|---|
| **Performance** | FPS, biểu đồ thời gian của 600 frame gần nhất (min, max), số entity. Dừng, chạy từng frame, tua nhanh hoặc chậm |
| **Entities** | Mọi entity kèm tên và danh sách component, lọc theo tên, id hoặc component |
| **World** | Bản đồ thu nhỏ: khung collider (xanh lá là vật cản, vàng là trigger, xám là đang tắt), vùng tilemap (đỏ là vật cản), khung camera của game (xanh dương). Kéo để di chuyển, cuộn để phóng to, bấm để chọn entity |
| **Inspector** | Component của entity đang chọn, kèm giá trị. Sửa trực tiếp vị trí, góc, tỉ lệ; bật tắt collider, trigger, sprite; hủy entity |
| **Watches** | Giá trị game tự đặt bằng njin::debug_watch(), cập nhật trực tiếp |
| **Log** | Log của game, lọc theo mức và chữ |

## Dùng

1. Trong game, mở cổng debug (thường chỉ ở bản debug):

@include debug_inspector.cpp

2. Chạy game và `build\bin\njin_inspector.exe`, theo thứ tự nào cũng được. Inspector tự kết
   nối khi game mở cổng, và tự nối lại khi game khởi động lại.

Cổng mặc định là 7779. Dùng cổng khác: `debug_server_start(*ctx, {.port = 7800})` trong game và
`njin_inspector --port 7800`.

Inspector nhớ vị trí và kích thước các bảng trong `njin_inspector.ini` ở thư mục chạy nó.

## Component của game

Inspector liệt kê **mọi** component của một entity, kể cả component của game, theo tên kiểu
C++. Component của engine (transform, collider, sprite, animator...) còn hiện cả giá trị.
Component của game muốn hiện giá trị thì đăng ký bằng njin::debug_component(): hàm nhận
component và trả về njin::json_value (xem @ref json).

Chưa đăng ký thì inspector ghi "no view" dưới tên component.

## Điều khiển thời gian

| Nút | Việc làm |
|---|---|
| Pause / Resume | Dừng game, như njin::time_set_paused() |
| Step frame | Khi đang dừng: chạy đúng **một** frame dài bằng một nhịp cố định (1/60 giây), rồi dừng lại. Vật lý trong `phase_fixed_update` tiến đúng một bước |
| time scale | Tua chậm hoặc nhanh, như njin::time_set_scale() |

Lệnh từ inspector được áp dụng ở đầu frame, trước mọi system của game, nên một frame không
bao giờ chạy nửa dừng nửa không.

## Chi phí và an toàn

- Chỉ nghe trên `127.0.0.1`: máy khác không kết nối được. Vẫn nên chỉ bật trong bản debug
  (`#ifndef NDEBUG`), vì inspector sửa và hủy được entity.
- Không có inspector nào kết nối thì mỗi frame chỉ tốn một lần hỏi socket.
- Khi có inspector: số liệu frame gửi mỗi frame, danh sách entity, entity đang chọn và watches
  gửi theo `debug_server_desc::snapshot_hz` (mặc định 10 lần/giây), tối đa
  `max_entities` entity (mặc định 4000; inspector báo khi danh sách bị cắt).
- **Không bao giờ làm game đứng**: socket không chặn, và nếu inspector đọc chậm thì dữ liệu mới
  bị bỏ thay vì dồn lại (thanh trạng thái của inspector báo số tin bị bỏ).
- Component của game được nhận ra theo tên kiểu mà trình biên dịch sinh, nên tên có thể hơi
  khác nhau giữa MSVC, GCC và Clang.

## Bên trong

Game và inspector nói chuyện bằng từng dòng JSON qua TCP. Phía game là module lõi `njin.debug`
(`runtime/modules/debug.cpp`); phía inspector là `src/tools/inspector`, dùng Dear ImGui qua
rlImGui. Hai bên kiểm tra số phiên bản giao thức khi kết nối: lệch thì inspector báo cần build
lại một trong hai.

Tắt việc build inspector (và tải ImGui) bằng `-DNJIN_BUILD_INSPECTOR=OFF` khi cấu hình CMake.
