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

- njin_create() mở cửa sổ và trả về context của engine.
- njin_run() chạy vòng lặp đến khi cửa sổ đóng.
- njin_destroy() giải phóng mọi tài nguyên.

Chương trình này mở một cửa sổ trống. Để làm được điều gì đó, bạn viết
**module**: xem @ref modules_systems.

## Thêm module của game

Tạo module trong một file `.cpp`, đăng ký nó trong `main` trước njin_run():

@include hello_module.cpp

Với một game trong `src/games/<tên>/`, file `CMakeLists.txt` của nó cần:

```cmake
add_executable(my_game main.cpp modules/hello.cpp)

# njin::rt kéo theo njin::api (và thư mục include của nó).
target_link_libraries(my_game PRIVATE njin::rt njin_warnings)
```

rồi thêm `add_subdirectory(src/games/my_game)` vào `CMakeLists.txt` gốc.

## Bước tiếp theo

- @ref first_jump : một nhân vật nhảy được trên bản đồ, trong 50 dòng (platformer)
- @ref first_walk : một nhân vật đi 8 hướng trên bản đồ, trong 50 dòng (top-down)
- @ref modules_systems : viết logic cho game
- @ref cheatsheet : muốn làm X thì dùng hàm nào
- @ref game_loop : biết một frame chạy theo thứ tự nào
- @ref ecs : tạo entity và component
- `src/games/pong`: một game hoàn chỉnh để đọc và sửa thử
