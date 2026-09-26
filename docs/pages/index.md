# njin {#mainpage}

njin là một engine game 2D nhỏ viết bằng C++20, dành riêng cho hai thể loại:
**top-down** (hành động, phiêu lưu, RPG, bắn súng nhìn từ trên xuống) và **platformer**
(màn hình ngang, nhảy qua các bục). Nó dùng **EnTT** cho ECS và
**raylib** cho cửa sổ, đồ họa, nhập liệu. raylib được giấu hoàn toàn: game chỉ
include `njin.h` và không bao giờ thấy raylib.

Trang này là tài liệu để **dùng** njin và để **hiểu** nó.

**Mã nguồn:** <https://github.com/tptindev/njin>. Báo lỗi và góp ý ở mục Issues của repo. Lịch sử
phiên bản nằm trong `CHANGELOG.md` ở thư mục gốc.

## Lộ trình cho người mới

Chưa quen C, C++, CMake hay shader? Đọc nhóm 13 bài @ref learn trước (bỏ qua nếu đã biết). Rồi đi theo thứ tự này:

1. @ref setup : cài môi trường (bỏ qua nếu đã có trình biên dịch C++20, CMake, Ninja và Git)
2. @ref getting_started : build và chạy chương trình đầu tiên
3. @ref first_jump (platformer) hoặc @ref first_walk (top-down) : một nhân vật chạy được trên bản đồ, dưới 50 dòng
4. @ref ecs : `entt::registry` và `view`, nếu các bài trên còn lạ. Mười thao tác là đủ
5. @ref sprites và @ref animation : thay hình chữ nhật bằng nhân vật có ảnh
6. @ref level : vẽ màn chơi trong Tiled hoặc LDtk thay vì viết từng ô bằng code
7. @ref audio, @ref ui, @ref dialog : tiếng, menu, hộp thoại
8. @ref samples : đọc một game đầy đủ
9. @ref cheatsheet : tra khi cần biết "muốn làm X thì dùng hàm nào"

## Đọc từ đâu

| Bạn muốn | Đọc |
|---|---|
| Chưa quen C, C++, CMake hay shader: 13 bài học trước khi làm game | @subpage learn |
| Cài trình biên dịch, CMake, Ninja trên Windows, Linux, macOS | @subpage setup |
| Build và chạy được một chương trình | @subpage getting_started |
| Có ngay một nhân vật chạy và nhảy được trên bản đồ, trong 50 dòng | @subpage first_jump |
| Có ngay một nhân vật đi 8 hướng và lướt được trên bản đồ, trong 50 dòng | @subpage first_walk |
| Biết "muốn làm X thì dùng hàm nào" | @subpage cheatsheet |
| Hiểu module, system, phase | @subpage modules_systems |
| Biết một frame chạy như thế nào | @subpage game_loop |
| Làm việc với entity, component, event | @subpage ecs |
| Điều khiển camera | @subpage camera |
| Xử lý phím và action | @subpage input |
| Vẽ hình, chữ, font tiếng Việt | @subpage drawing |
| Vẽ ảnh, dùng shader, sửa ảnh và shader khi game đang chạy | @subpage rendering |
| Nhân vật có animation | @subpage sprites |
| Animation từ Aseprite, máy trạng thái idle/run/jump | @subpage animation |
| Nổ, bụi, tia lửa, rung camera, hitstop | @subpage particles |
| Mẫu entity (prefab), gắn vũ khí vào nhân vật | @subpage prefabs |
| Bản đồ ô vuông, va chạm với bản đồ | @subpage tilemap |
| Vẽ màn chơi bằng Tiled hoặc LDtk | @subpage level |
| Đạn trúng quái, nhặt đồ, tường chắn, raycast | @subpage collision |
| Làm hiệu ứng toàn màn hình: bloom, CRT, vignette | @subpage post_processing |
| Phát tiếng động và nhạc nền | @subpage audio |
| Chia game thành menu, màn chơi, game over; chuyển cảnh mờ dần | @subpage scenes |
| Vật lý ổn định, tạm dừng, slow motion, timer, tween | @subpage time |
| Toán vec2, va chạm, số ngẫu nhiên | @subpage math |
| Cửa sổ, toàn màn hình, lưu game | @subpage window_files |
| Lưu game, file cấu hình bằng JSON | @subpage json |
| Menu, popup, toast, thanh trượt, dùng được với tay cầm | @subpage ui |
| Platformer: chạy, nhảy có coyote time, dốc, bục một chiều, nhảy tường, camera bám | @subpage platformer |
| Top-down: đi 8 hướng, lướt, quái đuổi theo A*, sắp theo Y | @subpage topdown |
| Màn hình ảo cho pixel art; hẹn giờ và tween theo entity | @subpage screen_timers |
| Hộp thoại, chữ chạy, lựa chọn; đa ngôn ngữ | @subpage dialog |
| Âm lượng từng kênh, đổi phím, rung tay cầm, lưu cài đặt | @subpage settings |
| Game mẫu, đóng gói bản phát hành | @subpage samples |
| Ghi log | @subpage logging |
| Xem FPS, entity, collider, log trong một cửa sổ riêng | @subpage debug |
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
    pong/       game Pong hoàn chỉnh
  tools/
    inspector/  njin_inspector, công cụ debug chạy cạnh game
```
