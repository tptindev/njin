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

@image html inspector_overview.png "njin_inspector nối với njin_debug_demo, bố cục Overview. Đang chọn entity 27: thấy component ball, transform, collider và giá trị của chúng. Bảng Performance có mục Screen recording"

## Dùng

1. Trong game, mở cổng debug (thường chỉ ở bản debug):

@include debug_inspector.cpp

2. Chạy game và `build\bin\njin_inspector.exe`, theo thứ tự nào cũng được. Inspector tự kết
   nối khi game mở cổng, và tự nối lại khi game khởi động lại.

**Log đi sang inspector khi nó đang kết nối.** Lúc đó console của game không in log nữa; mọi dòng,
kể cả các dòng lúc mở cửa sổ và nạp GL, nằm ở bảng **Log** của inspector. Chưa có inspector (hoặc nó
đã tắt) thì game vẫn log ra console như thường. Các dòng ra trước khi inspector nối vào được giữ lại
(tối đa 2000) và gửi ngay khi nối. Game tự mở cổng bằng njin::debug_server_start(), như ở trên;
tắt cổng (bản phát hành) thì log luôn ra stderr.

Cổng mặc định là 7779. Dùng cổng khác: `debug_server_start(*ctx, {.port = 7800})` trong game và
`njin_inspector --port 7800`.

**Cỡ cửa sổ.** Hai bố cục phủ kín cửa sổ ở mọi cỡ: vị trí và kích thước các bảng là tỉ lệ của vùng dưới thanh menu, nên kéo
cửa sổ to hay nhỏ thì các bảng co giãn theo và giữ nguyên chỗ, kể cả bảng bạn đã kéo đi chỗ khác. Màn hình nhỏ hơn 1700 x 960
thì cửa sổ mở nhỏ lại cho vừa. `njin_inspector --size 1280x720` mở cửa sổ đúng cỡ đó (tối thiểu 640 x 400). Cửa sổ rất nhỏ thì
bảng nào không đủ chỗ sẽ cuộn được.

Inspector nhớ vị trí và kích thước các bảng trong `njin_inspector.ini` ở thư mục chạy nó. `njin_inspector --layout consumption` mở thẳng bố cục Consumption
(mặc định là Overview).

## Component của game

Inspector liệt kê **mọi** component của một entity, kể cả component của game, theo tên kiểu
C++. Component của engine (transform, collider, sprite, animator...) còn hiện cả giá trị.
Component của game muốn hiện giá trị thì đăng ký bằng njin::debug_component(): hàm nhận
component và trả về njin::json_value (xem @ref json).

Chưa đăng ký thì inspector ghi "no view" dưới tên component.

## Gizmo: tự vẽ để debug {#debug_gizmo}

Các hàm `gizmo_*` (`njin_gizmo.h`) vẽ đường, mũi tên, khung, hình cầu, trục tọa độ, điểm và nhãn chữ
lên trên cùng của thế giới, cả 2D lẫn 3D (@ref graphics_3d). Gọi được ở mọi phase, không cần đứng
trong lúc vẽ; `duration` giữ lại một vết thay vì chỉ hiện đúng frame gọi.

@code
njin::gizmo_circle(ctx, enemy_pos, aggro_radius, njin::colors::red);
njin::gizmo_arrow(ctx, player_pos, player_pos + velocity * 0.2f, njin::colors::yellow);
njin::gizmo_line3d(ctx, muzzle, hit_point, njin::colors::yellow, 1.0f); // giữ 1 giây
@endcode

gizmos_set_visible() bật tắt tất cả, cho một phím debug hoặc để im lặng trong bản phát hành. Khi
inspector đang nối, gizmo cũng hiện trong ô World của nó, kể cả gizmo 3D khi game đang vẽ 3D.

## Tiêu thụ: CPU, RAM, GPU {#debug_consumption}

@image html inspector_consumption.png "Bố cục Consumption: Process (CPU, RAM, GPU của tiến trình), Systems (thời gian từng system), Memory, Assets và Entities"

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

Cửa sổ **Performance** còn có mục **Rendering (last frame)**: số sprite vẽ và số bị cắt vì nằm
ngoài camera, số chunk tilemap, số hạt (và bao nhiêu trên GPU), số lệnh instanced, số pass hậu kỳ, và
**số lệnh vẽ ước tính** (xem @ref render_stats).

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

## Quay màn hình {#debug_recording}

Bảng **Performance** có mục **Screen recording**: bấm **Record** (hoặc **F9** ở bất kỳ chỗ nào trong inspector, trừ khi đang gõ
vào ô văn bản) để quay cửa sổ game thành một file GIF, bấm lại để dừng. Game không phải làm gì thêm ngoài
njin::debug_server_start().

@image html inspector_recording.png "Mục Screen recording của njin_inspector, nối với njin_platformer. Trái: đang quay (REC 0:02, 38 khung, 223 KB, 640x360). Phải: đã dừng, hiện đường dẫn file với nút Open folder và Copy path"

Việc quay diễn ra **trong game**, không phải trong inspector: cứ mỗi 1/fps giây, sau khi frame đã vẽ xong (có cả UI, hiệu ứng
hậu kỳ), game đọc ảnh từ card đồ họa, thu nhỏ rồi nối vào file GIF. Vì đọc từ chính game chứ không chụp màn hình hệ điều hành,
cửa sổ khác che lên game không lọt vào file (đã thử). File nằm trong thư mục lưu của game,
`recordings/rec_<ngày>_<giờ>.gif` (cùng chỗ với `screenshots/` của njin::screenshot(); xem njin::save_path()); inspector hiện
đường dẫn, và **Open folder** mở thư mục đó.

| Lựa chọn | Giá trị | Ghi chú |
|---|---|---|
| Tốc độ | 10, 15, 20, 30 khung/giây | Mặc định 15 |
| Cỡ | 100%, 75%, 50%, 33% cửa sổ | Mặc định 50%. Thu nhỏ bằng cách lấy trung bình các pixel |
| Giới hạn | 5 đến 120 giây | Hết giờ thì tự dừng. Mặc định 30 |

Các lựa chọn áp dụng cho lần quay **kế tiếp**. Quay dừng khi: bấm Stop hay F9, hết giới hạn, inspector đóng hoặc mất kết nối
(game hoàn tất file rồi mới thôi, nên vẫn xem được), hoặc game thoát. Nếu cửa sổ game bị đổi cỡ khi đang quay, mỗi khung được
co về cỡ lúc bắt đầu (đổi cả tỉ lệ thì hình bị méo).

Vài điều về file:

- Mỗi khung có bảng **256 màu riêng**, chọn theo màu thật của khung đó và không pha màu (dither). Pixel art với ít hơn
  256 màu ra đúng màu (tới độ chính xác 5 bit mỗi kênh, sai số tối đa 7 trên 255); ảnh chuyển sắc mượt thì có thể thấy
  sọc màu.
- Khung **giống hệt** khung trước không được ghi lại: khung trước chỉ được giữ lâu hơn. Màn hình đứng yên (menu, game đang dừng) cho
  file rất nhỏ.
- Thời lượng mỗi khung là thời gian thật giữa hai lần chụp, nên GIF chạy đúng bằng thời gian đã quay, kể cả khi game giật.
  Trình xem GIF làm tròn thời gian mỗi khung theo 1/100 giây.
- Thử với njin_platformer (menu chính, 640x360, 15 khung/giây, 3 giây): 41 khung, 248 KB.

**Chi phí:** khung nào được chụp thì game tốn thêm ở đúng frame đó: đọc từ GPU rồi thu nhỏ và nén GIF ngay trên luồng của game
(đo riêng bước nén, khung 480x270, ảnh nhiều màu: khoảng 4 ms). Biểu đồ frame time của inspector có gai theo nhịp quay, và game
chậm đi thấy được nếu frame vốn đã sát 16,7 ms. Muốn nhẹ hơn thì hạ tốc độ hoặc cỡ.

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
rlImGui. Hai bên kiểm tra số phiên bản giao thức khi kết nối (hiện là 4): lệch thì inspector báo cần build
lại một trong hai. Việc quay màn hình đi qua lệnh `rec` (inspector gửi tốc độ, cỡ và giới hạn) và tin `rec` (game báo
trạng thái, số khung, dung lượng và đường dẫn file); phần chụp và ghi GIF là `runtime/modules/debug_record.cpp` và
`runtime/njin_gif.cpp`.

Tắt việc build inspector (và tải ImGui) bằng `-DNJIN_BUILD_INSPECTOR=OFF` khi cấu hình CMake.
