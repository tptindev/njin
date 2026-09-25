# Particle và hiệu ứng {#particles}

Những thứ nhỏ làm game "có lực": bụi khi tiếp đất, tia lửa khi chém trúng, camera rung khi
nổ, khung hình dừng một nhịp khi đòn đánh trúng. njin có sẵn tất cả, và một bộ mẫu dùng ngay.

@include particles_fx.cpp

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
