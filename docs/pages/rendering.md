# Vẽ ảnh và shader {#rendering}

Trang này nói về **texture** (ảnh từ file) và **shader**. Hình khối và chữ ở @ref drawing,
sprite có animation ở @ref sprites, ảnh ngoài màn hình ở @ref post_processing.

## Handle

Texture, shader và render texture được quản lý qua **handle**, một struct nhỏ chứa `id`:

- `id == 0` là handle **không hợp lệ**, ví dụ khi file không tồn tại.
- Truyền handle không hợp lệ hoặc đã unload vào bất kỳ hàm nào thì hàm đó **không làm gì**,
  không gây lỗi.
- Handle đã unload không bao giờ trỏ nhầm sang tài nguyên mới.

Tài nguyên được giải phóng tự động khi gọi njin::njin_destroy(). Chỉ cần gọi
`*_unload` khi muốn giải phóng sớm.

## Texture

```cpp
njin::texture_handle tex = njin::texture_load(ctx, "assets/player.png");
njin::vec2 size = njin::texture_size(ctx, tex);
njin::texture_draw(ctx, tex, {100.0f, 100.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
```

- njin::texture_load() nạp ảnh; đường dẫn tính từ thư mục làm việc khi chạy game.
- njin::texture_draw() vẽ với **góc trên trái** tại vị trí cho trước.
- Tham số cuối là **màu nhân** (njin::rgba, mỗi kênh 0..1). Trắng `{1,1,1,1}` giữ
  nguyên ảnh; `{1,0,0,1}` chỉ giữ kênh đỏ.

Vẽ texture trong `phase_render` (không gian thế giới, chịu ảnh hưởng camera) hoặc
`phase_post_render` (không gian màn hình). Xem @ref game_loop.

## Atlas: ghép nhiều ảnh vào một texture {#atlas}

Raylib gom các lệnh vẽ **liền nhau dùng chung texture** thành một lệnh. Sprite xếp theo `layer`
hoặc theo y (top-down) mà dùng nhiều texture khác nhau thì bị ngắt mỗi lần đổi texture: 90 sprite
xen kẽ ba ảnh là 90 lệnh vẽ. Ghép các ảnh nhỏ vào một atlas thì chúng cùng một texture, và số
lệnh còn hai.

```cpp
const njin::atlas_handle atlas = njin::atlas_create(ctx, {.size = 2048});
const njin::texture_handle hero = njin::atlas_load(ctx, atlas, "assets/hero.png");
const njin::texture_handle tree = njin::atlas_load(ctx, atlas, "assets/tree.png");
```

Kết quả là njin::texture_handle thường: đưa vào njin::sprite, njin::tilemap, njin::particle_emitter,
UI hay njin::texture_draw() đều được. Vẽ ảnh không co giãn, không xoay thì **giống hệt từng
pixel** so với nạp riêng.

| Việc | Cách xử lý |
|---|---|
| Ảnh lem sang ảnh bên cạnh khi phóng hoặc xoay | Mỗi ảnh có viền `padding` (mặc định 1 pixel) chép lại điểm ảnh ngoài cùng |
| Trang đầy | Atlas tự thêm trang mới; ảnh trên trang khác nhau vẫn là hai texture |
| Ảnh lớn hơn cả trang | Nạp như njin::texture_load(), có cảnh báo trong log |
| njin::texture_size() | Kích thước của ảnh, không phải của cả trang |
| Shader riêng của game | Lấy mẫu theo toạ độ của cả trang, không phải của ảnh. Ảnh dùng với shader kiểu đó thì nạp bằng njin::texture_load() |
| njin::texture_set_filter() | Đổi bộ lọc của cả trang |
| Hot reload, njin::texture_unload() | Không áp dụng cho ảnh trong atlas; chỗ đã xếp không được thu hồi |

Nên xếp vào atlas những ảnh nhỏ hay xuất hiện cùng nhau (nhân vật, kẻ địch, vật phẩm, đạn), và
để tileset hoặc ảnh nền lớn nạp riêng.

## Cắt bỏ ngoài màn hình và số lệnh vẽ {#render_stats}

Module sprite bỏ qua sprite và emitter hạt **nằm hoàn toàn ngoài camera**: không sắp xếp, không
tạo đỉnh. 40.000 sprite rải khắp bản đồ mà chỉ vài trăm nằm trong khung hình mất khoảng 1 ms thay
vì 9 ms. Khi camera đang rung thì việc cắt tạm tắt, vì rung để lộ một chút phần ngoài khung.

njin_inspector hiện ở cửa sổ **Performance** những gì frame vừa rồi đã vẽ: số sprite (và số bị cắt),
số chunk tilemap, số hạt (bao nhiêu trên GPU), và **số lệnh vẽ ước tính**. Raylib không báo số lệnh
vẽ thật, nên con số này được tính từ các lần đổi texture và blend mode; nó tăng lên khi sprite khác
texture xen kẽ nhau, và giảm khi dùng atlas.

## Card đồ họa rời trên laptop có hai card {#discrete_gpu}

Laptop có card onboard (tích hợp) và card rời thường khởi động chương trình trên card onboard cho
đỡ tốn pin. Game njin xin chạy trên **card rời** khi máy có, trên mọi hệ điều hành:

| Hệ điều hành | Cách làm |
|---|---|
| Windows | File `.exe` xuất hai biến `NvOptimusEnablement` và `AmdPowerXpressRequestHighPerformance` mà driver NVIDIA và AMD tìm. Hai biến phải nằm trong chính `.exe`, nên game link target `njin::gpu` (biên dịch thẳng vào exe): `target_link_libraries(my_game PRIVATE njin::rt njin::gpu)` |
| Linux | Chỉ trên **laptop** (nhận bằng loại khung máy SMBIOS, hoặc có pin): đặt `DRI_PRIME=1` (Mesa), và với driver NVIDIA riêng thì `__NV_PRIME_RENDER_OFFLOAD=1` cùng `__GLX_VENDOR_LIBRARY_NAME=nvidia` nếu thư viện GLX của NVIDIA có trên máy. Biến nào người dùng đã đặt rồi thì được giữ nguyên |
| macOS | Không cần làm gì: máy Mac hai card dùng card rời cho mọi app không xin chuyển card tự động |

Máy chỉ có một card thì không có gì thay đổi.

**PC để bàn** khác laptop: card nào chạy game là card mà **màn hình cắm vào**. Màn hình cắm vào card
rời thì game đã chạy trên card rời; cắm vào cổng của bo mạch chủ (card onboard) thì game chạy trên
card onboard, và engine không đổi được điều đó. Trên Linux engine cố ý không đặt biến offload cho PC
để bàn, vì ở đó "card còn lại" chính là card onboard. Với PC có hai card, cách chắc chắn là cắm màn hình
vào card rời. Người chơi vẫn quyết định cuối cùng: mục *Graphics
settings* của Windows hoặc bảng điều khiển driver ghi đè yêu cầu này cho từng game. Tắt hẳn khi build
bằng `-DNJIN_PREFER_DISCRETE_GPU=OFF`. Game không link `njin::gpu` thì trên Windows không có yêu cầu này. Log lúc khởi động ghi rõ card nào đang được dùng (dòng
`Renderer:`).

## Shader

Shader biến đổi cách mọi thứ được vẽ. njin dùng GLSL 330.

@include texture_shader.cpp

Quy trình: đặt uniform, `shader_begin`, vẽ, `shader_end`. Mọi thứ vẽ giữa
`shader_begin` và `shader_end` đi qua shader.

@warning Đặt uniform **trước** `shader_begin`.

Ví dụ fragment shader chuyển ảnh sang xám:

@include gray.fs

Một số điểm cần biết:

- Truyền `nullptr` cho vertex shader (hoặc fragment shader) để giữ shader mặc định của raylib.
- `fragTexCoord`, `fragColor`, `texture0`, `colDiffuse` là tên **raylib quy định**.
  `texture0` là ảnh đang vẽ; `fragColor` là màu nhân bạn truyền cho `texture_draw`.
- njin::shader_load() trả về handle `id == 0` nếu file thiếu hoặc lỗi biên dịch, và ghi log lý do.
- Uniform không tồn tại trong shader chỉ được ghi cảnh báo **một lần** rồi bị bỏ qua.

Các hàm đặt uniform:

| Hàm | Kiểu GLSL |
|---|---|
| njin::shader_set_i32() | `int` |
| njin::shader_set_f32() | `float` |
| njin::shader_set_vec2() | `vec2` |
| njin::shader_set_vec4() | `vec4` |

## Hot reload

Sửa ảnh hay shader trong lúc game đang chạy, lưu lại, và thấy kết quả ngay, không cần khởi
động lại:

```cpp
#ifndef NDEBUG
njin::hot_reload_enable(*ctx, true);
#endif
```

Khi bật, engine kiểm tra thời gian sửa của mọi file texture và shader đã nạp, vài lần mỗi giây.
File đổi được nạp lại **vào đúng handle cũ**: sprite, tilemap, ảnh của level, shader post đang
dùng nó đổi theo ngay, không phải sửa code. Một file vừa đổi được nạp ở lần kiểm tra sau, khi
nó đã thôi đổi, để không đọc nhầm file editor đang ghi dở.

Shader **lỗi biên dịch thì giữ bản cũ**, và lỗi của trình biên dịch nằm trong log: sửa shader
sai không làm game hỏng, sửa đúng thì bản mới vào ngay.

| Hàm / event | Việc làm |
|---|---|
| njin::hot_reload_enable() | Bật, tắt, và chọn khoảng giữa hai lần kiểm tra |
| njin::hot_reload_now() | Kiểm tra và nạp lại ngay, kể cả khi đang tắt (ví dụ gắn vào phím F5) |
| njin::asset_reloaded | Event gửi qua njin::events() sau mỗi lần nạp lại, kể cả khi thất bại |

Mặc định tắt: kiểm tra file tốn một chút thời gian, và game đã phát hành không cần.

