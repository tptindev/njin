# Game mẫu và đóng gói {#samples}

Ba chương trình trong `src/games` để đọc, chạy và sửa.

| Game | Là gì | Đọc để học |
|---|---|---|
| `njin_platformer` | *Mầm Leo Núi*: hai màn Tiled với dốc, bục một chiều, bục di chuyển, nhảy tường, quái, checkpoint, hộp thoại | @ref platformer |
| `njin_topdown` | *Rừng Cổ Thạch*: bản đồ Tiled có nước động, kiếm, quái đuổi theo A\*, cây che nhân vật, rương, hộp thoại | @ref topdown |
| `njin_debug_demo` | Bóng nảy để thử inspector: mỗi phím tốn CPU, RAM hay GPU một chút và inspector hiện ra ngay | @ref debug |

Cả hai game đầu có menu chính, tạm dừng, cài đặt (âm lượng, toàn màn hình, ngôn ngữ, đổi phím cho bàn
phím và tay cầm), hai ngôn ngữ (Việt, Anh), nhạc và tiếng, và màn hình ảo 640 x 360. Phần dùng chung
nằm ở `src/games/shared` (menu cài đặt); mọi hình, tiếng, bản đồ đều sinh ra từ script Python
`tools/make_assets.py` của từng game, nên không có file nhị phân không rõ nguồn.
Font Be Vietnam Pro (giấy phép SIL OFL, kèm `OFL.txt`) dùng cho chữ tiếng Việt.

Mỗi game có assets riêng nên chạy từ thư mục riêng của nó, `build/bin/<game>/` (exe và `assets` nằm cạnh nhau). Chạy:

@code{.bat}
run_inspected.bat              rem debug demo cùng inspector
run_inspected.bat platformer   rem một game khác, cùng inspector
build\bin\topdown\njin_topdown.exe
@endcode

## Đóng gói bản phát hành

Trong CMakeLists của game:

@code{.cmake}
njin_add_assets(my_game assets)     # chép assets/ cạnh exe mỗi lần build
njin_package(my_game
  NAME "Tên game"
  VERSION 1.0.0
  ICON icon.ico                     # icon của exe và thanh tác vụ
  ASSETS assets
  FILES README.txt LICENSE)
@endcode

@code{.bat}
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target my_game_dist
@endcode

Kết quả là `build-release/dist/my_game-1.0.0.zip` chứa exe, thư mục `assets` và các file kèm theo. Trên
Windows, exe được gắn icon và thông tin phiên bản, và **không có cửa sổ console** trừ bản Debug (cần
console để đọc log). Game tìm `assets/...` cạnh exe dù chạy từ thư mục nào.

Nhớ tắt cổng debug trong bản phát hành: `debug_server_start()` chỉ trong `#ifndef NDEBUG`.

@note Bản build cho Linux và macOS chưa được thử; code mạng của inspector có nhánh POSIX nhưng chưa
được biên dịch. Cũng chưa thử với tay cầm thật.
