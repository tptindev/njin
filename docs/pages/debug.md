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

## Tiêu thụ: CPU, RAM, GPU {#debug_consumption}

Nút **Consumption** trên thanh trên cùng đổi sang bố cục các cửa sổ đo tiêu thụ (nút **Overview**
đổi về). Có hai nguồn số liệu, và biết chúng khác nhau thế nào là điều quan trọng:

- **Hệ điều hành báo về tiến trình game** (cửa sổ *Process*): CPU, RAM, GPU thật. Inspector tự đọc
  bằng số hiệu tiến trình (game chỉ gửi `pid`), nên **game không tốn gì** để được đo.
- **Chính game báo về dữ liệu của nó** (các cửa sổ còn lại): thời gian từng system, kích thước từng
  component và entity, từng tài nguyên. Số này chi tiết hơn nhưng chỉ là phần game biết.

| Cửa sổ | Có gì |
|---|---|
| **Process** | CPU (% cả máy và % một lõi), RAM (đang dùng, riêng, đỉnh), GPU (% của engine bận nhất, 3D, copy), VRAM (chuyên dụng và dùng chung). Đồ thị hai phút gần nhất |
| **Systems** | Thanh phân bổ frame theo phase (phần còn lại là chờ vsync và trình chiếu). Bảng **từng system**: ms trung bình, frame vừa rồi, đỉnh, số lần gọi; lọc, sắp xếp |
| **Memory** | Từng loại component: số lượng, `sizeof`, heap nó giữ, tổng, biểu đồ tròn. Tab **Entities**: bộ nhớ và GPU của **từng entity**, sắp xếp được, bấm để chọn |
| **Assets** | Từng texture, render target, font, shader, sound, music, kèm dung lượng và nằm ở GPU hay RAM. Cộng thêm ảnh chunk tilemap, buffer post-processing, màn hình ảo |
| **Entities** | Thêm hai cột RAM và GPU. Cửa sổ **Inspector** ghi entity đang chọn giữ bao nhiêu |

Cách đọc các con số:

- **CPU %** lấy từ thời gian CPU của tiến trình. 100 % là cả máy; "% một lõi" cho biết một luồng
  bận cỡ nào (game một luồng chạy hết một lõi trên máy 16 lõi chỉ là 6 % cả máy).
- **GPU và VRAM** đọc từ bộ đếm hiệu năng của Windows (`GPU Engine`, `GPU Process Memory`), giống
  Task Manager, nên dùng được với card của mọi hãng (Windows 10 1709 trở lên). Card tích hợp dùng
  bộ nhớ hệ thống, nên VRAM chuyên dụng là 0 và số thật nằm ở "dùng chung". Trên Linux và macOS,
  cửa sổ Process chưa có GPU (Linux có CPU và RAM); dùng cửa sổ Assets.
- **Component**: tính `sizeof` + heap + 8 byte chỉ mục của EnTT. **Component của game phải đăng ký
  bằng njin::debug_component()** mới có kích thước (và giá trị); chưa đăng ký thì hiện `?`. Heap
  (vector, chuỗi) của component engine như tilemap, particle_emitter, level_object đã được ước tính;
  của component game thì chỉ tính phần `sizeof`.
- **Một entity chỉ tốn CPU thông qua các system chạy trên nó**, nên CPU được chia theo **system**, không
  theo entity. Xem system nào tốn, rồi xem nó chạy trên component nào. GPU của entity chỉ có với
  tilemap (ảnh chunk đã bake); sprite dùng chung texture nên texture được tính ở cửa sổ Assets.
- Ước tính GPU của Assets không gồm bộ nhớ của driver, swap chain và context, nên **nhỏ hơn** số
  VRAM hệ điều hành báo.

Tên system lấy từ tham số thứ ba của ecs_register() hoặc `sys_desc::name`; không đặt thì hiện
`module/#số`. Đo thời gian **chỉ chạy khi có inspector kết nối** (hai lần đọc đồng hồ mỗi system),
và ngừng ngay khi inspector đóng.

Game thử inspector: `njin_debug_demo` (xem @ref samples). Mỗi phím của nó cố ý tốn một thứ:
thêm bóng (entity, va chạm), `H` đốt 3 ms CPU trong một system, `M` giữ 64 MB RAM, `G` tạo một
render texture 32 MB trên GPU. Bấm rồi xem con số nhảy.

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
