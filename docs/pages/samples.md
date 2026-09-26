# Game mẫu và đóng gói {#samples}

Bốn chương trình trong `src/games` để đọc, chạy và sửa.

| Game | Là gì | Đọc để học |
|---|---|---|
| `njin_platformer` | *Mầm Leo Núi*: hai màn Tiled với dốc, bục một chiều, bục di chuyển, nhảy tường, quái, checkpoint, hộp thoại | @ref platformer |
| `njin_topdown` | *Rừng Cổ Thạch*: bản đồ Tiled có nước động, kiếm, quái đuổi theo A\*, cây che nhân vật, rương, hộp thoại | @ref topdown |
| `njin_debug_demo` | Bóng nảy để thử inspector: mỗi phím tốn CPU, RAM hay GPU một chút và inspector hiện ra ngay | @ref debug |
| `njin_render_demo` | Rừng 128 x 96 ô với 3000 cây đá: phím bật tắt atlas, hạt GPU/CPU, vsync, blur, bloom, CRT; HUD hiện số sprite bị cắt và số lệnh vẽ | @ref rendering, @ref particles |

`njin_render_demo` không có menu hay tiếng: nó chỉ để **thấy** các tính năng vẽ làm gì. Đi quanh
bản đồ bằng WASD hoặc phím mũi tên, đọc phím ở dòng cuối của HUD (hoặc đầu `main.cpp`). Thử phím
1 với 3000 cây đá bật: số lệnh vẽ rơi từ vài trăm xuống còn vài lệnh. Nó cũng mở cổng debug, nên chạy
`run_inspected.bat render_demo` để xem cùng các số đó trong inspector.

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

## Phiên bản

Số phiên bản của njin (semver) nằm ở một chỗ duy nhất: `src/engine/api/njin_version.h`. CMake, log
lúc khởi động, njin::version() và njin_inspector đều đọc từ đó; `njin_package()` mặc định dùng nó làm
phiên bản của game nếu bạn không đặt `VERSION`. Trước 1.0 API còn có thể đổi giữa hai bản MINOR. Lịch
sử nằm ở `CHANGELOG.md`. So `NJIN_VERSION` với `njin::version()` để phát hiện game và engine build từ
hai bản khác nhau.

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
