# Particle và hiệu ứng {#particles}

Những thứ nhỏ làm game "có lực": bụi khi tiếp đất, tia lửa khi chém trúng, camera rung khi
nổ, khung hình dừng một nhịp khi đòn đánh trúng. njin có sẵn tất cả, và một bộ mẫu dùng ngay.

@include particles_fx.cpp

@image html particles_fountain.gif "Đài phun hạt trong njin_render_demo (phím F bật tắt), chạy trên GPU"

## Particle

Gắn njin::particle_emitter vào entity có njin::transform. Module particle của engine sinh và
cập nhật hạt trong `phase_post_update` theo delta() (nên dừng khi pause và hitstop, chậm lại
theo time_set_scale()); module sprite vẽ chúng cùng sprite, theo `layer`. Cùng một lớp, hạt
vẽ sau sprite.

Hai cách phát:

| Cách | Làm thế nào | Dùng cho |
|---|---|---|
| Liên tục | `rate` > 0, `emitting = true` | Lửa, khói, mưa, vệt đuôi |
| Một loạt | njin::particles_burst() trên emitter có sẵn | Bắn tia lửa từ một khẩu súng |
| Một lần rồi tự hủy | njin::particles_spawn() | Nổ, bụi, máu: tạo entity, nổ, tự hủy khi hết hạt |

Entity do njin::particles_spawn() tạo thuộc scene đang chạy, nên rời scene là mất.

### Các trường chính

| Nhóm | Trường |
|---|---|
| Phát | `rate`, `emitting`, `max_particles`, `area` (vùng sinh quanh transform) |
| Chuyển động | `life`, `speed` (khoảng min–max), `angle` + `spread` (hướng và độ tỏa, độ), `gravity`, `drag`, `spin`, `local_space` |
| Hình ảnh | `size_start` → `size_end`, `size_jitter`, `color_start` → `color_end`, `shape` hoặc `texture` + `source`, `blend`, `layer` |

Hướng: 0 độ là sang phải, 90 là **xuống** (trục y hướng xuống), -90 là lên. Góc xoay của
transform được cộng vào `angle`, nên xoay entity là xoay luôn hướng phát.

`local_space = true` làm hạt đi theo emitter (lửa ở đuôi tên lửa); mặc định hạt ở lại trong
thế giới và để lại dấu vết.

`blend_additive` làm các hạt chồng lên nhau sáng rực lên: dùng cho lửa, tia lửa, phép thuật.

### CPU hay GPU

Engine tự chọn khi game khởi động. Máy có card đồ họa (OpenGL 3.3 trở lên, không phải bộ vẽ
bằng phần mềm như llvmpipe, SwiftShader hay Microsoft Basic Render Driver) thì hạt chạy trên
GPU; máy không có thì chạy trên CPU, đúng như cũ. Game không phải làm gì, và cả hai cách cho
gần như cùng một hình.

Trên GPU, CPU chỉ ghi lại trạng thái lúc sinh của từng hạt. Mỗi emitter có một vertex buffer
riêng, chỉ được ghi khi có hạt mới hoặc khi dọn hạt chết (khoảng mỗi 0,25 giây), nên một frame
không tốn việc gì theo từng hạt trên CPU. Vertex shader tính vị trí hiện tại bằng công thức
(gia tốc và lực cản có nghiệm đóng), và mỗi emitter được vẽ bằng **một lệnh instanced** thay vì
một lệnh vẽ cho mỗi hạt. Kết quả lệch so với CPU dưới nửa pixel ở 60 FPS, vì CPU cộng dồn từng
bước Euler còn GPU tính chính xác.

Thử trên Intel Iris Xe, bản Release, hạt hình tròn, 10.000 hạt sống cùng lúc: khoảng 42 ms mỗi
frame trên CPU, khoảng 1 ms trên GPU (phép thử giới hạn ở 1000 FPS, nên đó là cận dưới). Ngay cả
200 emitter nhỏ, mỗi cái 20 hạt, GPU vẫn nhanh hơn nhiều, nên không có ngưỡng "emitter đủ lớn".

Vài điều cần biết:

- njin::particle_emitter::gpu cho biết emitter đang chạy ở đâu. Khi nó là `true`, `pos`,
  `velocity`, `rot` trong `particles` giữ giá trị lúc sinh, và `age` là **thời điểm sinh** theo
  đồng hồ riêng của emitter chứ không phải tuổi. `particles` còn giữ cả hạt chết chưa được dọn,
  nên đếm hạt sống bằng inspector (trường `alive`) hoặc kiểm tra `gpu` trước khi đọc trực tiếp.
- Emitter chỉ đổi nơi chạy khi hết hạt, nên hạt đang bay không bị giật.
- Shader hậu kỳ của game (njin::camera_set_post_shader()) chạy trên cả khung hình đã vẽ xong,
  nên vẫn thấy hạt GPU như mọi thứ khác. Riêng hạt hình tròn (không có texture) trên GPU có
  mép khử răng cưa, còn CPU vẽ đa giác không khử.
- njin::particles_set_backend() với `particle_backend_cpu` ép chạy trên CPU, để so sánh hoặc
  tìm lỗi. njin::particles_gpu_available() cho biết máy này dùng được GPU không.

Emitter nằm ngoài camera không được vẽ (mô phỏng vẫn chạy để hạt đúng chỗ khi quay lại). Engine
biết vùng hạt có thể tới từ nơi chúng được sinh, tốc độ, tuổi thọ và gia tốc, nên emitter chỉ
bị bỏ qua khi chắc chắn không hạt nào trong khung hình.

### Mẫu có sẵn

Trong `namespace njin::fx`, mỗi hàm trả về một emitter đã chỉnh sẵn, sửa tùy ý trước khi dùng:

| Mẫu | Kiểu phát | Gợi ý |
|---|---|---|
| njin::fx::explosion() | Một lần | 40–80 hạt |
| njin::fx::sparks() | Một lần | 10–25 hạt, chỉnh `angle`/`spread` theo hướng va chạm |
| njin::fx::dust() | Một lần | 6–12 hạt dưới chân khi chạy, tiếp đất |
| njin::fx::debris() | Một lần | 8–16 hạt khi vỡ thùng, vỡ đá |
| njin::fx::splash() | Một lần | Máu hoặc nước bắn theo hướng đòn đánh |
| njin::fx::sparkle() | Cả hai | Nhặt đồ, hồi máu, phép thuật |
| njin::fx::smoke() | Liên tục | Ống khói, xe cháy |
| njin::fx::fire() | Liên tục | Đuốc, đống lửa |
| njin::fx::rain(), njin::fx::snow() | Liên tục | Gắn lên entity đi theo camera, `area` rộng bằng màn hình |
| njin::fx::trail() | Liên tục | Gắn lên đạn, tên lửa, phi tiêu |

## Hiệu ứng màn hình và thời gian

| Hàm | Việc làm | Giá trị hay dùng |
|---|---|---|
| njin::camera_shake() | Rung camera. Cộng dồn, tự giảm dần | 0.2 bước chân nặng, 0.4 trúng đòn, 0.8 vụ nổ |
| njin::hitstop() | Dừng hình: delta() bằng 0 trong chốc lát | 0.03–0.12 giây |
| njin::screen_flash() | Nháy cả màn hình một màu rồi mờ dần | trắng khi nổ, đỏ khi bị thương |
| njin::sprite_flash() | Tô sprite thành một màu (thường là trắng) | 0.1 giây khi trúng đòn |

Rung camera chỉ dịch **hình vẽ ra**: transform của camera, njin::camera_active(),
njin::w2scr() và njin::scr2w() không đổi, nên bấm chuột vẫn trúng chỗ. Cách rung chỉnh bằng
njin::camera_shake_config().

Rung, nháy màn hình và hitstop tính theo **giờ thật**, nên camera vẫn rung trong lúc hitstop.
Nháy sprite tính theo delta(), nên nó "đứng hình" cùng game trong hitstop, đúng như mong đợi.

Nháy sprite khác `sprite.tint`: tint chỉ **nhân** màu nên không thể làm sprite sáng trắng lên;
nháy thay hẳn màu từng điểm ảnh nhưng giữ hình dáng sprite.

Xem thêm @ref post_processing cho các hiệu ứng toàn màn hình như vignette đỏ khi máu thấp.
