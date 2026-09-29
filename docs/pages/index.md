# njin {#mainpage}

njin là một engine game nhỏ viết bằng C++20, làm cả game **2D** lẫn **3D**. Nó dùng **EnTT** cho
ECS và **raylib** cho cửa sổ, đồ họa, nhập liệu. raylib được giấu hoàn toàn: game chỉ include
`njin.h` và không bao giờ thấy raylib.

Phần lớn API (module, system, ECS, nhập liệu, thời gian, âm thanh, UI, particle/hiệu ứng, debug)
**dùng chung** cho 2D và 3D. Vẽ hình, sprite, tilemap, camera và va chạm ở Phần 2–4 là **2D**
(`njin_draw.h`, `njin_camera.h`, `njin_collision.h`...); @ref graphics_3d là trang **3D** riêng
(`njin_3d.h`): camera phối cảnh, hình khối và SDF, model glTF, ánh sáng có bóng đổ, instancing.
Một game chỉ dùng phần nào mình cần; hai phần dựng trên cùng ECS và cùng vòng lặp nên trộn được
(ví dụ HUD 2D vẽ đè lên cảnh 3D).

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
trái cũng hiện đúng cây này. Cột **Phạm vi** nói API của phần đó dùng cho loại game nào: "Chung" chạy
được với cả game 2D lẫn 3D (module, ECS, nhập liệu, âm thanh, UI, đóng gói...); "2D" là vẽ hình, sprite,
tilemap, camera và va chạm 2D; "3D" chỉ @ref graphics_3d, trang riêng cho `njin_3d.h` trong Phần 7.

| Phần | Mức | Phạm vi | Bạn được gì |
|---|---|---|---|
| @subpage part_start | Người mới | Chung | Cài môi trường, chạy chương trình đầu tiên, có ngay một nhân vật chạy được. Kèm 13 bài nền về C, C++, CMake, shader |
| @subpage part_core | Cơ bản | Chung | Hiểu module, system, một frame chạy thế nào, entity và component, nhập liệu, thời gian, toán, log |
| @subpage part_visual | Cơ bản | 2D | Vẽ hình và chữ, sprite, animation, camera, particle, pixel art |
| @subpage part_world | Trung cấp | 2D | Bản đồ ô vuông, Tiled và LDtk, va chạm, prefab; ghép lại thành game platformer và top-down |
| @subpage part_ui_audio | Trung cấp | Chung | Tiếng động và nhạc, menu, hộp thoại, đa ngôn ngữ |
| @subpage part_ship | Trung cấp | Chung | Chia màn chơi, lưu game, cài đặt của người chơi, cửa sổ, đọc game mẫu, đóng gói |
| @subpage part_advanced | Nâng cao | 2D + 3D | Shader, instancing, hậu kỳ, sinh bản đồ tự động, **@ref graphics_3d (3D)**, debug bằng inspector |

Sau bảy phần có @subpage part_appendix , gồm bảng tra nhanh "muốn làm X thì dùng gì", tra cứu từng hàm theo
nhóm API, và kiến trúc bên trong engine.

## Một chương trình nhỏ nhất

@include minimal_main.cpp

Hàm `run` chạy vòng lặp đến khi cửa sổ đóng. Mọi logic của game nằm trong
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
