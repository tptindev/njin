<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/brand/njin-logo-dark.svg">
    <img src="docs/images/brand/njin-logo.svg" alt="njin" width="360">
  </picture>
</p>

njin là game engine mã nguồn mở viết bằng C++20, làm cả game **2D** lẫn **3D**. Engine dùng EnTT cho ECS, raylib cho cửa sổ, đồ họa, âm thanh và input, và Jolt Physics cho vật lý 3D; game chỉ cần include API của njin qua `njin.h`.

Phiên bản hiện tại: **0.1.0**. Đây là bản phát hành mở đầu từ trạng thái nguồn hiện tại. API vẫn đang phát triển và có thể thay đổi giữa các bản minor trước 1.0. Xem [CHANGELOG.md](CHANGELOG.md) để biết chi tiết.

## Tính năng

- ECS, module và system theo phase; scene, prefab và vòng lặp game cố định.
- Sprite, animation, tilemap vuông từ Tiled/LDtk, va chạm và camera theo nhân vật.
- Bộ điều khiển nhân vật cho platformer và top-down, navigation A*, particle và hiệu ứng hậu kỳ.
- UI, âm thanh, lưu cài đặt, hội thoại và bản địa hóa.
- 3D: camera phối cảnh, hình khối và hình SDF mịn, model glTF với vật liệu, ánh sáng có bóng đổ và sương mù, hạt 3D, instancing, chọn vật bằng tia.
- Vật lý 3D (Jolt Physics): body tĩnh, kinematic, động; nhân vật đi trên sàn, leo bậc, đứng trên bục di chuyển; raycast.
- Gizmo để debug 2D và 3D.
- Màn hình ảo cho pixel art, khử răng cưa bằng supersampling và công cụ `njin_inspector` để xem entity, system, log, hiệu năng, tài nguyên và cảnh 3D khi game chạy.

## Editor tạo model và khung xương

Chạy `run_model_editor.bat` để mở **njin Model Editor**: tạo hình bằng khối SDF,
ghép/cắt khối, dựng cây xương, gắn khối, chỉnh tư thế và tạo animation bằng timeline
keyframe. Có gizmo ImGuizmo và xem trước SDF trên GPU. Công cụ lưu model/clip trong
JSON và xuất model tĩnh OBJ. Xem [hướng dẫn Model Editor](src/tools/model_editor/README.md).

## Game mẫu

| Target | Nội dung |
|---|---|
| `njin_sandbox` | Mẫu tối giản để bắt đầu với engine |
| `njin_pong` | Game Pong hoàn chỉnh với menu, âm thanh và lưu kỷ lục |
| `njin_platformer` | Platformer có bản đồ, dốc, nhảy và hội thoại |
| `njin_topdown` | Game top-down có tilemap, chiến đấu và quái tìm đường |
| `njin_debug_demo` | Mẫu dùng thử `njin_inspector` |
| `njin_render_demo` | Mẫu atlas, particle, culling và hậu kỳ |
| `njin_tower_defense` | Game thủ thành với tháp phòng thủ và các đợt quái |
| `njin_fps` | Bắn súng góc nhìn thứ nhất: model glTF, bóng đổ, đèn, vệt đạn phát sáng, hạt 3D |
| `njin_sokoban` | Đẩy thùng 2.5D: instancing, nhân vật hình SDF, đèn trên ô đích |
| `njin_platformer3d` | Platformer 3D góc nhìn thứ ba trên vật lý Jolt: nhảy đôi, bục di chuyển, thùng đẩy được |

Các demo có thể build độc lập, tái sử dụng engine đã build; xem
[hướng dẫn game project](src/games/README.md). Dùng `-DNJIN_BUILD_EXAMPLES=OFF`
để chỉ cấu hình engine và tools. Game nghiêm túc được quản lý bằng repository
riêng; thư mục game mới được ignore mặc định.

## Yêu cầu

- CMake **3.28 trở lên**
- Trình biên dịch hỗ trợ **C++20**
- Ninja và Git
- Kết nối mạng trong lần cấu hình đầu tiên để CMake tải raylib, EnTT, Jolt Physics và (khi bật inspector) Dear ImGui cùng các thư viện liên quan

Trên Linux, raylib cần thư viện phát triển cho X11 và OpenGL. Ví dụ Ubuntu/Debian:

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
```

## Build và chạy

Clone repository rồi cấu hình và build game Pong:

```sh
git clone https://github.com/tptindev/njin.git
cd njin
cmake --preset debug
cmake --build --preset debug --target njin_pong
```

Chạy chương trình đã build:

```sh
# Windows với GCC
build\bin\njin_pong.exe

# Windows với Visual Studio
build\bin\Release\njin_pong.exe

# Linux hoặc macOS
build/bin/njin_pong
```

Bỏ `--target njin_pong` để build tất cả game mẫu và inspector. Có thể thay target bằng bất kỳ game nào trong bảng trên. Các game có thư mục output riêng chạy từ đó, ví dụ `build/bin/topdown/`, `build/bin/platformer/` và `build/bin/tower_defense/`.

Trên Windows, các script tiện ích có sẵn:

- `build.bat` cấu hình và build Debug bằng Ninja.
- `run.bat` build rồi chạy `njin_sandbox`.
- `run_inspected.bat [tên_game]` build và chạy game cùng `njin_inspector` (mặc định là `debug_demo`).

Để tạo bản Release, dùng preset `release`; kết quả nằm trong `build-release/`:

```sh
cmake --preset release
cmake --build --preset release
```

## Tài liệu

Tài liệu hướng dẫn và tra cứu bằng tiếng Việt nằm trong [`docs/pages/`](docs/pages/). Bắt đầu với [cài đặt môi trường](docs/pages/setup.md), [build chương trình đầu tiên](docs/pages/getting_started.md), hoặc xem [các game mẫu](docs/pages/samples.md).

## Cấu trúc repository

```text
src/engine/api/       Header API công khai; game include njin.h
src/engine/runtime/   Phần triển khai engine
src/games/            Game mẫu và mã dùng chung
src/tools/inspector/  Công cụ debug riêng
docs/pages/           Tài liệu tiếng Việt
```

## Đóng góp và báo lỗi

Mở [GitHub Issues](https://github.com/tptindev/njin/issues) để báo lỗi hoặc đề xuất cải tiến. njin làm game 2D và 3D; xem thêm [CHANGELOG.md](CHANGELOG.md) và tài liệu trong `docs/pages/` trước khi bắt đầu.
