# njin {#mainpage}

njin là một engine game 2D nhỏ viết bằng C++20. Nó dùng **EnTT** cho ECS và
**raylib** cho cửa sổ, đồ họa, nhập liệu. raylib được giấu hoàn toàn: game chỉ
include `njin.h` và không bao giờ thấy raylib.

Trang này là tài liệu để **dùng** njin và để **hiểu** nó.

## Đọc từ đâu

| Bạn muốn | Đọc |
|---|---|
| Build và chạy được một chương trình | @subpage getting_started |
| Hiểu module, system, phase | @subpage modules_systems |
| Biết một frame chạy như thế nào | @subpage game_loop |
| Làm việc với entity, component, event | @subpage ecs |
| Điều khiển camera | @subpage camera |
| Xử lý phím và action | @subpage input |
| Vẽ hình, chữ, font tiếng Việt | @subpage drawing |
| Vẽ ảnh, dùng shader | @subpage rendering |
| Nhân vật có animation | @subpage sprites |
| Animation từ Aseprite, máy trạng thái idle/run/jump | @subpage animation |
| Nổ, bụi, tia lửa, rung camera, hitstop | @subpage particles |
| Mẫu entity (prefab), gắn vũ khí vào nhân vật | @subpage prefabs |
| Bản đồ ô vuông, va chạm với bản đồ | @subpage tilemap |
| Làm hiệu ứng toàn màn hình: bloom, CRT, vignette | @subpage post_processing |
| Phát tiếng động và nhạc nền | @subpage audio |
| Chia game thành menu, màn chơi, game over; chuyển cảnh mờ dần | @subpage scenes |
| Vật lý ổn định, tạm dừng, slow motion, timer, tween | @subpage time |
| Toán vec2, va chạm, số ngẫu nhiên | @subpage math |
| Cửa sổ, toàn màn hình, lưu game | @subpage window_files |
| Ghi log | @subpage logging |
| Hiểu cách engine được tổ chức bên trong | @subpage architecture |
| Tra cứu từng hàm | [Nhóm API](topics.html) |

## Một chương trình nhỏ nhất

@include minimal_main.cpp

Hàm `njin_run` chạy vòng lặp đến khi cửa sổ đóng. Mọi logic của game nằm trong
**module**, xem @ref modules_systems.

## Một game hoàn chỉnh

`src/games/pong` là một game Pong đầy đủ viết trên njin: menu, chơi với máy hoặc hai
người, tạm dừng, slow motion ở điểm quyết định, âm thanh tự sinh, lưu kỷ lục, cửa sổ
đổi kích thước được. Đọc từ trên xuống như một chuyến tham quan engine. Build target
`njin_pong` rồi chạy `build\bin\njin_pong.exe`.

## Cấu trúc mã nguồn

```
src/
  engine/
    api/        header công khai. Chỉ include njin.h là đủ
    runtime/    phần cài đặt, dùng raylib. Game không include trực tiếp
  games/
    sandbox/    game mẫu
```
