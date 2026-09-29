# Phần 3: Hình ảnh và chuyển động {#part_visual}

**Mức: cơ bản.** Cần biết trước: @ref modules_systems, @ref game_loop và @ref ecs. Phần này đưa game từ hình chữ nhật
màu tới nhân vật có ảnh, animation, camera bám theo và hiệu ứng.

**Phạm vi: 2D.** API ở đây (draw_rect(), sprite, tilemap, camera 2D) chỉ vẽ trong không gian 2D. Làm
game 3D thì đọc @ref graphics_3d thay vào (Phần 7); hạt và hiệu ứng của phần này (@ref particles) dùng
chung được cho cả 3D.

## Đi theo thứ tự này

| Thứ tự | Trang | Bạn được gì |
|---|---|---|
| 1 | @subpage drawing | Vẽ hình khối và chữ, font tiếng Việt, ba phase vẽ và không gian của từng phase |
| 2 | @subpage sprites | Sprite là component: gắn ảnh vào entity, sprite sheet, clip |
| 3 | @subpage animation | Animation từ Aseprite, máy trạng thái idle/run/jump |
| 4 | @subpage camera | Camera là entity: bám theo nhân vật, kẹp trong màn chơi, đổi tọa độ thế giới và màn hình |
| 5 | @subpage particles | Nổ, bụi, tia lửa, rung camera, hitstop: những thứ làm game "có lực" |
| 6 | @subpage screen_timers | Màn hình ảo cho pixel art; hẹn giờ và tween theo entity |

Trang 1 đến 4 là phần thiết yếu. @ref particles và @ref screen_timers là lớp bóng bẩy: đọc khi game đã chạy được.

## Đọc xong thì làm gì

@ref part_world để dựng thế giới cho nhân vật đi trong đó. Muốn tự viết shader hoặc vẽ hàng nghìn sprite bằng một
lệnh vẽ thì xem @ref part_advanced.
