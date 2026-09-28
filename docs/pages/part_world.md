# Phần 4: Thế giới và gameplay {#part_world}

**Mức: trung cấp.** Cần biết trước: @ref part_core và @ref sprites, @ref camera. Phần này dựng màn chơi, cho các vật
va chạm với nhau, rồi ghép mọi thứ thành hai thể loại của njin.

## Đi theo thứ tự này

**Màn chơi và va chạm**

| Thứ tự | Trang | Bạn được gì |
|---|---|---|
| 1 | @subpage tilemap | Bản đồ ô vuông chia chunk, va chạm với bản đồ |
| 2 | @subpage level | Vẽ màn chơi trong Tiled hoặc LDtk thay vì viết từng ô bằng code |
| 3 | @subpage collision | Collider, đạn trúng quái, nhặt đồ, tường chắn, raycast |
| 4 | @subpage prefabs | Mẫu entity để spawn, transform cha-con, gắn vũ khí vào nhân vật |

**Ghép thành game** (chọn thể loại bạn làm, hoặc đọc cả hai)

| Thứ tự | Trang | Bạn được gì |
|---|---|---|
| 5 | @subpage platformer | Chạy, nhảy có coyote time, dốc, bục một chiều, nhảy tường, camera bám |
| 5 | @subpage topdown | Đi 8 hướng, lướt, quái đuổi theo A\*, sắp theo Y để cây che nhân vật đúng chỗ |

@ref platformer và @ref topdown ghép lại các trang trước nó, nên đọc sau cùng.

## Đọc xong thì làm gì

@ref part_ui_audio để game có tiếng, menu và hộp thoại.
