# Bài 8: CMake cơ bản {#learn_cmake_basics}

**Bài này dạy gì:** viết được `CMakeLists.txt` cho một chương trình và một thư viện, hiểu `PRIVATE`/`PUBLIC`/`INTERFACE`,
chọn chuẩn C++ và kiểu build, và biết vì sao "xóa thư mục `build/` rồi cấu hình lại" chữa được nhiều lỗi lạ.

**Cần biết trước:** biết biên dịch bằng tay một chương trình nhiều file bằng `g++` (biên dịch từng file thành file đối
tượng rồi liên kết, xem @ref learn_c_project). Chưa cần biết gì về CMake.

Mọi ví dụ dưới đây đã được chạy thật với CMake 4.0.2, Ninja 1.13 và GCC 15.2 (w64devkit) trên Windows. Đường dẫn
dài trong kết quả được rút gọn. Chưa thử với MSVC, Linux hay macOS: chỗ nào nhắc tới chúng sẽ được ghi rõ.

## CMake làm gì

Biên dịch bằng tay ổn với một file. Với hai mươi file thì bạn cần: chỉ biên dịch lại file đã đổi, biết file nào
phụ thuộc file nào, truyền đúng cờ (`-I`, `-std=`, `-O2`) cho từng file, và làm được điều đó trên máy Windows lẫn
Linux. Đó là việc của một **hệ thống build**.

CMake không tự biên dịch. Nó đọc `CMakeLists.txt` rồi **sinh ra** file cho một công cụ build thật (Ninja, Make, Visual
Studio). Vì vậy mọi dự án CMake đi qua hai bước:

| Bước | Lệnh | Việc làm |
|---|---|---|
| **Cấu hình** (configure) | `cmake -S . -B build -G Ninja` | Đọc `CMakeLists.txt`, tìm trình biên dịch, sinh `build/build.ninja` |
| **Build** | `cmake --build build` | Gọi Ninja để biên dịch và liên kết |

## Dự án nhỏ nhất

Hai file:

```cpp
// main.cpp
#include <iostream>

int main() {
  std::cout << "Xin chao tu CMake\n";
}
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(hello LANGUAGES CXX)

add_executable(hello main.cpp)
```

- `cmake_minimum_required` nói CMake nào trở lên mới hiểu file này. njin viết `3.28...4.0`: tối thiểu 3.28, và
  chấp nhận các quy tắc mới nhất đến 4.0.
- `project` đặt tên dự án và ngôn ngữ dùng (`CXX` là C++, `C` là C).
- `add_executable(tên file...)` tạo một **target** chương trình từ các file nguồn.

Chạy hai bước:

```
$ cmake -S . -B build -G Ninja
-- The CXX compiler identification is GNU 15.2.0
...
-- Configuring done (1.7s)
-- Generating done (0.0s)
-- Build files have been written to: .../b1/build

$ cmake --build build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.obj
[2/2] Linking CXX executable hello.exe

$ ./build/hello
Xin chao tu CMake
```

Ba cờ của bước cấu hình: `-S .` là thư mục chứa `CMakeLists.txt` (**source**), `-B build` là thư mục chứa mọi
thứ được sinh ra (**binary dir**), `-G Ninja` chọn công cụ build. Thư mục `build/` chứa `CMakeCache.txt`,
`build.ninja` và file thực thi; thư mục nguồn không bị đụng tới. Đó là **build ngoài nguồn** (out-of-source): muốn
dọn sạch, xóa `build/` là xong.

Build lại lần hai, CMake và Ninja chỉ làm phần cần làm:

```
$ cmake --build build          (không đổi gì)
ninja: no work to do.

$ touch main.cpp; cmake --build build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.obj
[2/2] Linking CXX executable hello.exe

$ (thêm một dòng vào CMakeLists.txt); cmake --build build
[0/1] Re-running CMake...
-- them dong nay
-- Configuring done (0.3s)
```

Sửa `CMakeLists.txt` thì bước cấu hình tự chạy lại. Bạn hiếm khi phải gõ lại `cmake -S . -B build`.

## Chọn chuẩn C++

Nếu bạn không nói gì, GCC 15.2 không bật C++20. Một chương trình dùng `std::span` (C++20) sẽ không biên dịch:

```
$ cmake --build build
main.cpp:4:14: error: 'span' is not a member of 'std'
```

Sửa bằng cách yêu cầu C++20 cho target:

```cmake
target_compile_features(std_demo PRIVATE cxx_std_20)
```

Cả dự án thì đặt biến ở đầu file (njin làm đúng như vậy):

```cmake
set(CMAKE_CXX_STANDARD 20)             # mọi target khai báo sau dòng này
set(CMAKE_CXX_STANDARD_REQUIRED ON)    # không có C++20 thì báo lỗi, không lặng lẽ hạ xuống
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20 chứ không phải -std=gnu++20
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)    # bỏ bước quét C++20 module
```

Đã kiểm tra: với `CMAKE_CXX_EXTENSIONS OFF` cờ thực tế là `-std=c++20`; với `target_compile_features` thì là
`-std=gnu++20` (bản mở rộng của GCC). Dòng cuối tắt một bước mà từ C++20 CMake tự thêm: khi chưa tắt, mỗi file
nguồn có thêm bước `Scanning ... for CXX dependencies` và `Generating CXX dyndep file`, và cờ biên dịch có thêm
`-fmodules-ts -fmodule-mapper=...`. Game không dùng C++20 module thì các bước đó chỉ thừa; njin tắt nó trong
`CMakeLists.txt` gốc.

## Mọi thứ là một target

CMake hiện đại nghĩ theo **target**: một chương trình hoặc một thư viện, cùng với những thứ nó cần và những thứ nó
cung cấp cho người dùng. Ba lệnh tạo target:

| Lệnh | Tạo ra |
|---|---|
| `add_executable(game main.cpp)` | một chương trình |
| `add_library(rt STATIC a.cpp b.cpp)` | một thư viện tĩnh (`.a` hay `.lib`) |
| `add_library(api INTERFACE)` | một "thư viện" không có file nguồn: chỉ mang theo thông tin (thư mục include, cờ) |

Rồi gắn thông tin vào target bằng các lệnh `target_*`: `target_include_directories`, `target_link_libraries`,
`target_compile_definitions`, `target_compile_features`, `target_compile_options`.

### PRIVATE, PUBLIC, INTERFACE

Mỗi lệnh `target_*` phải nói thông tin đó dành cho ai:

| Từ khóa | Dùng cho chính target này | Truyền cho ai liên kết với nó |
|---|---|---|
| `PRIVATE` | có | không |
| `INTERFACE` | không | có |
| `PUBLIC` | có | có |

Cách dễ nhớ: hỏi "**header của tôi có nhắc tới thứ đó không?**". Có: `PUBLIC`. Chỉ dùng trong file `.cpp` của tôi:
`PRIVATE`.

Thử với ba target: `api` (header mà game được dùng), `hidden` (một thứ game không được thấy), `rt` (thư viện ở
giữa) và `game`:

```cmake
add_library(api INTERFACE)                       # chỉ có header
target_include_directories(api INTERFACE api)

add_library(hidden INTERFACE)                    # thứ game không được thấy
target_include_directories(hidden INTERFACE hidden)

add_library(rt STATIC rt/rt.cpp)
target_link_libraries(rt PUBLIC api PRIVATE hidden)

add_executable(game game.cpp)
target_link_libraries(game PRIVATE rt)
```

`rt.cpp` include cả `api.h` và `hidden.h` nên biên dịch được. Nhưng nếu `game.cpp` cũng `#include <hidden.h>`:

```
$ cmake --build build
game.cpp:2:10: fatal error: hidden.h: No such file or directory
    2 | #include <hidden.h>   // the game reaches into the private dependency
```

Đổi thành `target_link_libraries(rt PUBLIC api hidden)` thì biên dịch được và game in `run_game() = 42, hidden = 42`.
Đây là cách bạn ép người dùng thư viện chỉ chạm vào phần công khai.

Với **thư viện tĩnh**, `PRIVATE` chỉ giấu header, không giấu việc liên kết. Đã kiểm tra: `rt` nối `hidden` (thư viện
tĩnh) bằng `PRIVATE`, lệnh liên kết của `game` vẫn có `librt.a libhidden.a`, trong khi lệnh biên dịch `game.cpp` không
có cờ `-I` nào cho `hidden`. Game liên kết được, nhưng không thấy header.

**Lỗi hay gặp:** header công khai của bạn `#include` header của thư viện khác, mà bạn nối thư viện đó bằng `PRIVATE`.
`render.h` include `core.h`, `render` nối `core` bằng `PRIVATE`, game include `render.h`:

```
render/include/render.h:2:10: fatal error: core.h: No such file or directory
```

Lỗi nằm trong `render.h` chứ không phải `game.cpp`. Đổi thành `PUBLIC` là xong.

### Cách njin bố trí

njin dùng đúng hình dạng đó (mở `src/engine/api/CMakeLists.txt` và `src/engine/runtime/CMakeLists.txt`):

```cmake
# api: chỉ có header, không có file nguồn
add_library(njin_api INTERFACE)
add_library(njin::api ALIAS njin_api)
target_include_directories(njin_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(njin_api INTERFACE cxx_std_20)
target_link_libraries(njin_api INTERFACE EnTT::EnTT)

# runtime: thư viện tĩnh, raylib bị giấu
add_library(njin_rt STATIC ${NJIN_RT_SOURCES} ...)
add_library(njin::rt ALIAS njin_rt)
target_link_libraries(
  njin_rt
  PUBLIC njin::api
  PRIVATE raylib njin_warnings)
```

Một game chỉ viết `target_link_libraries(my_game PRIVATE njin::rt)`. Nó nhận `njin.h` (qua `njin::api`), nhận C++20
và EnTT, nhưng **không** thấy `raylib.h`. Đó là lý do trang chủ wiki nói "raylib được giấu hoàn toàn".

**`njin::api` là `ALIAS`.** Tên có `::` luôn phải là một target có thật. Gõ sai tên có `::` thì CMake báo lỗi ngay ở
bước cấu hình:

```
CMake Error at CMakeLists.txt:8 (target_link_libraries):
  Target "game" links to:

    my::libb

  but the target was not found.  Possible reasons include:

    * There is a typo in the target name.
```

Gõ sai một tên thường (`mylibb`) thì CMake coi nó là một thư viện hệ thống, và bạn chỉ biết ở bước liên kết:
`ld.exe: cannot find -lmylibb`. Vì thế các thư viện đáng kể đều được đặt tên kiểu `njin::rt`.

## Kiểu build và trình sinh

**Generator** (`-G`) là công cụ build mà CMake sinh file cho. `Ninja` nhanh và có ở mọi hệ điều hành. Còn có `Unix
Makefiles`, `MinGW Makefiles`, `Visual Studio 17 2022`... (`cmake --help` liệt kê những gì máy bạn có).

**Kiểu build** quyết định cờ tối ưu và gỡ lỗi. Với Ninja mỗi thư mục build chỉ có **một** kiểu, chọn lúc cấu hình bằng
`-DCMAKE_BUILD_TYPE=`. Đã đo bằng cách in cờ biên dịch thật của cùng một chương trình:

| `CMAKE_BUILD_TYPE` | Cờ thực tế | `assert()` |
|---|---|---|
| (bỏ trống) | không có cờ nào | bật |
| `Debug` | `-g` | bật |
| `Release` | `-O3 -DNDEBUG` | tắt (bị biên dịch bỏ đi) |

Quên đặt kiểu build là lỗi phổ biến: chương trình build ra **không tối ưu**. Ví dụ trong njin: `NDEBUG` chỉ có ở
Release, và các game mẫu chỉ mở cổng debug `#ifndef NDEBUG`.

Trình sinh **nhiều cấu hình** (Visual Studio, `Ninja Multi-Config`) thì khác: một thư mục build chứa nhiều kiểu, chọn
lúc build bằng `--config`:

```
$ cmake -S . -B multi -G "Ninja Multi-Config"
$ cmake --build multi --config Debug
[2/2] Linking CXX executable Debug\types_demo.exe
$ cmake --build multi --config Release
[2/2] Linking CXX executable Release\types_demo.exe
```

Hai file chạy nằm ở `multi/Debug/` và `multi/Release/`. Trình sinh Visual Studio cũng nhiều cấu hình nên dùng
`--config Release` như trên (chưa thử ở đây).

## Biến, `message` và `if`

```cmake
set(GREETING "xin chao")                 # biến thường
message(STATUS "GREETING = ${GREETING}")
message(STATUS "project  = ${PROJECT_NAME} ${PROJECT_VERSION}")

set(SOURCES main.cpp)                    # danh sách là một chuỗi ngăn bằng ';'
list(APPEND SOURCES extra.cpp)
message(STATUS "SOURCES  = ${SOURCES}")

foreach(name IN ITEMS a b c)
  message(STATUS "  vong lap: ${name}")
endforeach()
```

```
-- GREETING = xin chao
-- project  = vars_demo 1.2.3
-- SOURCES  = main.cpp;extra.cpp
--   vong lap: a
--   vong lap: b
--   vong lap: c
```

`message` có nhiều mức. `STATUS` chỉ in ra; `WARNING` in kèm vị trí và tiếp tục; `FATAL_ERROR` dừng cấu hình:

```
CMake Warning at CMakeLists.txt:29 (message):
  day la mot canh bao (van tiep tuc)

CMake Error at CMakeLists.txt:30 (message):
  day la loi: dung o day

-- Configuring incomplete, errors occurred!
```

Rẽ nhánh theo hệ điều hành và trình biên dịch:

```cmake
message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")
if(WIN32)  ...  endif()
if(MSVC)   ...  endif()
```

Kết quả trên máy đã thử (Windows, GCC):

```
-- CMAKE_SYSTEM_NAME = Windows
-- CMAKE_CXX_COMPILER_ID = GNU
-- WIN32 is true
-- MSVC is false
```

Chú ý: `WIN32` là "đang build cho Windows", **kể cả** khi dùng GCC. `MSVC` chỉ đúng khi trình biên dịch là MSVC.
Hai thứ khác nhau, và njin dùng cả hai (mở `CMakeLists.txt` gốc và `src/engine/runtime/CMakeLists.txt`):

```cmake
if(MSVC)
  target_compile_options(njin_warnings INTERFACE /W4)                    # cờ của MSVC
else()
  target_compile_options(njin_warnings INTERFACE -Wall -Wextra -Wpedantic) # cờ của GCC/Clang
endif()

if(WIN32)
  target_link_libraries(njin_rt PRIVATE ws2_32)     # socket của Windows, cần cho cổng debug
endif()
```

## Tùy chọn và bộ nhớ đệm (cache)

`option(TÊN "mô tả" mặc định)` tạo một công tắc mà người build bật tắt bằng `-D`:

```cmake
option(SHOW_DEBUG "Print extra lines" OFF)
message(STATUS "SHOW_DEBUG = ${SHOW_DEBUG}")
```

njin có một cái thật: `option(NJIN_BUILD_INSPECTOR "Build the njin_inspector debug tool" ON)`. Không muốn tải Dear
ImGui và build inspector thì cấu hình với `-DNJIN_BUILD_INSPECTOR=OFF`.

Giá trị của `option` và mọi biến `-D` được lưu trong **cache**, file `build/CMakeCache.txt`. Cache sống lâu hơn
`CMakeLists.txt`. Thử:

```
$ cmake -S . -B build -G Ninja -DSHOW_DEBUG=ON
-- SHOW_DEBUG = ON

$ cmake -S . -B build        (không -D nữa; giờ file mặc định vẫn ghi OFF)
-- SHOW_DEBUG = ON

$ grep SHOW_DEBUG build/CMakeCache.txt
SHOW_DEBUG:BOOL=ON

$ cmake -S . -B fresh -G Ninja   (thư mục mới)
-- SHOW_DEBUG = OFF
```

Giá trị cũ **vẫn còn** trong thư mục `build/` đã có, dù bạn không nhắc tới nó nữa. Trình biên dịch cũng bị nhớ:

```
$ CXX=clang++ cmake -S . -B build        (thư mục build cũ)
-- compiler   = C:/Dev/raylib/w64devkit/bin/c++.exe        <- vẫn là GCC

$ CXX=clang++ cmake -S . -B fresh2 -G Ninja   (thư mục mới)
-- The CXX compiler identification is Clang 21.1.8 with GNU-like command-line
```

Đây là lý do của lời khuyên quen thuộc: **cấu hình có vẻ sai, đổi trình biên dịch không ăn, một tùy chọn cứ kẹt ở giá
trị cũ, thì xóa thư mục `build/` rồi cấu hình lại.** Cache là nguyên nhân, và `build/` sinh ra được từ đầu nên xóa
không mất gì. (Đổi biến môi trường `CXX` chỉ có tác dụng lúc thư mục build mới được tạo.)

## Tự kiểm tra

1. Hai bước `cmake -S . -B build` và `cmake --build build` khác nhau thế nào?
2. `target_link_libraries(rt PRIVATE hidden)`: `game` (nối `rt`) có `#include` được header của `hidden` không? Và có
   liên kết được với `hidden` nếu nó là thư viện tĩnh không?
3. Vì sao đặt `PRIVATE` cho một thư viện mà header công khai của bạn include là sai?
4. `-DCMAKE_BUILD_TYPE=Release` làm thay đổi gì cờ biên dịch?
5. Vừa đổi giá trị mặc định của một `option` trong `CMakeLists.txt` mà kết quả build không đổi. Nguyên nhân và cách
   chữa?

## Bài tập

1. **Công tắc.** Thêm `option(VERBOSE_GREETING ...)` (mặc định `OFF`) vào dự án `hello`. Khi bật, chương trình in câu
   chào dài; khi tắt, in câu ngắn. Làm bằng `target_compile_definitions`, không sửa `main.cpp` giữa hai lần.
2. **Ai thấy header nào.** Có ba thứ: `core` (INTERFACE, có `core.h`), `render` (thư viện tĩnh, `render.h` include
   `core.h`) và `game` (nối `render`). Viết `CMakeLists.txt` sao cho `game.cpp` chỉ `#include <render.h>` mà biên dịch
   được. Chỉ ra lỗi nếu bạn nối `core` vào `render` bằng `PRIVATE`.
3. **Cả Debug lẫn Release.** Với chương trình in ra `NDEBUG` có bị định nghĩa hay không, build **cả hai kiểu trong cùng
   một thư mục build** và chạy từng bản.

## Đáp án

**Tự kiểm tra**

1. Cấu hình đọc `CMakeLists.txt` và sinh file cho Ninja. Build gọi Ninja để biên dịch. Cấu hình chạy lại (tự động) khi
   `CMakeLists.txt` đổi; build chạy lại mỗi khi file nguồn đổi.
2. Không `#include` được header của `hidden` (thông tin include không truyền qua `PRIVATE`). Nhưng nếu `hidden` là thư
   viện tĩnh thì vẫn liên kết được: lệnh liên kết của `game` có cả `librt.a` và `libhidden.a`.
3. Người dùng thư viện của bạn include `render.h`, `render.h` include `core.h`, mà `game` không có thư mục include của
   `core`: `fatal error: core.h: No such file or directory` nằm ngay trong `render.h`.
4. Từ không cờ nào (khi bỏ trống) thành `-O3 -DNDEBUG`. Debug là `-g`.
5. Cache giữ giá trị cũ của biến `SHOW_DEBUG`, và `option` chỉ đặt mặc định khi biến chưa có trong cache. Xóa `build/`
   rồi cấu hình lại, hoặc truyền `-DSHOW_DEBUG=OFF`.

**Bài tập 1**

```cmake
cmake_minimum_required(VERSION 3.28)
project(ex1 LANGUAGES CXX)

option(VERBOSE_GREETING "Print the long greeting" OFF)

add_executable(ex1 main.cpp)
if(VERBOSE_GREETING)
  target_compile_definitions(ex1 PRIVATE VERBOSE_GREETING=1)
endif()
```

`main.cpp` dùng `#ifdef VERBOSE_GREETING ... #else ... #endif`. Kết quả đã chạy:

```
VERBOSE_GREETING=OFF -> Xin chao.
VERBOSE_GREETING=ON -> Xin chao! Rat vui duoc gap ban.
```

(Mỗi lần dùng một thư mục build mới, vì cache giữ giá trị cũ.)

**Bài tập 2**

```cmake
cmake_minimum_required(VERSION 3.28)
project(ex2 LANGUAGES CXX)

add_library(core INTERFACE)
target_include_directories(core INTERFACE core/include)

add_library(render STATIC render/render.cpp)
target_include_directories(render PUBLIC render/include)
target_link_libraries(render PUBLIC core)      # PUBLIC vì render.h include core.h

add_executable(game game.cpp)
target_link_libraries(game PRIVATE render)
```

Kết quả: `render_value() = 14`. Với `target_link_libraries(render PRIVATE core)` lỗi là:

```
In file included from game.cpp:1:
render/include/render.h:2:10: fatal error: core.h: No such file or directory
```

**Bài tập 3**

```
$ cmake -S . -B multi -G "Ninja Multi-Config"
$ cmake --build multi --config Debug
$ cmake --build multi --config Release
$ multi/Debug/types_demo.exe     -> NDEBUG is not set: assert() is active
$ multi/Release/types_demo.exe   -> NDEBUG is set: assert() is compiled out
```

## Bước tiếp theo

@ref learn_cmake_projects : tải thư viện bằng `FetchContent`, preset, nhiều target, sao chép assets, và mở một cửa sổ raylib.
