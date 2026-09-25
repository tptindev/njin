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

