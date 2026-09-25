# Toán học, va chạm, số ngẫu nhiên {#math}

Các phần này chỉ gồm header, không phụ thuộc engine: dùng được cả ngoài system.

@include math_collide.cpp

## vec2

njin::vec2 hỗ trợ đầy đủ phép toán:

```cpp
njin::vec2 a{3, 4}, b{1, 2};
a + b;  a - b;  a * 2.0f;  2.0f * a;  a / 2.0f;  -a;
a * b;  // nhân từng thành phần
a += b; a *= 0.5f;
a == b;
```

| Hàm | Trả về |
|---|---|
| njin::dot(), njin::cross() | Tích vô hướng, tích có hướng 2D |
| njin::length(), njin::length_sq(), njin::distance() | Độ dài, bình phương độ dài, khoảng cách |
| njin::normalize() | Vector độ dài 1 (`{0,0}` giữ nguyên) |
| njin::rotate(), njin::from_angle(), njin::angle_of() | Xoay, vector từ góc, góc của vector |
| njin::lerp(), njin::clamp(), njin::move_toward() | Nội suy, giới hạn, tiến về đích |

**Góc** tính bằng độ, **theo chiều kim đồng hồ trên màn hình** (vì trục y hướng xuống):
0 độ là sang phải, 90 độ là xuống dưới. Cùng quy ước với njin::transform::rot.

njin::rect (góc trên trái + kích thước) và njin::circle là hai hình cơ bản.
njin::rect_from_center() và njin::rect_center() đổi qua lại với tâm.

## Va chạm

**Kiểm tra chồng nhau** (đúng/sai): njin::point_in_rect(), njin::point_in_circle(),
njin::rects_overlap(), njin::circles_overlap(), njin::circle_rect_overlap().

**Va chạm có đẩy ra** trả về njin::contact: njin::collide_rects(), njin::collide_circles(),
njin::collide_circle_rect(). Khi `hit` đúng, dời vật thứ nhất một đoạn `normal * depth`
thì hai vật vừa chạm mép:

```cpp
const njin::contact hit = njin::collide_rects(player, wall);
if (hit.hit)
  player.pos += hit.normal * hit.depth;
```

njin::reflect() phản xạ vận tốc qua pháp tuyến: bóng nảy khỏi tường.

@note Chỉ chạm mép thì **không** tính là chồng nhau. Nhờ vậy vật vừa được đẩy ra không bị
coi là vẫn còn va chạm ở frame sau.

## Số ngẫu nhiên

njin::random() trả về bộ sinh dùng chung của engine, gieo hạt giống từ thời gian lúc mở
game:

```cpp
njin::rng &r = njin::random(ctx);
r.range(0.0f, 1.0f);   // số thực trong [0, 1)
r.range(1, 6);         // xúc xắc: 1 đến 6, gồm cả 6
r.chance(0.25f);       // đúng với xác suất 25%
r.direction();         // vector độ dài 1, hướng bất kỳ
r.point_in(area);      // điểm bất kỳ trong một hình chữ nhật
```

njin::rng **lặp lại được**: cùng hạt giống thì cùng dãy số. Tự tạo một bộ riêng khi cần
một dãy độc lập, ví dụ sinh bản đồ từ một mã số:

```cpp
njin::rng level_rng(seed);
```
