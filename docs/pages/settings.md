# Cài đặt của người chơi {#settings}

Âm lượng, phím, toàn màn hình, ngôn ngữ: những thứ người chơi tự chỉnh và mong game nhớ.

@include settings_rebind.cpp

## Kênh âm thanh

njin::audio_bus chia âm thanh thành kênh, như thanh trượt trong menu cài đặt. Âm lượng thật của
một âm là âm lượng riêng của nó, nhân âm lượng kênh, nhân kênh `bus_master`.

| Kênh | Dùng cho |
|---|---|
| `bus_master` | Nhân vào tất cả |
| `bus_music` | Mọi music |
| `bus_sfx` | Hiệu ứng trong game (mặc định của sound) |
| `bus_ui` | Tiếng menu; đưa vào bằng sound_set_bus() |
| `bus_voice` | Lồng tiếng |

audio_set_bus_volume() và audio_set_bus_muted() có hiệu lực ngay, cả với âm đang phát.
music_crossfade() chuyển nhạc mượt (theo giờ thật, nên chạy cả khi game pause);
music_fade_in() và music_fade_out() cho từng bản.

## Đổi phím

Một action có thể gắn phím, nút chuột và nút tay cầm. Để người chơi đổi:

- **ui_keybind()**: một dòng trong menu, "Nhảy | Space". Bấm vào rồi bấm phím mới (Esc hủy). Dùng
  `ui_keybind(ctx, "Nhảy##pad", action, true)` cho dòng nút tay cầm. Trong lúc chờ, UI không điều
  hướng; kiểm tra ui_keybind_listening() để không coi Esc là "đóng menu".
- action_rebind() là phép đổi phím ở dưới: nguồn mới thay nguồn **cùng thiết bị** của action, và bị
  gỡ khỏi mọi action khác để một phím không làm hai việc. Nhận phím bằng input_any_pressed().
- input_source_name() cho chữ hiện ("Space", "Left Shift", "Pad A"), và action_sources() liệt kê phím
  đang gắn: dùng để vẽ gợi ý "Bấm E để nói chuyện" đúng với phím người chơi đã đổi.

## Lưu và nạp

@code
njin::settings_load(ctx); // sau khi đăng ký hết action, axis và nạp ngôn ngữ
njin::settings_save(ctx); // khi rời menu cài đặt
@endcode

File là JSON trong thư mục lưu game (`settings.json`, xem save_path()): âm lượng và tắt tiếng từng
kênh, phím của mọi action và axis, toàn màn hình, ngôn ngữ. Phần nào không có trong file thì giữ
nguyên, nên game thêm action mới trong bản cập nhật vẫn có phím mặc định. Dữ liệu riêng của game
(độ khó, độ sáng) đi kèm dưới khóa `game`: `settings_save(ctx, "settings.json", &data)`.

Nếu cần tự lưu phím vào chỗ khác: input_bindings_save() và input_bindings_load().

## Rung tay cầm

pad_rumble() cho hai motor (trầm và nhanh) trong một khoảng thời gian; tay cầm không có motor thì
không làm gì. Kết hợp với camera_shake() và hitstop() khi trúng đòn.
