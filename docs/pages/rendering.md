# Vẽ ảnh và shader {#rendering}

Hiện njin có hai thứ để vẽ: **texture** (ảnh từ file) và **render texture** (ảnh
ngoài màn hình, xem @ref post_processing). Cả hai được vẽ bằng lệnh có tên `*_draw`.

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
