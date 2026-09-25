# Cửa sổ, file và lưu game {#window_files}

@include save_window.cpp

## Cấu hình khi tạo

Ngoài tiêu đề, kích thước, FPS và màu nền, njin::njin_cfg có:

| Trường | Mặc định | Ý nghĩa |
|---|---|---|
| `fixed_hz` | 60 | Số nhịp `phase_fixed_update` mỗi giây |
| `exit_key` | `key_escape` | Phím đóng game ngay. Đặt `key_none` khi cần Esc cho menu |
| `resizable` | `false` | Cho phép kéo đổi kích thước cửa sổ |
| `app_name` | tiêu đề | Tên thư mục lưu game |

## Cửa sổ

| Hàm | Việc làm |
|---|---|
| njin::screen_size() | Kích thước **hiện tại** của cửa sổ |
| njin::window_resized() | Cửa sổ vừa đổi kích thước ở frame này |
| njin::window_set_fullscreen() / njin::window_fullscreen() | Toàn màn hình dạng cửa sổ không viền |
| njin::window_set_size(), njin::window_set_title() | Đổi kích thước, tiêu đề |
| njin::cursor_set_visible() | Ẩn hiện con trỏ chuột |
| njin::cursor_set_locked() | Khóa con trỏ trong cửa sổ; đọc njin::mouse_delta() |
| njin::njin_quit() | Thoát ở cuối frame. `phase_shutdown` vẫn chạy |

**Giữ màn chơi vừa mọi kích thước cửa sổ.** Thiết kế game theo một kích thước cố định,
rồi mỗi frame đặt zoom của camera cho vừa cửa sổ:

```cpp
const njin::vec2 screen = njin::screen_size(ctx);
cam.zoom = std::fmin(screen.x / 960.0f, screen.y / 540.0f);
cam.offset = screen * 0.5f;
```

## Chụp màn hình

njin::screenshot() lưu ảnh của frame hiện tại ra file:

```cpp
if (njin::key_pressed(ctx, njin::key_f12))
  njin::screenshot(ctx);                      // tự đặt tên, trong thư mục lưu game
njin::screenshot(ctx, "captures/level1.png"); // hoặc chỉ định đường dẫn
```

- Ảnh được chụp ở **cuối frame**, sau `phase_post_render`, nên có đủ thế giới lẫn UI dù
  gọi ở phase nào.
- Không truyền đường dẫn thì ảnh vào `screenshots/` trong thư mục lưu game (xem
  njin::save_path()), tên theo ngày giờ, không bao giờ trùng nhau.
- Định dạng theo đuôi file: `.png`, `.bmp`, `.tga`, `.qoi`. Đuôi khác thì không lưu và ghi
  cảnh báo vào log.

@note raylib mặc định tự chụp màn hình khi bấm **F12** trong mọi game, lưu vào thư mục làm
việc. njin **tắt** hành vi ngầm đó (cờ `SUPPORT_SCREEN_CAPTURE=0` trong `CMakeLists.txt`),
nên F12 là phím bình thường, game tự quyết định dùng vào việc gì.

## Đường dẫn tài nguyên

Mọi hàm nạp (texture, shader, font, sound, music) tìm file theo thứ tự:

1. đúng đường dẫn đã cho, tính từ thư mục làm việc,
2. nếu không thấy và là đường dẫn tương đối: tính từ **thư mục chứa file exe**.

Nhờ bước 2, game chạy bằng cách nhấp đúp file exe (thư mục làm việc khác) vẫn tìm thấy
`assets/` đặt cạnh exe.

## File

| Hàm | Việc làm |
|---|---|
| njin::file_exists() | File có tồn tại không |
| njin::file_read() | Đọc toàn bộ file vào một chuỗi |
| njin::file_write() | Ghi toàn bộ file, tạo thư mục cha nếu cần |
| njin::save_path() | Đường dẫn file lưu game trong thư mục của người dùng |

njin::file_write() ghi vào một file tạm rồi đổi tên, nên game tắt giữa chừng cũng không
làm hỏng file cũ.

njin::save_path() trả về đường dẫn trong thư mục riêng của người dùng, tạo thư mục nếu
chưa có:

| Hệ điều hành | Thư mục |
|---|---|
| Windows | `%APPDATA%\<app_name>` |
| macOS | `~/Library/Application Support/<app_name>` |
| Linux | `~/.local/share/<app_name>` |

Mọi đường dẫn là UTF-8, nên tên file và thư mục có dấu tiếng Việt vẫn đúng.
