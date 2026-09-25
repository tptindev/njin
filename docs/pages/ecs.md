# Entity, component và event {#ecs}

njin dùng **EnTT** cho ECS. EnTT là một phần của API công khai: bạn dùng trực
tiếp `entt::registry` và `entt::dispatcher`, engine không bọc lại chúng.

@note EnTT có tài liệu riêng rất đầy đủ tại
https://github.com/skypjack/entt/wiki. Trang này chỉ nói phần bạn cần khi dùng nó trong njin.

## Ba khái niệm

| Khái niệm | Là gì | Ví dụ |
|---|---|---|
| **Entity** | Một định danh, không chứa dữ liệu | `entt::entity` |
| **Component** | Một struct dữ liệu gắn vào entity | njin::transform, `velocity` |
| **System** | Hàm xử lý các entity có component nhất định | xem @ref modules_systems |

## Lấy registry

njin::world() trả về registry chứa mọi entity của game:

```cpp
entt::registry &registry = njin::world(ctx);
```

## Các thao tác thường dùng

```cpp
// Tạo entity và gắn component
const entt::entity e = registry.create();
registry.emplace<njin::transform>(e);

// Truy vấn: mọi entity có đủ các component này
auto view = registry.view<njin::transform, const velocity>();
for (auto [entity, tr, vel] : view.each()) {
  tr.pos.x += vel.value.x;   // tr là tham chiếu, sửa được
}

// Bỏ component, hoặc hủy cả entity
registry.remove<velocity>(e);
registry.destroy(e);
```

`const` trước kiểu component trong `view<...>` nghĩa là bạn chỉ đọc nó.

## Component có sẵn

Engine hiểu sẵn ba component:

| Component | Ý nghĩa |
|---|---|
| njin::transform | Vị trí, góc xoay, tỉ lệ |
| njin::camera_2d | Cấu hình camera (offset, zoom) |
| njin::camera_on | Tag đánh dấu camera đang dùng |

Xem @ref camera để biết cách dùng camera.

## Component của riêng bạn

Bất kỳ struct nào cũng là component. Không cần đăng ký:

@include movement.cpp

Component không có dữ liệu (như `struct player_tag {}`) dùng để **đánh dấu** entity.
Xem `player_tag` trong ví dụ camera ở @ref camera.

## Event

Khi hai system cần nói chuyện với nhau mà không gọi trực tiếp, dùng event qua
njin::events(), là một `entt::dispatcher`:

@include events.cpp

Điểm cần nhớ:

- Event là một struct bất kỳ.
- Đăng ký người nhận bằng `sink<Event>().connect<&hàm>()` (thường trong `setup`).
- `enqueue` **xếp hàng** event. Người nhận được gọi ngay sau `phase_post_update` của frame đó.
- Muốn gọi ngay lập tức thì dùng `trigger` thay cho `enqueue`.

## Lưu ý

- Đừng giữ tham chiếu tới component qua nhiều frame: thêm hoặc bớt component có
  thể làm dữ liệu dời chỗ. Lấy lại từ registry mỗi frame.
- Hủy entity trong lúc đang duyệt `view` cần cẩn thận, xem tài liệu EnTT về việc này.
