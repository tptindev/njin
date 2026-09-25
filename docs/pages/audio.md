# Âm thanh {#audio}

njin có hai loại âm thanh, cho hai nhu cầu khác nhau:

| | **Sound** | **Music** |
|---|---|---|
| Dùng cho | Hiệu ứng ngắn: tiếng bắn, nhấp, va chạm | Nhạc nền, âm thanh môi trường dài |
| Lưu ở đâu | Nằm **hết trong bộ nhớ** | **Stream từ đĩa**, giải mã từng đoạn |
| Phát chồng nhiều bản | Có (tối đa 8) | Không, một bài một luồng |
| Lặp | Có, nhưng chỗ nối có quãng lặng cỡ một frame | Có, **không** có quãng lặng |
| Hàm | `sound_*` | `music_*` |

Cả hai dùng handle giống texture và shader: xem @ref rendering.

## Ví dụ

@include audio.cpp

## Nạp và thất bại

njin::sound_load() và njin::music_load() trả về handle có `id == 0` khi:

- Máy **không có thiết bị âm thanh** (không loa, lỗi driver). Game vẫn chạy bình thường,
  chỉ là không có tiếng.
- File **không tồn tại**.
- File **không có đuôi** (như `Doxyfile`). raylib chọn bộ giải mã theo đuôi file nên
  không đoán được định dạng.
- File **không giải mã được**.

Mọi trường hợp đều ghi log lý do kèm đường dẫn. Mọi hàm nhận handle không hợp lệ hoặc
đã unload thì **không làm gì**, nên bạn không cần kiểm tra ở mỗi chỗ gọi.

Định dạng hỗ trợ là những gì raylib giải mã được, gồm wav, ogg, mp3, flac. Đường dẫn
tính từ thư mục làm việc khi chạy game (xem @ref getting_started).

## Sound

### Phát

Bốn cách phát, cho bốn nhu cầu:

| Hàm | Hành vi | Hợp với |
|---|---|---|
| njin::sound_play_once() | **Chồng** lên các bản đang phát, tối đa 8. Quá số đó thì bản chạy lâu nhất bị cắt | Âm thanh nhiều thứ cùng kích hoạt: đám đông được nghe như đám đông |
| njin::sound_play_once_at() | Như trên, nhưng chỉnh **cao độ** và **độ lớn** cho riêng bản này | Cùng một bản ghi cho nhiều vật kích cỡ khác nhau |
| njin::sound_play_restart() | **Cắt** mọi bản đang phát rồi phát lại từ đầu | Tiếng nhấp giao diện, tiếng cảnh báo: bấm dồn thì nghe rõ từng lần |
| njin::sound_play_loop() | Phát lặp cho đến khi njin::sound_stop() | Tiếng động cơ, tiếng nền ngắn |

Với njin::sound_play_once_at(), `pitch` 1 là như bản ghi và 2 là cao gấp đôi, `gain` nhân
vào âm lượng của sound. Cả hai **chỉ áp dụng cho bản đó**: lần phát thường sau đó trở về
1 và 1.

### Âm thanh theo vị trí

njin::sound_play_at() phát như njin::sound_play_once() kèm một vị trí trong thế giới:

- **Âm lượng** giảm dần theo khoảng cách tới điểm camera đang nhìn: đủ to trong khoảng gần,
  nhỏ dần đều, im hẳn ở khoảng xa. Chỉnh bằng njin::audio_set_range() (mặc định 200 và 1200).
- **Trái phải**: tiếng lệch sang loa trái hoặc phải theo vị trí trên màn hình.

### Âm lượng và tắt tiếng

- njin::sound_set_volume(): 1 là âm lượng gốc, 0 là im lặng. Áp dụng ngay cả cho những bản
  đang phát.
- njin::sound_set_muted(): đưa âm lượng đang phát về 0 **mà không đụng đến âm lượng đã
  lưu**, nên bật lại thì khôi phục đúng như cũ.

### Âm thanh tự sinh

Không phải âm thanh nào cũng đọc từ file. njin::sound_load_samples() tạo sound từ các mẫu
trong bộ nhớ:

@include audio_procedural.cpp

## Music

njin::music_load() không nạp cả file mà mở một stream. Vòng lặp và bài hát nhạc nền thường
dùng cách này.

| Hàm | Việc làm |
|---|---|
| njin::music_play() | Phát **từ đầu**, kể cả khi đang tạm dừng hoặc đang phát |
| njin::music_stop() | Dừng và đưa về đầu bài |
| njin::music_pause() / njin::music_resume() | Tạm dừng và phát tiếp, **giữ nguyên vị trí** |
| njin::music_set_looping() | Bật hoặc tắt lặp. Mặc định là bật, có hiệu lực ngay giữa bài |
| njin::music_set_volume() / njin::music_set_muted() | Giống sound |

## Module âm thanh của engine

Có một module lõi tên `njin.audio` luôn được đăng ký sẵn. Mỗi frame, ở `phase_post_update`,
nó làm hai việc:

- **Cấp dữ liệu cho các stream nhạc.** Nhạc giải mã từng đoạn nhỏ và sẽ im lặng ngay sau
  đoạn đầu nếu không có việc này.
- **Phát lại các sound lặp** đã kết thúc. Đó là lý do njin::sound_play_loop() chỉ cần gọi một
  lần.

Bạn không cần gọi gì để bật nó. Xem @ref architecture để biết module lõi hoạt động thế nào.

@note Vì sound lặp được phát lại **sau khi** nó kết thúc, chỗ nối có một quãng lặng cỡ một
frame. Với tiếng nền ngắn thì không đáng kể. Với nhạc nền dài, dùng njin::music_load().

## Giải phóng

Tài nguyên được giải phóng tự động khi gọi njin::njin_destroy(). Chỉ gọi njin::sound_unload()
hoặc njin::music_unload() khi muốn giải phóng sớm. Thiết bị âm thanh được mở cùng cửa sổ và
đóng sau khi mọi tài nguyên đã được giải phóng.
