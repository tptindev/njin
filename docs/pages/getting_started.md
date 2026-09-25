# Bắt đầu {#getting_started}

Trang này hướng dẫn build njin, chạy game mẫu và viết chương trình đầu tiên.

## Yêu cầu

- **CMake** 3.28 trở lên
- Trình biên dịch **C++20** (bản build hiện tại dùng GCC trong w64devkit)
- **Ninja**
- **Git**: CMake tải raylib 6.0 và EnTT v4.0.0 về khi cấu hình lần đầu

## Build và chạy

Trên Windows, hai script ở thư mục gốc lo hết:

| Script | Việc làm |
|---|---|
| `build.bat` | Cấu hình bằng Ninja (Debug), cập nhật `compile_commands.json` cho clangd, rồi build |
| `run.bat` | Build target `njin_sandbox` và chạy `build\bin\njin_sandbox.exe` |

Hoặc chạy CMake trực tiếp:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target njin_sandbox
build\bin\njin_sandbox.exe
```

@note `run.bat` chạy game với thư mục làm việc là thư mục gốc của repo. Đường
dẫn tương đối như `assets/player.png` được tính từ đó.

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

- @ref modules_systems : viết logic cho game
- @ref game_loop : biết một frame chạy theo thứ tự nào
- @ref ecs : tạo entity và component
