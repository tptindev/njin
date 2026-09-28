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

## Tài liệu chia thành 7 phần

Các trang xếp theo **độ khó tăng dần**: đi từ Phần 1 xuống Phần 7, hoặc nhảy thẳng tới phần bạn cần. Thanh bên
trái cũng hiện đúng cây này.

| Phần | Mức | Bạn được gì |
|---|---|---|
| @subpage part_start | Người mới | Cài môi trường, chạy chương trình đầu tiên, có ngay một nhân vật chạy được. Kèm 13 bài nền về C, C++, CMake, shader |
| @subpage part_core | Cơ bản | Hiểu module, system, một frame chạy thế nào, entity và component, nhập liệu, thời gian, toán, log |
| @subpage part_visual | Cơ bản | Vẽ hình và chữ, sprite, animation, camera, particle, pixel art |
| @subpage part_world | Trung cấp | Bản đồ ô vuông, Tiled và LDtk, va chạm, prefab; ghép lại thành game platformer và top-down |
| @subpage part_ui_audio | Trung cấp | Tiếng động và nhạc, menu, hộp thoại, đa ngôn ngữ |
| @subpage part_ship | Trung cấp | Chia màn chơi, lưu game, cài đặt của người chơi, cửa sổ, đọc game mẫu, đóng gói |
| @subpage part_advanced | Nâng cao | Shader, instancing, hậu kỳ, sinh bản đồ tự động, debug bằng inspector, kiến trúc bên trong |

Ngoài các phần trên: @subpage cheatsheet là bảng tra nhanh "muốn làm X thì dùng gì", và
[Nhóm API](topics.html) tra cứu từng hàm.

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
