# Bài 9: CMake cho dự án thật {#learn_cmake_projects}

**Bài này dạy gì:** phần CMake bạn dùng hàng ngày trong một dự án có nhiều thư mục, thư viện tải về và tài nguyên:
`add_subdirectory`, `FetchContent`, preset, sao chép assets, cờ cảnh báo, `compile_commands.json`. Cuối bài là một cửa sổ
raylib dựng bằng CMake và cách đọc `CMakeLists.txt` của njin từ trên xuống.

**Cần biết trước:** @ref learn_cmake_basics (target, `PRIVATE`/`PUBLIC`, cấu hình và build, cache).

Mọi ví dụ đã chạy thật với CMake 4.0.2, Ninja 1.13 và GCC 15.2 (w64devkit) trên Windows; đường dẫn dài trong kết quả
được rút gọn. MSVC, Linux và macOS chưa thử ở đây.

## Dự án có nhiều thư mục

Một dự án nhỏ, thư viện và game tách riêng:

```
mini/
  CMakeLists.txt          gốc: cài đặt chung, gọi các thư mục con
  cmake/assets.cmake      hàm add_assets
  lib/
    CMakeLists.txt        thư viện `mini`
    include/mini.h
    src/add.cpp
  game/
    CMakeLists.txt        chương trình `game`
    main.cpp
    assets/hello.txt
```

```cmake
# mini/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(mini LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)      # ghi compile_commands.json cho clangd

# Cờ cảnh báo nằm trong một INTERFACE target: target nào muốn thì nối vào.
add_library(mini_warnings INTERFACE)
if(MSVC)
  target_compile_options(mini_warnings INTERFACE /W4)
else()
  target_compile_options(mini_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

include(cmake/assets.cmake)

add_subdirectory(lib)
add_subdirectory(game)
```

```cmake
# mini/lib/CMakeLists.txt
file(GLOB_RECURSE MINI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(mini STATIC ${MINI_SOURCES})
target_include_directories(mini PUBLIC include)   # PUBLIC: game cũng cần header này
target_link_libraries(mini PRIVATE mini_warnings)
```

```cmake
# mini/game/CMakeLists.txt
add_executable(game main.cpp)
target_link_libraries(game PRIVATE mini mini_warnings)

# Mỗi game chạy từ thư mục riêng, nên hai game không dùng chung assets.
set_target_properties(game PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/game")

add_assets(game assets)
```

`add_subdirectory(lib)` đọc `lib/CMakeLists.txt` như một dự án con. Hai điều cần nhớ:

- **Biến** đặt ở thư mục cha thấy được ở thư mục con: `CMAKE_CXX_STANDARD 20` ở gốc làm file trong `lib/` được biên dịch
  với `-std=c++20` (đã kiểm tra trong `compile_commands.json`). Biến đặt ở thư mục con thì **không** truyền ngược lên,
  trừ khi viết `set(TÊN "giá trị" PARENT_SCOPE)` (đã thử: thư mục cha thấy `TÊN` nhưng không thấy biến `set` thường).
- **Target** thì toàn cục: `mini_warnings` tạo ở gốc nhưng `lib/` và `game/` đều nối được. Đường dẫn tương đối trong
  `target_include_directories(mini PUBLIC include)` tính từ thư mục của `CMakeLists.txt` đang chạy.

Build:

```
$ cmake -S . -B build -G Ninja && cmake --build build
[4/10] Building CXX object lib/CMakeFiles/mini.dir/src/add.cpp.obj
[7/10] Linking CXX static library lib\libmini.a
[8/10] Building CXX object game/CMakeFiles/game.dir/main.cpp.obj
[9/10] Linking CXX executable bin\game\game.exe

$ cd build/bin/game && ./game
add(2, 3) = 5
assets/hello.txt: xin chao v1
```

(Các dòng `Scanning ... for CXX dependencies` bị lược bớt: chúng do bước quét C++20 module, xem
@ref learn_cmake_basics.)

njin bố trí y hệt: `CMakeLists.txt` gốc gọi `add_subdirectory` cho `src/engine/api`, `src/engine/runtime` và từng game
trong `src/games/`; mỗi game có `RUNTIME_OUTPUT_DIRECTORY` riêng, ví dụ `build/bin/platformer/`.

## `include` khác `add_subdirectory` thế nào

`include(cmake/assets.cmake)` chạy file đó **ngay trong thư mục hiện tại**, như thể bạn dán nội dung vào chỗ đó. Vì thế
hàm `add_assets` định nghĩa trong đó dùng được ở mọi thư mục con gọi sau. `add_subdirectory` thì mở một phạm vi mới
cho `CMakeLists.txt` của thư mục con. njin làm đúng như vậy: `include(cmake/njin.cmake)` để mọi game gọi được
`njin_add_assets` và `njin_package`, rồi mới `add_subdirectory(src/games/...)`.

## Liệt kê file nguồn bằng `file(GLOB)`

Cách chắc chắn: ghi tên từng file trong `add_executable(game main.cpp play.cpp menus.cpp)`. njin làm vậy cho từng game.
Cách tiện: `file(GLOB_RECURSE ... "*.cpp")` gom mọi file `.cpp` trong thư mục; njin dùng nó cho thư viện runtime vì có
hàng chục file.

Nhưng danh sách được **chụp lại lúc cấu hình**. Thử: cấu hình xong rồi mới thêm `lib/src/mul.cpp`, trong khi
`game` đã gọi `mul()`, rồi chỉ chạy `cmake --build`.

Có `CONFIGURE_DEPENDS` (như trong ví dụ trên):

```
[0/2] Re-checking globbed directories...
[1/2] Re-running CMake...
[6/14] Building CXX object lib/CMakeFiles/mini.dir/src/mul.cpp.obj
add(2, 3) = 5
mul(2, 3) = 6
```

Không có `CONFIGURE_DEPENDS`:

```
FAILED: bin/game/game.exe
ld.exe: game/CMakeFiles/game.dir/main.cpp.obj:main.cpp:(.text+0x41): undefined reference to `mul(int, int)'
```

Phải cấu hình lại bằng tay (`cmake -S . -B build`) mới thấy file mới. Cái giá của `CONFIGURE_DEPENDS`: mỗi lần build
CMake kiểm tra lại thư mục (dòng `Re-checking globbed directories...`), chậm hơn một chút trên dự án lớn. Tài liệu CMake vẫn
khuyên liệt kê file bằng tay vì một thay đổi trong danh sách file khi đó hiện rõ trong diff của git. Chọn cái nào là
đánh đổi giữa tiện và rõ ràng.

## Cờ cảnh báo trong một INTERFACE target

Đưa cờ vào `mini_warnings` rồi chỉ nối nó vào target của **bạn**. Thêm `int unused = 5;` vào `game/main.cpp`:

```
game/main.cpp:5:7: warning: unused variable 'unused' [-Wunused-variable]
```

Bỏ `mini_warnings` khỏi `target_link_libraries(game ...)`: số cảnh báo về 0. Quan trọng nhất là **không** nối nó vào thư
viện tải về (như raylib): bạn không sửa được mã của người khác nên không cần cả trăm cảnh báo của họ. njin làm vậy:
`njin_warnings` chỉ nối vào `njin_rt` và các game, và comment trong `CMakeLists.txt` gốc ghi "Never linked into fetched
dependencies".

**`SYSTEM`** giải quyết một chuyện gần đó: header của thư viện ngoài mà bạn include gây cảnh báo. Đã thử với một header
có `static int helper() { return 1; }` không dùng tới:

```
target_include_directories(app PRIVATE vendor):         1 cảnh báo
target_include_directories(app SYSTEM PRIVATE vendor):  0 cảnh báo
```

Thư mục đánh dấu `SYSTEM` được trình biên dịch coi là "của hệ thống" nên im lặng. `FetchContent_Declare(... SYSTEM)`
(bên dưới) làm điều đó cho mọi header của thư viện tải về.

## `compile_commands.json`

Đặt `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` thì cấu hình ghi thêm `build/compile_commands.json`: danh sách lệnh biên
dịch của **từng file nguồn**. Trình soạn thảo dùng nó (clangd) để hiểu `#include`, cờ và chuẩn C++ của bạn:

```json
{
  "directory": ".../build",
  "command": "c++.exe -I.../lib/include -std=c++20 -Wall -Wextra -Wpedantic ... -c .../lib/src/add.cpp",
  "file": ".../lib/src/add.cpp"
}
```

clangd tìm file này ở thư mục gốc của dự án, nên nhiều người chép nó lên: `build.bat` của njin có
`copy /Y build\compile_commands.json compile_commands.json`, và `.gitignore` bỏ qua bản chép đó. Preset của njin cũng
đặt `CMAKE_EXPORT_COMPILE_COMMANDS` là `ON`. Chú ý: khi bước quét C++20 module chưa tắt, mỗi lệnh có thêm cờ
`-fmodules-ts -fmodule-mapper=...`, thêm nhiễu vào file này.

## Sao chép assets cạnh file chạy

Game tìm `assets/hello.txt` theo đường dẫn tương đối, nên thư mục `assets` phải nằm cạnh file chạy. Đây là hàm `add_assets`
rút gọn từ `njin_add_assets` trong `cmake/njin.cmake`:

```cmake
# cmake/assets.cmake
function(add_assets target dir)
  cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
             NORMALIZE OUTPUT_VARIABLE source)
  cmake_path(GET source FILENAME name)
  add_custom_target(
    ${target}_${name}
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${source}"
            "$<TARGET_FILE_DIR:${target}>/${name}"
    COMMENT "Copying ${name}/ next to ${target}"
    VERBATIM)
  add_dependencies(${target} ${target}_${name})
endfunction()
```

- `function(...)` định nghĩa lệnh của riêng bạn; tham số là `target` và `dir`.
- `cmake_path(ABSOLUTE_PATH ...)` đổi `assets` thành đường dẫn tuyệt đối (tính từ thư mục của `CMakeLists.txt` đang
  gọi hàm), và `cmake_path(GET ... FILENAME)` lấy tên thư mục cuối, `assets`.
- `add_custom_target` tạo một bước build tự viết; `add_dependencies(game game_assets)` bảo nó chạy trước khi `game`
  coi như xong.
- `$<TARGET_FILE_DIR:game>` là một **generator expression**: CMake điền giá trị lúc **build**, không phải lúc cấu hình.
  Nhờ vậy nó đúng cả khi thư mục chạy thay đổi theo kiểu build.

Kết quả đã chạy:

```
$ ls build/bin/game
assets  game.exe

$ echo "xin chao v2" > game/assets/hello.txt; cmake --build build
[1/2] Copying assets/ next to game
$ ./game | tail -1
assets/hello.txt: xin chao v2
```

Bước sao chép **chạy ở mỗi lần build** (bạn luôn thấy dòng `Copying ...`) nhưng `copy_directory_if_different` chỉ chép
file đã đổi. Có hai điều thú vị đã kiểm tra:

- File **bị xóa** khỏi thư mục nguồn thì **không bị xóa** khỏi bản sao: thêm `temp.txt`, build, xóa nó khỏi nguồn, build
  lại, `temp.txt` vẫn nằm trong `build/bin/game/assets`. Đây là hành vi được ghi trong comment của `njin_add_assets`.
- Với `Ninja Multi-Config`, assets đi theo từng cấu hình: `mc/Debug/assets/a.txt` và `mc/Release/assets/a.txt`. Chính
  vì có `$<TARGET_FILE_DIR:...>`, không phải đường dẫn viết cứng.

## Tải thư viện bằng FetchContent

Cần EnTT? Không cần cài, không cần chép mã vào repo. CMake tải nó lúc cấu hình. Đây đúng là cách `CMakeLists.txt` gốc của
njin lấy EnTT (và raylib):

```cmake
include(FetchContent)
FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v4.0.0
  SYSTEM)
FetchContent_MakeAvailable(entt)

add_executable(fetch_demo main.cpp)
target_link_libraries(fetch_demo PRIVATE EnTT::EnTT)
```

- `FetchContent_Declare` **khai báo** nơi lấy thư viện, chưa tải gì. `FetchContent_MakeAvailable` tải (nếu cần) rồi gọi
  `add_subdirectory` vào mã tải về, để các target của nó (ở đây `EnTT::EnTT`) dùng được như của bạn.
- **`GIT_TAG` nên ghim** vào một phiên bản (`v4.0.0`, `6.0`) hoặc một mã commit, không phải nhánh như `main`. Nhánh di
  chuyển: hôm nay build được, tuần sau thư viện đổi và game của bạn hỏng mà bạn không đổi gì. Phần inspector của njin ghim
  Dear ImGui vào `v1.92.9` và rlImGui vào cả một mã commit, kèm comment vì sao hai bản phải đi cùng nhau.
- **`SYSTEM`** như đã nói: header của thư viện tải về không làm cảnh báo của bạn tràn ra.

Cần mạng mấy lần? Đã đo với EnTT:

```
$ cmake -S . -B build -G Ninja      (lần đầu, có mạng)
-- Configuring done (11.8s)

$ cmake -S . -B build               (lần hai)
-- Configuring done (1.5s)

$ ls build/_deps
entt-build  entt-src  entt-subbuild
```

Thư viện được tải vào `build/_deps/`. Lần đầu mất khoảng 12 giây (có tải); lần hai chỉ 1,5 giây vì mã đã nằm đó. Nhưng
**xóa `build/` là xóa cả `_deps`**: cấu hình lại mất 10,4 giây, tức là tải lại. Hai cách tránh:

- Đặt nơi tải ngoài `build/` bằng `-DFETCHCONTENT_BASE_DIR=<thư mục>`. Đã thử: cấu hình lần đầu 10,3 giây, xóa `build/`,
  cấu hình lại 4,1 giây (không tải lại).
- Dùng một bản có sẵn trên máy bằng `-DFETCHCONTENT_SOURCE_DIR_<TÊN>=<thư mục>` (`<TÊN>` viết hoa: `RAYLIB`, `ENTT`).
  CMake dùng thư mục đó và không tải gì. Cách này cũng dùng khi làm việc không có mạng, nếu bạn đã có mã nguồn.

Tải bằng git nên máy cần có Git (xem @ref setup).

**Thư viện không có `CMakeLists.txt`**: `FetchContent_MakeAvailable` chỉ tải mã về, và bạn tự viết target. Phần
inspector của njin làm vậy với Dear ImGui: comment ghi "Neither ships a CMakeLists.txt: this only downloads them", rồi
`add_library(njin_imgui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ...)` liệt kê các file `.cpp` của nó bằng tay. Biến
`imgui_SOURCE_DIR` do FetchContent đặt sẵn (tên thư viện + `_SOURCE_DIR`).

## Preset: một lệnh thay cho cả dòng tham số

Gõ `-G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` mỗi lần thì dễ sai. **Preset** là một file
`CMakePresets.json` ở thư mục gốc ghi sẵn các tham số đó và đặt tên:

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_EXPORT_COMPILE_COMMANDS": "ON" }
    },
    { "name": "debug",   "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "release", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" }
  ]
}
```

- Preset `"hidden": true` không dùng trực tiếp được, chỉ để các preset khác **kế thừa** (`inherits`) chung phần cấu
  hình. `debug` và `release` chỉ khác đúng một dòng.
- `${sourceDir}` là thư mục chứa file preset, `${presetName}` là tên preset: mỗi preset có thư mục build riêng.
- Phần `buildPresets` cho phép `cmake --build --preset debug`.

Đã chạy:

```
$ cmake --list-presets
Available configure presets:

  "debug"
  "release"

$ cmake --preset debug
-- Build files have been written to: .../build/debug

$ cmake --build --preset debug
[2/2] Linking CXX executable preset_demo.exe

$ cmake --preset release && cmake --build --preset release
[2/2] Linking CXX executable preset_demo.exe

$ ls build
debug  release
```

`base` không hiện trong danh sách vì bị ẩn. Hai thư mục build tồn tại song song, nên đổi qua lại giữa Debug và
Release không phải cấu hình lại. Viết sai tên preset kế thừa thì CMake báo `Invalid preset: "debug"`.

Preset của njin (mở `CMakePresets.json` ở thư mục gốc) y hệt, chỉ khác `binaryDir`: `debug` dùng `build/`, `release` dùng
`build-release/`. CMake còn đọc `CMakeUserPresets.json` cho preset cá nhân không đưa vào git (chưa thử ở đây).

## Ví dụ đầy đủ: mở một cửa sổ raylib

raylib là thư viện C vẽ đồ họa mà njin dựa vào. Dùng `FetchContent` để tải nó. File `main.c`:

@include learn_cmake_raylib_window.c

Và `CMakeLists.txt` cùng thư mục:

```cmake
cmake_minimum_required(VERSION 3.28)
project(raylib_window LANGUAGES C)

include(FetchContent)

set(BUILD_EXAMPLES OFF)   # raylib đọc biến này: ON sẽ thêm thư mục examples/ của nó vào build

FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 6.0
  SYSTEM)
FetchContent_MakeAvailable(raylib)

add_executable(raylib_window main.c)
target_link_libraries(raylib_window PRIVATE raylib)
```

Chỉ `target_link_libraries(... raylib)` là đủ: các thư viện hệ thống mà raylib cần (đồ họa OpenGL, âm thanh) do target
`raylib` tự khai báo, bạn không phải liệt kê. Cấu hình và build:

```
$ cmake -S . -B build -G Ninja
$ cmake --build build
[30/30] Linking C executable raylib_window.exe
```

Nếu bạn đã có mã raylib 6.0 sẵn trên máy (ví dụ njin đã tải nó vào `build/_deps/raylib-src`), thêm
`-DFETCHCONTENT_SOURCE_DIR_RAYLIB=<thư mục đó>` để khỏi tải lại. Đã chạy đúng cách này: cấu hình mất 6,7 giây, build 30
bước mất 6 giây. Lần đầu **không** có bản sẵn thì CMake tải raylib từ GitHub, và thời gian tải phụ thuộc mạng của bạn
(chưa đo riêng, chỉ đo EnTT ở trên).

Chạy `./build/raylib_window` (Windows: `build\raylib_window.exe`). Một cửa sổ mở ra; nhật ký của raylib ghi:

```
INFO: Initializing raylib 6.0
INFO: DISPLAY: Device initialized successfully
INFO: GLAD: OpenGL extensions loaded successfully
```

@image html learn_cmake_raylib_window.png "Cửa sổ raylib đầu tiên, dựng bằng CMake"

(Ảnh được chụp bằng `TakeScreenshot` ở khung hình thứ 20 của một bản sao chương trình này, build đúng bằng
`CMakeLists.txt` ở trên; bản sao chỉ có thêm một dòng gọi `TakeScreenshot`. Con số FPS thay đổi theo máy.) Bấm Esc hoặc nút đóng để thoát. Đây cũng là khung mà bài shader dùng lại: cùng
`CMakeLists.txt`, chỉ đổi nội dung `main.c`.

## Đọc CMakeLists.txt của njin từ trên xuống

Mở `CMakeLists.txt` ở thư mục gốc njin. Mỗi khối làm một việc:

**1. Phiên bản CMake.** `cmake_minimum_required(VERSION 3.28...4.0)`, kèm comment: raylib 6.0 cần từ 3.25, EnTT v4.0.0
cần từ 3.28.

**2. Đọc phiên bản của njin từ một header.** Số phiên bản chỉ tồn tại ở `njin_version.h`; CMake đọc nó ra để không
bao giờ lệch:

```cmake
file(READ ".../njin_version.h" NJIN_VERSION_HEADER)
foreach(part MAJOR MINOR PATCH)
  string(REGEX MATCH "#define NJIN_VERSION_${part} ([0-9]+)" _ "${NJIN_VERSION_HEADER}")
  set(NJIN_VERSION_${part} "${CMAKE_MATCH_1}")
endforeach()
set(NJIN_VERSION "${NJIN_VERSION_MAJOR}.${NJIN_VERSION_MINOR}.${NJIN_VERSION_PATCH}")
```

`file(READ ...)` đọc file vào biến; `string(REGEX MATCH ...)` tìm dòng `#define NJIN_VERSION_MAJOR 0`, và nhóm trong
ngoặc `([0-9]+)` nằm ở biến `CMAKE_MATCH_1`. Chạy riêng đoạn này bằng `cmake -P` (chế độ chạy một script) trên bản sao của
header, kết quả là `-- njin 0.3.0`.

**3. `project(njin_lab VERSION ${NJIN_VERSION} LANGUAGES C CXX)`.** Vừa C (raylib là C) vừa C++.

**4. Chuẩn và cài đặt chung:** C++20 bắt buộc, không dùng bản mở rộng của trình biên dịch, tắt quét module,
`CMAKE_EXPORT_COMPILE_COMMANDS ON`.

**5. Nơi đặt file build ra:**

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
```

Mọi file chạy vào `build/bin/`, thư viện tĩnh vào `build/lib/`. Từng game có thể ghi đè bằng
`RUNTIME_OUTPUT_DIRECTORY` riêng (như bên trên).

**6. `njin_warnings`:** INTERFACE target chứa `/W4` (MSVC) hoặc `-Wall -Wextra -Wpedantic` (còn lại), như đã học.

**7. FetchContent cho raylib và EnTT**, cả hai ghim phiên bản và có `SYSTEM`. Trước đó có `set(BUILD_EXAMPLES OFF)`.

**8. Sửa một target tải về:** `target_compile_definitions(raylib PRIVATE SUPPORT_SCREEN_CAPTURE=0)`. Vì
`FetchContent_MakeAvailable` đã tạo target `raylib`, bạn thêm định nghĩa vào nó từ ngoài: ở đây tắt việc raylib tự lưu ảnh
chụp khi bấm F12 (comment giải thích: game làm trên njin có thể muốn dùng F12 cho việc khác, và có `njin::screenshot()` thay
thế).

**9. `include(cmake/njin.cmake)`.** Nạp các hàm `njin_add_assets` và `njin_package` cho mọi game (bài này đã dạy
phiên bản rút gọn của cái đầu).

**10. `add_subdirectory`** theo thứ tự: `src/engine/api`, `src/engine/gpu`, `src/engine/runtime` (thư viện), rồi từng
game.

**11. Một tùy chọn:** `option(NJIN_BUILD_INSPECTOR ... ON)` bọc `add_subdirectory(src/tools/inspector)`. Tắt nó thì
không tải Dear ImGui và không build công cụ inspector.

Còn `njin_package(<target> ...)` trong `cmake/njin.cmake`, mỗi game gọi một lần để đóng gói bản phát hành: trên Windows
gắn icon và thông tin phiên bản vào file chạy, bỏ cửa sổ console ở mọi kiểu build trừ Debug, và tạo target
`<tên>_dist` gom file chạy, thư mục assets và vài file kèm (readme, giấy phép) vào một file `.zip`. Mình mới chỉ **đọc**
hàm này để mô tả, chưa chạy nó trong bài học này. Xem @ref samples để biết cách dùng.

## Tự kiểm tra

1. `FetchContent_MakeAvailable` chỉ cần mạng lần đầu vì sao, và vì sao xóa `build/` lại làm nó tải lại?
2. `file(GLOB ...)` không có `CONFIGURE_DEPENDS` gây lỗi gì khi bạn thêm một file `.cpp` mới?
3. Vì sao không nối `mini_warnings` vào thư viện tải về?
4. `include(file.cmake)` và `add_subdirectory(dir)` khác nhau chỗ nào?
5. Vì sao `add_assets` dùng `$<TARGET_FILE_DIR:game>` thay vì một đường dẫn viết cứng?

## Bài tập

1. **Nhiều thư mục assets.** Sửa `add_assets` để gọi được `add_assets(game assets shaders)`, chép cả hai thư mục cạnh
   file chạy.
2. **Preset có công tắc.** Dự án có `option(SHOW_DEBUG ...)` mặc định `OFF`. Thêm hai preset, `plain` và `verbose`, sao cho
   `cmake --preset verbose` bật nó và `cmake --preset plain` thì không, mỗi preset một thư mục build.
3. **Đừng tải lại.** Bạn xóa `build/` mỗi ngày và mỗi lần EnTT lại được tải. Sửa lệnh cấu hình để lần sau không tải
   nữa.

## Đáp án

**Tự kiểm tra**

1. Mã được tải vào `build/_deps/`; những lần cấu hình sau thấy nó ở đó nên không tải lại. Xóa `build/` là xóa
   luôn `_deps`. Đặt `FETCHCONTENT_BASE_DIR` ra ngoài `build/` để tránh.
2. Danh sách file được chụp lúc cấu hình. File mới không có trong danh sách, nên nếu có chỗ gọi hàm của nó thì lỗi liên kết
   `undefined reference` (như `mul(int, int)` ở trên) cho tới khi bạn cấu hình lại bằng tay.
3. Bạn không sửa được mã của họ, nên cảnh báo chỉ là nhiễu che mất cảnh báo trong mã của bạn.
4. `include` chạy file ngay trong phạm vi hiện tại (hàm và biến thấy được ở đó); `add_subdirectory` mở một phạm vi con
   với `CMakeLists.txt` riêng.
5. Vì thư mục chứa file chạy có thể thay đổi theo target và theo kiểu build (ví dụ `Debug/` và `Release/` với
   `Ninja Multi-Config`); generator expression được tính lúc build nên luôn đúng.

**Bài tập 1**

```cmake
function(add_assets target)
  foreach(dir IN LISTS ARGN)
    cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
               NORMALIZE OUTPUT_VARIABLE source)
    cmake_path(GET source FILENAME name)
    add_custom_target(
      ${target}_${name}
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${source}"
              "$<TARGET_FILE_DIR:${target}>/${name}"
      COMMENT "Copying ${name}/ next to ${target}"
      VERBATIM)
    add_dependencies(${target} ${target}_${name})
  endforeach()
endfunction()
```

`ARGN` là các tham số sau `target`. Kết quả đã chạy với `add_assets(ex3 assets shaders)`:

```
[1/4] Copying assets/ next to ex3
[2/4] Copying shaders/ next to ex3
build/assets/a.txt
build/shaders/s.fs
```

**Bài tập 2**

```json
{
  "version": 6,
  "configurePresets": [
    { "name": "plain", "generator": "Ninja", "binaryDir": "${sourceDir}/build/plain" },
    {
      "name": "verbose",
      "inherits": "plain",
      "binaryDir": "${sourceDir}/build/verbose",
      "cacheVariables": { "SHOW_DEBUG": "ON" }
    }
  ]
}
```

```
$ cmake --preset plain
-- SHOW_DEBUG = OFF
$ cmake --preset verbose
-- SHOW_DEBUG = ON
```

**Bài tập 3**

Thêm `-DFETCHCONTENT_BASE_DIR="$PWD/deps"` vào lệnh cấu hình. Đã đo: lần đầu 10,3 giây (có tải); sau
`rm -rf build`, cấu hình lại chỉ mất 4,1 giây và không tải. Thư mục `deps` nên nằm ngoài `build/` và được git bỏ qua.

## Bước tiếp theo

@ref learn_shader_start : shader là gì, và chạy shader đầu tiên trong chính cửa sổ raylib vừa dựng.
