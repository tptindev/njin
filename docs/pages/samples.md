# Game mẫu và đóng gói {#samples}

Tám chương trình trong `examples` để đọc, chạy và sửa. Mới bắt đầu? Làm @ref first_jump (platformer) hoặc
@ref first_walk (top-down) trước: mỗi bài dưới 50 dòng, rồi quay lại đây xem một game đầy đủ.

| Game | Là gì | Đọc để học |
|---|---|---|
| `njin_platformer` | *Mầm Leo Núi*: hai màn Tiled với dốc, bục một chiều, bục di chuyển, nhảy tường, quái, checkpoint, hộp thoại | @ref platformer |
| `njin_topdown` | *Rừng Cổ Thạch*: bản đồ Tiled có nước động, kiếm, quái đuổi theo A\*, cây che nhân vật, rương, hộp thoại | @ref topdown |
| `njin_debug_demo` | Bóng nảy để thử inspector: mỗi phím tốn CPU, RAM hay GPU một chút và inspector hiện ra ngay | @ref debug |
| `njin_render_demo` | Rừng 128 x 96 ô với 3000 cây đá: phím bật tắt atlas, hạt GPU/CPU, vsync, blur, bloom, CRT, và phím 7 đến 9 cho đêm có đèn, hoàng hôn bằng bảng màu, sương mù bằng nhiễu; phím L, N, O, M, P cho ánh sáng PBR với bóng đổ, normal map và kim loại; HUD hiện số sprite bị cắt và số lệnh vẽ | @ref rendering, @ref particles, @ref shader_advanced, @ref lighting |
| `njin_fps` | Bắn súng góc nhìn thứ nhất: đi lại và bắn mục tiêu trên lưới 5x5, model glTF (khẩu súng), bóng đổ, sương mù, đèn màu, đèn pin, vệt đạn và mục tiêu trúng phát sáng (bloom), hạt 3D, rung camera | @ref graphics_3d |
| `njin_sokoban` | Đẩy thùng 2.5D: camera nghiêng, sàn/tường instanced, thùng có texture, nhân vật hình SDF viên nang có viền sáng, đèn trên ô đích, hạt bụi và lấp lánh | @ref graphics_3d |
| `njin_platformer3d` | *Nhảy qua mây*: platformer 3D góc nhìn thứ ba trên vật lý Jolt, nhân vật nhảy đôi, bục di chuyển, thùng đẩy được, checkpoint, vòng đích | @ref physics3d |
| `njin_skylight` | *Giếng trời*: nắng lọt qua giếng trời 3 x 3 ô trên trần một sảnh tối thành chín luồng sáng trong sương, rọi lên bức tượng; bloom, ánh sáng dội, và với `NJIN_RAYTRACING` bóng và ánh sáng dội bằng tia. Xoay, dời, zoom camera bằng chuột, kéo tượng, khối đá, quả cầu vào nắng; phím 1 đến 4 bật tắt từng phần để so | @ref shadow3d_raytraced, @ref fog3d_volume, @ref probe3d |

@image html platformer.gif "njin_platformer: chạy và nhảy trên dốc, nhắc \"E\" khi lại gần con cú"

@image html topdown.gif "njin_topdown: đi, lướt, con slime hồng đuổi theo, nhắc phím tương tác ở gần biển báo"

@image html pong_play.png "njin_pong: một trận Pong đang chơi"

@image html render_demo.png "njin_render_demo: rừng 128 x 96 ô với hàng nghìn sprite; HUD ở trên cho số sprite, lệnh vẽ và phím bật tắt"

@image html fps.png "njin_fps: nhắm vào lưới mục tiêu, vệt đạn và điểm trúng phát sáng, sương mù xa"

@image html sokoban.png "njin_sokoban: một màn đang chơi, nhân vật SDF viền sáng, đèn trên ô đích"

@image html platformer3d.png "njin_platformer3d: bục đầu với ba thùng vật lý, dãy bục lên tới vòng đích ở xa"

@image html skylight.png "njin_skylight: nắng qua giếng trời 3 x 3 ô rọi lên bức tượng rắn, khối đá vừa được kéo vào nắng"

`njin_fps` là ví dụ 3D, chuyển thể từ [fps_tutorial của Biped-Potato](https://github.com/Biped-Potato/fps_tutorial)
(Bevy) sang njin; model khẩu súng (`assets/models/ak.glb`) là của tác giả đó, giấy phép MIT
(`assets/models/LICENSE.txt`). `njin_sokoban` sinh texture thùng từ script Python, không có file nhị phân
không rõ nguồn. `njin_platformer3d` vẽ mọi thứ bằng hình khối và hình SDF, không có file asset nào. Cả
ba không có menu, cài đặt hay nhạc: đọc @ref graphics_3d để xem API 3D chúng dùng.

Bức tượng của `njin_skylight` (`assets/snake_statue.glb`) là *Snake Statue* của Ville Seppänen, giấy phép CC BY 4.0
(`assets/LICENSE.txt`). Build thường thì nó dùng ảnh bóng và lưới ánh sáng chụp một lần; build engine với
`-DNJIN_RAYTRACING=ON` (@ref setup_engine_build) trên máy có ray tracing để thấy bóng mềm dần theo khoảng cách và ánh
sáng dội theo kịp khi kéo vật. Sảnh sâu 20 m nên sương dò 128 bước và ảnh bóng phủ cả sảnh (`shadow_range` 22): ít bước
hơn thì mép luồng sáng lởm chởm khi camera chạy, ảnh bóng hẹp hơn thì sương ngoài nó sáng như có nắng.

`njin_fps` còn chơi được nhiều người qua mạng (@ref netcode): một máy mở phòng và chơi luôn, các máy khác vào,
mọi người bắn chung một bộ bia, chủ phòng giữ điểm. Thử hai cửa sổ trên cùng một máy, với mạng xấu giả lập:

```sh
cd build/bin/fps
njin_fps --host --lag 100 --jitter 30 --loss 10
njin_fps --join 127.0.0.1 --lag 100 --jitter 30 --loss 10
```

Máy khác trong mạng LAN thì `--join` tới IP của máy chủ phòng (`--port` nếu không phải 7777). `--bot` cho người
chơi tự đi ngang và tự bắn, để thử mà không cần ngồi ở cả hai bàn phím. Mỗi cửa sổ hiện ping, tick, input đến sớm
mấy tick và cỡ lần sửa lệch gần nhất; log ghi các số đó mỗi 5 giây.

`njin_render_demo` không có menu hay tiếng: nó chỉ để **thấy** các tính năng vẽ làm gì. Đi quanh
bản đồ bằng WASD hoặc phím mũi tên, đọc phím ở dòng cuối của HUD (hoặc đầu `main.cpp`). Thử phím
1 với 3000 cây đá bật: số lệnh vẽ rơi từ vài trăm xuống còn vài lệnh. Nó cũng mở cổng debug, nên chạy
`run_inspected.bat render_demo` để xem cùng các số đó trong inspector.

Cả hai game đầu có menu chính, tạm dừng, cài đặt (âm lượng, toàn màn hình, ngôn ngữ, đổi phím cho bàn
phím và tay cầm), hai ngôn ngữ (Việt, Anh), nhạc và tiếng, và màn hình ảo 640 x 360. Phần dùng chung
nằm ở `examples/shared` (menu cài đặt); mọi hình, tiếng, bản đồ đều sinh ra từ script Python
`tools/make_assets.py` của từng game, nên không có file nhị phân không rõ nguồn.
`topdown` dùng font Be Vietnam Pro (giấy phép SIL OFL, kèm `OFL.txt`) cho chữ tiếng Việt.
`platformer` là pixel art đến tận UI: font VT323 (SIL OFL, kèm `OFL-VT323.txt`, font pixel có đủ
tiếng Việt) vẽ bằng njin::font_pixel ở cỡ 16 và 32, panel và nút vuông góc, màn hình ảo 640 x 360
phóng theo số nguyên với lọc nearest, và `crisp_text` tắt.

**Mọi game trong `examples` đều mở cổng cho njin_inspector khi build bản debug** (bảy game ở bảng trên
cùng `njin_pong` và `njin_sandbox`; `njin_debug_demo` và `njin_render_demo` luôn mở), nên
`run_inspected.bat <tên game>` chạy game kèm inspector cho bất kỳ game nào. Khi inspector đang nối, log của
game hiện ở inspector thay vì console.

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
sử nằm ở `CHANGELOG.md`; `0.1.0` là bản phát hành mở đầu từ source tree hiện tại. So `NJIN_VERSION` với
`njin::version()` để phát hiện game và engine build từ hai bản khác nhau.

## Đóng gói bản phát hành

Trong CMakeLists của game:

@code{.cmake}
njin_add_assets(my_game assets)     # chép assets/ cạnh exe mỗi lần build
njin_package(my_game
  NAME "Tên game"
  VERSION 1.0.0
  ICON icon.ico                     # icon của exe và thanh tác vụ
  ASSETS assets
  EXCLUDE blender *.blend *.md      # không đưa vào bản phát hành
  FILES README.txt LICENSE)
@endcode

@code{.bat}
cmake --preset release
cmake --build --preset release --target my_game_dist
@endcode

Kết quả là `build-release/dist/my_game-1.0.0.zip` chứa exe, thư mục `assets` và các file kèm theo. Trên
Windows, exe được gắn icon và thông tin phiên bản, và **không có cửa sổ console** trừ bản Debug (cần
console để đọc log). Game tìm `assets/...` cạnh exe dù chạy từ thư mục nào. Game chưa đóng gói mà
vẫn muốn bỏ cửa sổ console thì gọi riêng `njin_hide_console(my_game)`.

`EXCLUDE` (không bắt buộc) bỏ khỏi thư mục assets trong bản đóng gói những thứ chỉ cần lúc làm game: file
nguồn Blender, bản sao lưu, ảnh duyệt mẫu. Mỗi mẫu là một đường dẫn tính từ thư mục assets, dùng được `*`
(mọi ký tự trừ `/`), `**` (mọi ký tự) và `?`. Mẫu chỉ một thư mục thì bỏ cả thư mục đó; mẫu không có `/`
khớp tên ở mọi cấp (`*.md` bỏ mọi file `.md`). Không có `EXCLUDE` thì chép hết như trước. Chạy thử bản trong
`dist/<target>/` một lần trước khi gửi, để chắc là không bỏ mất file game cần.

Nhớ tắt cổng debug trong bản phát hành: `debug_server_start()` chỉ trong `#ifndef NDEBUG`.

@note Ảnh và ảnh động trong trang này chụp từ bản build Windows. Trên Linux (Ubuntu 26.04 trong WSL2) mọi game mẫu và
njin_inspector build được, và njin_pong mở được cửa sổ; macOS chưa thử: xem @ref setup. Cũng chưa thử với tay cầm thật.
