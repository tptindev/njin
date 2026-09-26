# Entity, component và event {#ecs}

njin dùng **EnTT** cho ECS. EnTT là một phần của API công khai: bạn dùng trực
tiếp `entt::registry` và `entt::dispatcher`, engine không bọc lại chúng.

@note EnTT có tài liệu riêng rất đầy đủ tại
https://github.com/skypjack/entt/wiki. Trang này chỉ nói phần bạn cần khi dùng nó trong njin.

## Chỉ cần nhớ chừng này

Chưa biết ECS cũng làm được game với njin. Coi `entt::entity` là **một con số định danh**, không
phải một object: nó không có hàm, không chứa dữ liệu. Dữ liệu nằm ở component, lấy ra qua
registry bằng định danh đó. Mười thao tác dưới đây đủ cho phần lớn game:

| Muốn | Viết |
|---|---|
| Lấy registry | `entt::registry &reg = njin::world(ctx);` |
| Tạo một entity | `const entt::entity e = reg.create();` |
| Gắn component | `reg.emplace<hp>(e, hp{.value = 3});` |
| Đọc hoặc sửa component (chắc chắn có) | `reg.get<hp>(e).value -= 1;` |
| Đọc nếu có | `if (hp *h = reg.try_get<hp>(e)) h->value += 10;` |
| Entity có component này không | `reg.all_of<enemy_tag>(e)` |
| Hủy entity | `reg.destroy(e);` |
| Entity này còn sống không | `reg.valid(e)` |
| "Chưa có entity nào" | `entt::null` |
| Tìm entity duy nhất có tag (người chơi) | `reg.view<player_tag>().front()` |

Lưu **`entt::entity`**, không lưu con trỏ hay tham chiếu tới component. Trước khi dùng một entity đã lưu từ
lâu (quái đã chết? đạn đã hủy?), hỏi `reg.valid(e)`.

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

Còn một cách viết khác, gọn hơn khi không cần entity: hàm nhận thẳng các component.

```cpp
registry.view<njin::transform, velocity>().each([&](njin::transform &tr, velocity &vel) {
  tr.pos += vel.value;
});
```

Cần entity (để hủy, để đọc component khác) thì dùng vòng `for` ở trên, nhớ tên đầu tiên trong `[...]`
là entity.

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

## Lỗi thường gặp

**`only 3 names provided for structured binding ... decomposes into 4 elements`**
: `view<A, B, C>().each()` cho ra **entity cộng ba component**, tức bốn phần tử. Viết
  `for (auto [e, a, b, c] : view.each())`, hoặc dùng dạng hàm nhận component (không có entity).
  Nếu không dùng `e`, ghi `(void)e;` để tránh cảnh báo biến không dùng.

**`invalid use of void expression` khi gọi `get` hay `try_get` trên một tag**
: Component rỗng (`struct player_tag {};`) không có dữ liệu để trả về. Hỏi bằng `reg.all_of<player_tag>(e)`
  và gắn bằng `reg.emplace<player_tag>(e)`.

**`Assertion failed: ... Set does not contain entity` lúc chạy (bản Debug)**
: `reg.get<X>(e)` trên một entity không có `X`. `get` yêu cầu chắc chắn có; chưa chắc thì dùng
  `try_get` hoặc `all_of`. Bản Release không kiểm tra và hành vi không xác định, nên sửa ngay khi
  gặp trong Debug.

**Hủy quái trong lúc đang duyệt chúng**
: Gom vào một `std::vector<entt::entity>` rồi hủy sau khi vòng lặp kết thúc.

Cần tạo nhiều entity giống nhau (quái, đạn, vật phẩm)? Đừng lặp lại chuỗi `emplace`: viết một hàm dựng một
lần với njin::prefab_register(), xem @ref prefabs.

Muốn thấy các thao tác này trong một chương trình chạy được: @ref first_jump và @ref first_walk.

## Lưu ý

- Đừng giữ tham chiếu tới component qua nhiều frame: thêm hoặc bớt component có
  thể làm dữ liệu dời chỗ. Lấy lại từ registry mỗi frame.
- Hủy entity trong lúc đang duyệt `view` cần cẩn thận, xem tài liệu EnTT về việc này.
