# Bắt đầu {#getting_started}

Trang này hướng dẫn build njin, chạy game mẫu và viết chương trình đầu tiên. Chưa cài trình biên dịch,
CMake hay Ninja? Làm theo @ref setup trước, có hướng dẫn cho từng hệ điều hành. Chưa quen C++, CMake
hay shader? Nhóm bài @ref learn dạy từ đầu, và không cần njin.

## Yêu cầu

- **CMake** 3.28 trở lên
- Trình biên dịch **C++20**. Môi trường phát triển chính là GCC trong w64devkit trên Windows.
  MSVC và GCC trên Linux có trong CI (`.github/workflows/build.yml`)
- **Ninja** (không cần nếu dùng MSVC với generator của Visual Studio)
- **Git**: CMake tải raylib 6.0, EnTT v4.0.0 và (cho njin_inspector) Dear ImGui về khi cấu hình lần đầu
- **Linux**: thêm các thư viện phát triển X11 và OpenGL, ví dụ trên Ubuntu:
  `sudo apt install ninja-build libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev`

## Lấy mã nguồn

```
git clone https://github.com/tptindev/njin.git
cd njin
```

## Build và chạy

Trên Windows, hai script ở thư mục gốc lo hết:

| Script | Việc làm |
|---|---|
| `build.bat` | Cấu hình bằng Ninja (Debug), cập nhật `compile_commands.json` cho clangd, rồi build |
| `run.bat` | Build target `njin_sandbox` và chạy `build\bin\njin_sandbox.exe` |
| `build\bin\njin_pong.exe` | Game Pong mẫu, build cùng `build.bat` |

Trên mọi hệ điều hành, dùng **preset** trong `CMakePresets.json`, không cần nhớ tham số:

| Preset | Thư mục build | Việc làm |
|---|---|---|
| `debug` | `build/` | Ninja, Debug. Game có kết nối debug tới njin_inspector |
| `release` | `build-release/` | Ninja, Release. Tối ưu, không có kết nối debug |

```
cmake --preset debug
cmake --build --preset debug --target njin_sandbox
build\bin\njin_sandbox.exe
```

Bỏ `--target` để build mọi thứ (game mẫu, njin_inspector). Dùng MSVC thì không cần preset:
`cmake -S . -B build -A x64` rồi `cmake --build build --config Release`.

@note `run.bat` chạy game với thư mục làm việc là thư mục gốc của repo. Đường
dẫn tương đối như `assets/player.png` được tính từ đó trước, rồi từ thư mục chứa exe.
Dùng `njin_add_assets()` trong CMake để thư mục assets của game được copy cạnh exe mỗi lần
build, xem @ref window_files.

## Chương trình nhỏ nhất

@include minimal_main.cpp

Ba hàm cần biết:

- create() mở cửa sổ và trả về context của engine.
- run() chạy vòng lặp đến khi cửa sổ đóng.
- destroy() giải phóng mọi tài nguyên.

Chương trình này mở một cửa sổ trống. Để làm được điều gì đó, bạn viết
**module**: xem @ref modules_systems.

## Thêm module của game

Tạo module trong một file `.cpp`, đăng ký nó trong `main` trước run():

@include hello_module.cpp

Với một game trong `src/games/<tên>/`, file `CMakeLists.txt` của nó cần:

```cmake
add_executable(my_game main.cpp modules/hello.cpp)

# Lõi engine, kèm njin::api (và thư mục include của nó).
njin_use(my_game)
target_link_libraries(my_game PRIVATE njin_warnings)
```

rồi thêm `add_subdirectory(src/games/my_game)` vào `CMakeLists.txt` gốc.

## Chọn phần engine game cần

Engine gồm một **lõi** và vài **feature** tùy chọn, mỗi feature là một thư viện riêng.
`njin_use()` chỉ link lõi và những feature được nêu tên, nên game 2D không phải mang theo
3D, vật lý 3D hay Lua:

| Feature | Gồm | Thư viện ngoài |
|---|---|---|
| (lõi) | cửa sổ, input, ECS, vẽ 2D, body 2D, tilemap và level, âm thanh, UI, particle, ánh sáng 2D, cổng debug | raylib, EnTT |
| `3d` | model, material, lượt vẽ 3D, particle 3D, post3d, world3d (trời, địa hình, nước, cỏ), animation 3D, âm thanh 3D | meshoptimizer |
| `physics3d` | body, nhân vật, ragdoll, xe (cần `3d`) | Jolt Physics |
| `nav3d` | navmesh và agent (cần `3d` và `physics3d`) | Recast/Detour |
| `video` | phát video MPEG-1 | pl_mpeg |
| `net` | socket, multiplayer, netcode, lockstep | asio, ENet |
| `script` | script Lua | Lua, sol2 |

```cmake
njin_use(my_game)                           # game 2D: chỉ lõi
njin_use(my_fps FEATURES physics3d script)  # thêm 3d (physics3d cần nó) và Lua cho cả hai
```

Feature mà feature khác cần sẽ tự được thêm vào. `script` đi cùng một feature 3D thì Lua
cũng có các hàm của feature đó. Game include header của một feature chưa bật (ví dụ
`njin_physics3d.h`) thì dừng ngay ở bước biên dịch, kèm lời nhắc feature cần thêm.
`njin::rt` vẫn là toàn bộ engine: `target_link_libraries(my_game PRIVATE njin::rt)` chạy như
trước. Một game chỉ dùng một trong hai cách.

## Build game nhanh hơn

Gọi thêm `njin_precompile_headers()` sau `njin_use()`:

```cmake
njin_use(my_game)
njin_precompile_headers(my_game)
```

Header chuẩn của C++, EnTT và các component của lõi (`transform`, `sprite`, `collider`...) được biên dịch sẵn
một lần, rồi mọi file của game dùng lại thay vì tự đọc lại từ đầu. Chỉ có những header ít khi đổi được đưa vào,
nên sửa một header của game vẫn chỉ build lại các file cần build lại. Mọi game trong cùng một build dùng chung
một header biên dịch sẵn. Tắt cả engine lẫn game bằng `-DNJIN_PCH=OFF`, ví dụ để kiểm tra mỗi file tự include đủ
những gì nó dùng. Xem thêm @ref setup_engine_build.

## Bước tiếp theo

- @ref first_jump : một nhân vật nhảy được trên bản đồ, trong 50 dòng (platformer)
- @ref first_walk : một nhân vật đi 8 hướng trên bản đồ, trong 50 dòng (top-down)
- @ref modules_systems : viết logic cho game
- @ref cheatsheet : muốn làm X thì dùng hàm nào
- @ref game_loop : biết một frame chạy theo thứ tự nào
- @ref ecs : tạo entity và component
- `src/games/pong`: một game hoàn chỉnh để đọc và sửa thử
