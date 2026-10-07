# Đường cong (spline) {#spline}

Trang này cho vật đi dọc một đường cong mượt: camera chạy trên ray trong cutscene, bục di chuyển, quái tuần tra,
đường bay của rồng, quỹ đạo đạn cong, đường đua. Mọi thứ khai báo trong `njin_spline.h`, chạy cho cả 2D
(njin::spline2d) lẫn 3D (njin::spline3d) với cùng tên hàm.

Cần biết trước: @ref math (vec2, vec3).

## Hai loại đường cong

| Loại | Đặt điểm thế nào | Hợp với |
|---|---|---|
| `spline_catmull_rom` (mặc định) | Đường đi **qua** mọi điểm | Đường ray, lối tuần tra: chấm điểm là xong |
| `spline_bezier` | Điểm 0, 3, 6... nằm trên đường; hai điểm giữa mỗi cặp là tay cầm kéo đường | Đường cần chỉnh dáng kỹ, như công cụ pen của trình vẽ |

Catmull-Rom mặc định dùng dạng **centripetal** (`alpha = 0.5`): khi các điểm cách nhau không đều, đường không
thắt nút hay vòng xoắn. `closed = true` nối điểm cuối về điểm đầu thành vòng kín.

```cpp
njin::spline3d rail;
rail.points = {{12, 4, 0}, {6, 6, 10}, {-8, 5, 9}, {-12, 3, -2}};
rail.closed = true;
njin::spline_bake(rail); // sau mỗi lần sửa điểm
```

## Đi theo tham số hay theo khoảng cách

spline_point() đọc đường theo tham số `t`: phần nguyên là đoạn, phần lẻ là vị trí trong đoạn; với Catmull-Rom,
`t = 2` là đúng điểm thứ 2. Nhưng đi đều theo `t` thì **không đều theo khoảng cách**: đoạn dài đi nhanh, đoạn
ngắn đi chậm.

Muốn đi đều (camera không giật, bục không lúc nhanh lúc chậm) thì đi theo khoảng cách: spline_bake() lập một bảng
độ dài, rồi spline_point_at() và spline_tangent_at() nhận khoảng cách tính từ đầu đường. spline_length() là độ
dài cả đường.

| Hàm | Trả lời |
|---|---|
| spline_point(), spline_tangent() | Điểm và hướng ở tham số `t` |
| spline_point_at(), spline_tangent_at() | Điểm và hướng ở một khoảng cách dọc đường |
| spline_t_at() | Tham số `t` ở một khoảng cách |
| spline_length() | Độ dài cả đường |
| spline_nearest() | Chỗ gần một điểm nhất trên đường, và khoảng cách dọc đường tới đó: bám vào ray, biết người chơi đã chạy được bao xa trên đường đua |
| spline_draw_debug() | Vẽ đường và điểm điều khiển bằng gizmo |

## Đi dọc đường mỗi frame

njin::spline_follower giữ trạng thái của một vật đang đi: đã đi bao xa, tốc độ, và đến cuối thì làm gì
(`spline_stop`, `spline_loop`, `spline_ping_pong`). Mỗi frame gọi spline_follow():

```cpp
njin::spline_follower mover{.speed = 4.0f, .end = njin::spline_loop};
// mỗi frame:
const njin::vec3 pos = njin::spline_follow(rail, mover, njin::delta(ctx));
const njin::vec3 dir = njin::spline_tangent_at(rail, mover.distance);
```

Với `spline_stop`, `mover.finished` thành true khi tới cuối. Với `spline_ping_pong`, `speed` đổi dấu mỗi lần
quay đầu. Đi ngược thì đặt `speed` âm.

## Ví dụ đầy đủ

Camera chạy vòng trên một ray kín và nhìn theo hướng ray; một bục đi tới đi lui dọc một đường Bezier; phím Tab
ẩn hiện các đường.

@include spline.cpp

## Giới hạn

- Hướng (tangent) chỉ là một vector: không có góc lăn (roll) dọc đường. Camera trên ray tự giữ `up` của nó.
- Bảng độ dài cần lập lại (spline_bake()) sau mỗi lần sửa điểm; thiếu bảng thì các hàm theo khoảng cách tự lập
  một bảng tạm mỗi lần gọi, đúng nhưng chậm.
