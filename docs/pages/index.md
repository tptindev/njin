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
| Vẽ ảnh, dùng shader | @subpage rendering |
| Làm hiệu ứng toàn màn hình | @subpage post_processing |
| Phát tiếng động và nhạc nền | @subpage audio |
| Ghi log | @subpage logging |
| Hiểu cách engine được tổ chức bên trong | @subpage architecture |
| Tra cứu từng hàm | [Nhóm API](topics.html) |

## Một chương trình nhỏ nhất

@include minimal_main.cpp

Hàm `njin_run` chạy vòng lặp đến khi cửa sổ đóng. Mọi logic của game nằm trong
**module**, xem @ref modules_systems.

## Cấu trúc mã nguồn

```
src/
  engine/
    api/        header công khai. Chỉ include njin.h là đủ
    runtime/    phần cài đặt, dùng raylib. Game không include trực tiếp
  games/
    sandbox/    game mẫu
```
