# Phần 2: Khái niệm cốt lõi {#part_core}

**Mức: cơ bản.** Cần biết trước: đã làm xong @ref part_start (hoặc ít nhất @ref first_jump). Phần này giải thích
những thứ mọi game njin đều dùng, dù là platformer hay top-down: code của bạn nằm ở đâu, một frame chạy ra sao, dữ
liệu được tổ chức thế nào.

## Đi theo thứ tự này

| Thứ tự | Trang | Bạn được gì |
|---|---|---|
| 1 | @subpage modules_systems | Module, system, phase: logic của game được chia và đăng ký thế nào |
| 2 | @subpage game_loop | Một frame chạy những gì, theo thứ tự nào |
| 3 | @subpage ecs | Entity, component, event với EnTT. Mười thao tác là đủ dùng |
| 4 | @subpage input | Phím bàn phím, chuột, tay cầm, action, nhập văn bản |
| 5 | @subpage time | Delta time, tạm dừng, slow motion, fixed update cho vật lý ổn định |
| 6 | @subpage math | vec2, hình chữ nhật, hình tròn, nội suy, số ngẫu nhiên lặp lại được |
| 7 | @subpage logging | Ghi log theo mức độ với macro `NJIN_*` |

Ba trang đầu là **bắt buộc**: các phần sau đều giả định bạn đã biết chúng. Bốn trang còn lại đọc theo nhu cầu, nhưng
@ref input và @ref time nên đọc trước khi làm gameplay.

## Đọc xong thì làm gì

@ref part_visual để cho game có hình và chuyển động.
