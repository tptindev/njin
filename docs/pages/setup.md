# Cài môi trường {#setup}

Trang này hướng dẫn cài những thứ cần có để build njin trên từng hệ điều hành. Làm xong là chạy được
`njin_pong`, và tiếp tục với @ref getting_started.

## Cần những gì

| Thứ cần có | Để làm gì | Yêu cầu |
|---|---|---|
| Trình biên dịch C++20 | biên dịch engine và game | GCC 15.2 đã thử; bản cũ hơn chưa thử |
| **CMake** | cấu hình build | 3.28 trở lên |
| **Ninja** | chạy build (các preset dùng nó) | bản nào cũng được |
| **Git** | CMake tải raylib 6.0, EnTT v4.0.0 và Dear ImGui về lúc cấu hình lần đầu | bản nào cũng được |
| Kết nối mạng | cho lần cấu hình đầu tiên (không cần lại sau đó) | |

Trình biên dịch, CMake và Ninja phải chạy được từ terminal: gõ tên lệnh ra phiên bản là đúng.

## Trạng thái từng nền tảng

Trang này nói thật về những gì đã được thử:

| Nền tảng | Trình biên dịch | Trạng thái |
|---|---|---|
| Windows 10/11 | GCC (w64devkit) | **Đã dùng để phát triển njin và chạy thử các bước dưới đây** |
| Windows 10/11 | MSVC (Visual Studio) | Có trong CI (`.github/workflows/build.yml`), chưa thử tay |
| Linux (Ubuntu) | GCC | **Đã thử** trên Ubuntu 26.04 (WSL2): bản clone mới, các bước dưới đây, build đủ mọi target, Pong chạy |
| Linux (Debian) | GCC | Có trong CI, chưa thử tay |
| Linux (Fedora, Arch) | GCC | Chưa thử. Tên gói lấy theo hướng dẫn build raylib |
| macOS | Clang (Xcode) | Chưa thử |

Gặp lỗi trên nền tảng "chưa thử", đó có thể là lỗi thật của njin chứ không phải lỗi của bạn: hãy báo ở mục
Issues của repo.

## Windows với GCC (w64devkit)

Đây là môi trường phát triển của njin.

1. **Trình biên dịch**: cài **w64devkit** (GCC cho Windows, không cần cài đặt phức tạp). Tải từ
   <https://github.com/skeeto/w64devkit/releases>, giải nén vào một thư mục không có dấu cách hay dấu
   tiếng Việt (ví dụ `C:\Dev\w64devkit`). Bản đóng gói kèm trong bộ cài raylib cũng dùng được: bản đã thử
   là `C:\Dev\raylib\w64devkit` với GCC 15.2.
2. **CMake, Ninja và Git** bằng winget (có sẵn trên Windows 11 và các bản Windows 10 gần đây):

   ```
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   winget install Git.Git
   ```

   Có thể bỏ qua CMake hoặc Ninja nếu thư mục `bin` của w64devkit bạn tải đã có sẵn. Kiểm tra ở bước 4.
3. **Thêm vào PATH**: thêm thư mục `bin` của w64devkit (ví dụ `C:\Dev\w64devkit\bin`) vào biến môi trường
   `Path` của Windows (Settings, tìm "environment variables", sửa `Path`). Mở terminal **mới** để nhận
   thay đổi.
4. **Kiểm tra**: trong terminal mới:

   ```
   g++ --version
   cmake --version
   ninja --version
   git --version
   ```

   Cả bốn lệnh phải in ra phiên bản, và CMake từ 3.28 trở lên. Đã thử với GCC 15.2, CMake 4.0 và
   Ninja 1.13.
5. **Build và chạy** (xem @ref setup_first_build ở dưới).

Trên Windows còn có hai script ở thư mục gốc repo: `build.bat` cấu hình và build Debug, `run.bat` build và chạy
`njin_sandbox`. Xem @ref getting_started.

## Windows với MSVC (Visual Studio)

@warning Đường này có trong CI nhưng chưa được thử tay. Nếu vấp, báo lỗi ở Issues.

1. Cài **Visual Studio 2022** (bản Community miễn phí) hoặc **Build Tools for Visual Studio 2022**, chọn
   workload **Desktop development with C++**.
2. Cài CMake và Git như trên: `winget install Kitware.CMake` và `winget install Git.Git`. Không cần Ninja.
3. Trong thư mục repo:

   ```
   cmake -S . -B build -A x64
   cmake --build build --config Release --parallel
   ```

   Đây đúng là hai lệnh mà CI chạy. Không dùng `cmake --preset`, vì các preset chọn Ninja.
4. Chương trình nằm ở `build\bin\Release\`, ví dụ `build\bin\Release\njin_pong.exe`.

## Linux

@note Đã thử trên **Ubuntu 26.04 chạy trong WSL2** với GCC 15.2, CMake 4.2.3 và Ninja 1.13.2: cài đúng danh sách gói
Ubuntu dưới đây trên một bản cài mới, clone repo, `cmake --preset debug`, build `njin_pong` (rồi build mọi target,
gồm njin_inspector và các game mẫu) đều thành công, và Pong mở được cửa sổ. Cửa sổ đó vẽ bằng bộ dựng hình phần mềm
của Mesa (`llvmpipe`) qua WSLg, nên chưa thử với GPU thật trên Linux. Debian, Fedora và Arch chưa thử.

raylib vẽ bằng OpenGL trên X11 nên cần các thư viện phát triển của chúng.

**Ubuntu và Debian** (Ubuntu đã thử, Debian chưa):

```
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
```

**Fedora** (chưa thử):

```
sudo dnf install gcc-c++ cmake ninja-build git mesa-libGL-devel libX11-devel \
  libXrandr-devel libXi-devel libXcursor-devel libXinerama-devel alsa-lib-devel
```

**Arch** (chưa thử):

```
sudo pacman -S --needed base-devel cmake ninja git mesa libx11 libxrandr libxi libxcursor libxinerama alsa-lib
```

Kiểm tra bằng `g++ --version`, `cmake --version` (từ 3.28), `ninja --version`. Distro cũ có thể có CMake dưới
3.28: khi đó cài bản mới từ trang chủ CMake, hoặc `pip install cmake`.

Rồi build và chạy như dưới đây. Trên Linux file chạy không có đuôi `.exe`: `build/bin/njin_pong`.

## macOS

@warning Chưa được thử. Đây là các bước chuẩn để build một dự án raylib, chưa kiểm chứng với njin.

1. Cài công cụ dòng lệnh của Xcode: `xcode-select --install`.
2. Cài [Homebrew](https://brew.sh), rồi: `brew install cmake ninja git`.
3. Build và chạy như dưới đây.

Mã nguồn của njin có nhánh riêng cho POSIX (mạng của inspector, đọc thời gian file), nhưng những nhánh này chưa
từng được biên dịch trên macOS.

## Build và chạy lần đầu {#setup_first_build}

Thư mục gốc repo, sau khi đã cài xong:

```
git clone https://github.com/tptindev/njin.git
cd njin
cmake --preset debug
cmake --build --preset debug --target njin_pong
```

Lần cấu hình đầu tiên tải raylib, EnTT và Dear ImGui từ GitHub nên mất một lúc và cần mạng; các lần sau thì
không. Rồi chạy:

| Hệ điều hành | Lệnh |
|---|---|
| Windows (GCC) | `build\bin\njin_pong.exe` |
| Windows (MSVC) | `build\bin\Release\njin_pong.exe` |
| Linux, macOS | `build/bin/njin_pong` |

@image html pong_menu.png "Cửa sổ menu của njin_pong: nếu thấy nó, môi trường đã đủ"

Một cửa sổ Pong mở ra là môi trường đã đủ. Bỏ `--target njin_pong` để build mọi thứ (các game mẫu và
njin_inspector). Preset `release` build vào `build-release/` với tối ưu, xem @ref getting_started.

## Gặp lỗi

| Triệu chứng | Nguyên nhân thường gặp | Cách xử lý |
|---|---|---|
| `cmake: command not found` hoặc `'cmake' is not recognized` | CMake chưa cài, hoặc terminal mở trước khi sửa `Path` | Cài CMake, mở terminal mới |
| CMake báo không tìm thấy `Ninja` | Ninja chưa cài hoặc không nằm trong `Path` | Cài Ninja, mở terminal mới, kiểm tra `ninja --version` |
| Cấu hình dừng ở "Cloning into 'raylib-src'" hoặc báo lỗi git | Thiếu Git, không có mạng, hoặc mạng chặn GitHub | Kiểm tra `git --version` và truy cập được `github.com` |
| CMake báo phiên bản cần 3.28 | CMake cũ | Cài CMake mới hơn |
| Linker không ghi được `njin_*.exe` (Windows) | Game vẫn đang chạy | Đóng game rồi build lại |
| Game báo không tìm thấy `assets/...` | Chạy từ sai thư mục | Game có assets chạy từ thư mục riêng `build/bin/<tên game>/`, xem @ref samples |
| Linux: thiếu `X11/Xlib.h`, `GL/gl.h` | Thiếu thư viện phát triển | Cài các gói ở mục Linux |
| Đổi trình biên dịch mà CMake vẫn dùng cái cũ | CMake nhớ trình biên dịch trong `build/` | Xóa thư mục `build/` rồi cấu hình lại |

## Bước tiếp theo

- @ref learn : nếu chưa quen C, C++, CMake hay shader, 12 bài học từ đầu
- @ref getting_started : chương trình đầu tiên
- @ref first_jump và @ref first_walk : một nhân vật chạy được trong 50 dòng
