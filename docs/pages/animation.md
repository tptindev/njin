# Animation và máy trạng thái {#animation}

njin::sprite_anim (xem @ref sprites) đủ cho một dải frame chạy đều. Nhân vật thật cần
hơn thế: mỗi frame một thời lượng riêng, nhiều animation có tên, và luật chuyển giữa
chúng (đứng → chạy → nhảy → đánh). Phần này làm việc đó.

Ba khái niệm:

| Khái niệm | Là gì | Tạo bằng |
|---|---|---|
| **Sheet** | Ảnh + danh sách frame (vùng, thời lượng) + các **clip** có tên | njin::anim_sheet_load() (Aseprite) hoặc njin::anim_sheet_grid() |
| **Graph** | Máy trạng thái: các trạng thái (mỗi cái chạy một clip) và luật chuyển | njin::anim_graph_create() |
| **Animator** | Component trên entity: trạng thái hiện tại và tham số riêng | `reg.emplace<njin::animator>(e, {.graph = g})` |

Nhiều entity dùng chung một sheet và một graph; mỗi entity có animator riêng.

## Ví dụ

@include animation.cpp

## Xuất từ Aseprite

Trong Aseprite, đặt **tag** cho từng đoạn animation (idle, run...), rồi *File > Export Sprite
Sheet*:

- tab *Output*: bật **JSON Data**, chọn *Array* hoặc *Hash*, bật **Tags**;
- tab *Borders*: **tắt Trim** (frame bị cắt sẽ lệch vị trí; engine cảnh báo nếu gặp).

Hoặc bằng dòng lệnh:

```sh
aseprite -b hero.aseprite --sheet hero.png --data hero.json --list-tags --format json-array
```

njin::anim_sheet_load() đọc file JSON, nạp ảnh nằm cạnh nó, và biến mỗi tag thành một clip
cùng tên. Những gì lấy từ Aseprite:

| Trong Aseprite | Trong njin |
|---|---|
| Thời lượng từng frame | Giữ nguyên, tính bằng mili giây |
| Hướng của tag: Forward, Reverse, Ping-pong, Ping-pong Reverse | njin::anim_direction |
| Repeat của tag (∞ hoặc số lần) | `repeat` của clip: 0 là lặp mãi |

Không có tag nào thì có một clip tên `"default"` gồm mọi frame.

Sheet chia lưới đều (không dùng Aseprite) thì tạo bằng njin::anim_sheet_grid() rồi thêm clip
bằng njin::anim_sheet_add_clip().

## Máy trạng thái

Mỗi frame, trong `phase_post_update`, engine:

1. xét các chuyển trạng thái **theo thứ tự khai báo**, dùng cái đầu tiên thỏa mãn (tối đa
   một lần mỗi frame);
2. tắt mọi trigger;
3. cho clip chạy theo delta() nhân `animator.speed` và `speed` của trạng thái;
4. ghi `sprite.texture` và `sprite.source`.

Một chuyển trạng thái thỏa mãn khi:

- `from` đúng trạng thái hiện tại, hoặc `from` là null (từ mọi trạng thái, trừ chính `to`);
- mọi điều kiện trong `when` đúng;
- nếu có `after_finish`: clip hiện tại đã chạy hết ít nhất một lượt.

Vì xét theo thứ tự, hãy đặt chuyển quan trọng lên trước: bị đánh, chết, rồi mới đến đánh,
nhảy, chạy.

### Tham số

Điều kiện so sánh **tham số** với một giá trị. Tham số là số thực có tên, tự được tạo từ các
điều kiện (tối đa njin::anim_max_params cái). Game đặt chúng mỗi frame:

| Hàm | Dùng cho | So sánh bằng |
|---|---|---|
| njin::animator_set() | Số: tốc độ, máu | `anim_gt`, `anim_lt`, `anim_ge`, `anim_le`, `anim_eq`, `anim_ne` |
| njin::animator_set_bool() | Bật/tắt: đang đứng trên đất | `anim_true`, `anim_false` |
| njin::animator_trigger() | Sự kiện một lần: bấm nút đánh | `anim_trigger` |

Trigger chỉ sống **trong frame được bật**. Nó bị tiêu thụ khi làm chuyển trạng thái, và tự
tắt cuối frame nếu không dùng đến, nên một cú bấm lúc đang nhảy không bị "để dành" rồi bất
ngờ bật ra sau đó.

### Không dùng graph

Chỉ cần chạy clip theo tên thì gán `sheet` thay vì `graph` và gọi njin::animator_play():

```cpp
reg.emplace<njin::animator>(coin, njin::animator{.sheet = coin_sheet});
njin::animator_play(ctx, reg.get<njin::animator>(coin), "spin");
```

Giống njin::anim_play(), gọi mỗi frame được: đang chạy đúng clip đó thì không làm gì.
Với graph, njin::animator_play() nhảy thẳng tới một trạng thái (ví dụ khi hồi sinh).

## Đọc trạng thái

| Trường / hàm | Ý nghĩa |
|---|---|
| njin::animator_in() | Đang ở trạng thái (hoặc clip) có tên này không |
| njin::animator_current() | Tên trạng thái (hoặc clip) hiện tại |
| `animator.finished` | Clip không lặp đã chạy hết |
| `animator.state_time` | Đã ở trạng thái hiện tại bao lâu |
| `animator.frame` | Frame đang hiện trong sheet, để canh hitbox theo frame |
| njin::anim_clip_duration() | Tổng thời lượng một lượt của clip |
