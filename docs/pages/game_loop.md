# Vòng lặp game {#game_loop}

run() chạy vòng lặp sau:

```mermaid
flowchart TD
  S([phase_startup<br/>một lần]):::once --> L{Cửa sổ còn mở?}:::decide
  L -- có --> T[Cập nhật thời gian và đọc input]:::engine
  T --> SC[Chuyển scene nếu có yêu cầu]:::engine
  SC --> U1[phase_pre_update]:::update
  U1 --> FX[phase_fixed_update<br/>0..n lần]:::fixed
  FX --> U2[phase_update]:::update
  U2 --> U3[phase_post_update]:::update
  U3 --> E[Phát event đã enqueue]:::engine
  E --> B[Bắt đầu vẽ và xóa nền]:::engine
  B --> R1[phase_pre_render]:::render
  R1 --> R2[phase_render]:::render
  R2 --> R3[phase_post_render]:::render
  R3 --> X[Kết thúc vẽ]:::engine
  X --> L
  L -- không --> D([phase_shutdown<br/>một lần]):::once
```

## Đầu mỗi frame

Trước phase đầu tiên, engine:

1. Cập nhật thời gian: delta() là thời gian của frame trước (đã nhân tốc độ, bằng 0 khi
   tạm dừng), elapsed() là thời gian từ lúc mở cửa sổ. Xem @ref time.
2. Đọc trạng thái input (xem @ref input). Trạng thái này **không đổi** trong suốt frame.
3. Chuyển scene nếu frame trước có gọi scene_set() (xem @ref scenes).

## Nhóm update

`phase_pre_update`, `phase_update`, `phase_post_update` là nơi đặt logic. Cách
chia thông thường:

- **pre_update**: đọc input, chuẩn bị dữ liệu
- **update**: logic chính, di chuyển, va chạm
- **post_update**: việc phụ thuộc kết quả của update, ví dụ camera đi theo người chơi

Ngay sau `phase_post_update`, các event đã xếp hàng bằng `events(ctx).enqueue`
được phát (xem @ref ecs).

## Nhóm render

`phase_pre_render`, `phase_render`, `phase_post_render` chạy giữa lúc bắt đầu và
kết thúc vẽ. Module camera của engine chia chúng thành hai không gian:

| Phase | Không gian | Chịu ảnh hưởng của camera |
|---|---|---|
| `phase_pre_render` | Thế giới | Có |
| `phase_render` | Thế giới | Có |
| `phase_post_render` | Màn hình | Không |

Vì vậy: vẽ nhân vật, bản đồ ở `phase_render`; vẽ UI ở `phase_post_render` để nó
không bị camera dịch chuyển hay phóng to. Xem @ref camera.

## Startup và shutdown

- `phase_startup` chạy **một lần**, sau khi cửa sổ đã mở và trước frame đầu tiên. Đây là nơi
  tạo entity, nạp texture, shader và gắn phím.
- `phase_shutdown` chạy **một lần** sau khi cửa sổ đóng.

Tài nguyên (texture, shader, render texture, sound, music) được giải phóng tự động khi gọi
destroy(), bạn không bắt buộc phải unload trong `phase_shutdown`.
