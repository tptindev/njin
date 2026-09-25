# Prefab và transform cha–con {#prefabs}

## Prefab

Prefab là **mẫu để tạo entity**: một hàm dựng có tên, gắn component vào entity vừa tạo.
Viết hàm dựng một lần, rồi spawn bao nhiêu lần cũng được, kể cả tìm theo tên.

@include prefabs.cpp

njin::prefab_spawn() làm lần lượt:

1. tạo entity;
2. gắn njin::transform bằng tham số `at`;
3. gắn njin::scene_owned trỏ tới scene đang chạy (nếu có, và nếu `prefab_desc::scene_owned`
   không bị tắt), nên rời scene là entity tự mất;
4. gọi hàm dựng.

Muốn khác đi một chút (máu nhiều hơn, màu khác) thì sửa trên entity trả về:

```cpp
const entt::entity boss = njin::prefab_spawn(ctx, "goblin", {.pos = {500, 300}, .scale = 3});
njin::world(ctx).get<health>(boss).hp = 50;
```

| Hàm | Việc làm |
|---|---|
| njin::prefab_register() | Đăng ký prefab. Tên đã có thì trả về cái cũ |
| njin::prefab_find() | Tìm theo tên |
| njin::prefab_spawn() | Tạo entity từ prefab (handle hoặc tên) |
| njin::prefab_spawn_child() | Tạo entity từ prefab và gắn làm con của entity khác |

## Transform cha–con

Gắn njin::child_of vào một entity để nó đi theo entity cha: vũ khí trong tay, bánh xe, cái
bóng dưới chân, emitter lửa ở đuôi tên lửa.

```cpp
reg.emplace<njin::child_of>(gun, njin::child_of{.parent = player, .local = {.pos = {12, -4}}});
```

Mỗi frame, trong `phase_post_update`, module hierarchy của engine **ghi đè** transform của
con bằng transform của cha kết hợp với `local` (xem njin::transform_combine()):

| Của con | Được tính |
|---|---|
| `pos` | `cha.pos + xoay(local.pos × cha.scale, cha.rot)` |
| `rot` | `cha.rot + local.rot` |
| `scale` | `cha.scale × local.scale` |

Vì vậy:

- **Di chuyển con bằng `local`**, không phải bằng transform của nó (sẽ bị ghi đè).
- Cha có thể là con của entity khác; engine tính từ gốc xuống.
- Khi cha bị hủy, con bị hủy theo. Đặt `destroy_with_parent = false` để con ở lại, tách ra
  tại vị trí cuối cùng.
- System trong `phase_update` đọc transform của con sẽ thấy giá trị của frame trước. Cần
  chính xác ngay thì tự tính bằng njin::transform_combine().
- Gắn một entity đang đứng sẵn trong thế giới vào cha mà không làm nó nhảy chỗ: dùng
  njin::transform_relative() để tính `local`.

Lật hướng (`sprite.flip_x`) không tự lật con. Khi nhân vật quay trái, đổi dấu `local.pos.x`
của vũ khí.
