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

## Âm thanh 3D {#audio_3d}

Trong game 3D, tiếng phát ra từ một chỗ trong thế giới và được nghe từ một **tai nghe**
(njin::audio_listener3d). Engine tính lại cho từng tiếng, mỗi frame:

- **Âm lượng** theo khoảng cách từ tai tới nguồn, theo hướng loa (nếu có nón) và theo vật
  che giữa hai bên.
- **Trái phải** theo hướng của nguồn so với hướng tai đang nhìn.
- **Cao độ** theo hiệu ứng Doppler: nguồn và tai đi lại gần nhau thì tiếng cao lên, đi xa nhau
  thì trầm xuống (tiếng xe lao qua).

@include audio3d.cpp

### Tai nghe

Mặc định tai **đi theo camera** của lần njin::begin_3d() vẽ ra màn hình gần nhất: cùng vị trí,
cùng hướng nhìn, và vận tốc engine đo từ quãng camera đi mỗi frame. Game 3D thường không phải
gọi gì.

Khi tai không ở camera (góc nhìn thứ ba mà muốn nghe từ nhân vật), đặt tay bằng
njin::audio_set_listener3d(); từ lúc đó tai thôi theo camera, cho đến khi gọi
njin::audio_listener3d_follow_camera(). Lần begin_3d() vẽ vào render texture (model xoay
trong menu) không đổi tai.

### Phát

| Hàm | Hành vi |
|---|---|
| njin::sound_play3d() | Phát **một lần** tại một điểm, hoặc đi theo một entity có njin::transform3d |
| njin::sound_loop3d() | Phát **lặp** cho đến khi njin::voice3d_stop(): máy nổ, lửa, thác nước |

Cả hai trả về một njin::voice3d_handle cho riêng tiếng đó. Dùng nó để dời tiếng
(njin::voice3d_set_position(), njin::voice3d_attach()), đặt vận tốc cho Doppler
(njin::voice3d_set_velocity()), đổi cách nghe lúc đang phát (njin::voice3d_set_desc(): cao độ
theo vòng tua máy, âm lượng theo ga) và dừng nó. Tiếng phát xong thì handle tự hết hiệu lực;
ô của nó được dùng lại nhưng handle cũ **không bao giờ** trỏ nhầm sang tiếng mới.

Tiếng đi theo entity: entity bị hủy thì tiếng một lần phát nốt ở chỗ cuối cùng, còn tiếng lặp
dừng luôn. njin::sound_stop() cho một sound cũng dừng mọi tiếng 3D của sound đó.

Tối đa **64** tiếng 3D cùng lúc. Quá số đó thì tiếng một lần đã chạy lâu nhất bị cắt; tiếng lặp
không bao giờ bị cắt.

### Khoảng cách

njin::sound3d_desc cho biết tiếng nghe được tới đâu:

| Trường | Ý nghĩa |
|---|---|
| `min_distance` | Gần hơn thì nghe đủ to. Mặc định 1 |
| `max_distance` | Xa hơn thì im. Mặc định 40 |
| `rolloff` | Cách giảm ở giữa (bảng dưới) |
| `rolloff_factor` | Độ dốc: 2 là tắt nhanh hơn, 0.5 là vang xa hơn |

| `rolloff` | Âm lượng ở khoảng cách `d` | Hợp với |
|---|---|---|
| njin::rolloff_inverse | `min / (min + factor * (d - min))`: như ngoài đời | Mặc định, hầu hết mọi tiếng |
| njin::rolloff_linear | Giảm đều từ 1 đến 0 giữa `min_distance` và `max_distance` | Khi cần biết chắc tiếng nghe được tới đâu |
| njin::rolloff_exponential | `(d / min)^-factor`: tắt nhanh hơn | Tiếng nhỏ, chỉ nghe khi đứng sát |

Inverse và exponential tự chúng không bao giờ về 0, nên ở 10% cuối trước `max_distance` engine
cho chúng nhỏ dần về 0: tiếng đi ra khỏi tầm không bị tắt đột ngột.

### Trái phải, Doppler, nón

- `spread` (0..1) là độ lệch trái phải: 1 là tiếng bên phải nghe hẳn ở loa phải, 0 là luôn ở
  giữa. Gần tai hơn `min_distance` thì tiếng tự về giữa dần, để tiếng ngay trên đầu không nhảy
  từ loa này sang loa kia.
- `doppler` (0 là tắt, 1 là như ngoài đời). Vận tốc của tiếng mặc định do engine đo từ quãng nó
  đi mỗi frame, làm mượt trong khoảng 0,1 giây; có vận tốc thật (njin::body3d_velocity()) thì
  đưa vào bằng njin::voice3d_set_velocity(). Tốc độ âm thanh mặc định 343 đơn vị mỗi giây (một
  đơn vị là một mét), đổi bằng njin::audio_set_speed_of_sound().
- **Nón**: loa có hướng (còi xe, loa phóng thanh) đặt `cone_direction`. Trong góc `cone_inner`
  quanh hướng đó thì đủ to, ngoài `cone_outer` thì còn `cone_outer_volume`, ở giữa thì chuyển
  dần. `cone_direction` bằng 0 (mặc định) là phát đều mọi hướng.

### Vật che

Bật `occlusion` thì mỗi frame engine bắn một tia vật lý (njin::physics3d_raycast()) từ tai tới
nguồn. Tia chạm một body ở trước nguồn thì tiếng nhỏ lại còn `occlusion_volume`, chuyển mượt
trong khoảng 0,15 giây. Chỗ chạm cách nguồn trong `occlusion_margin` thì không tính: thân xe
không che tiếng máy của chính nó.

@note Vật che chỉ làm tiếng **nhỏ đi**, không làm tiếng **đục** đi (không lọc âm cao). raylib
trộn từng tiếng bằng âm lượng, trái phải và cao độ, không có bộ lọc riêng cho mỗi tiếng.

### Âm lượng cuối cùng

Âm lượng của một tiếng 3D là tích của: âm lượng riêng của sound (njin::sound_set_volume()),
kênh của nó và `bus_master` (njin::audio_set_bus_volume(), xem @ref settings), `volume` trong njin::sound3d_desc,
khoảng cách, nón và vật che. Đổi âm lượng kênh trong menu cài đặt thì tiếng 3D đang phát nhận
ngay ở frame sau.

njin::voice3d_state() trả về những gì engine vừa tính cho một tiếng (âm lượng, trái phải, cao
độ, khoảng cách, có bị che không), để debug hoặc vẽ chỉ báo tiếng động trên màn hình.

## Module âm thanh của engine

Có một module lõi tên `njin.audio` luôn được đăng ký sẵn. Mỗi frame, ở `phase_post_update`,
nó làm ba việc:

- **Cấp dữ liệu cho các stream nhạc.** Nhạc giải mã từng đoạn nhỏ và sẽ im lặng ngay sau
  đoạn đầu nếu không có việc này.
- **Phát lại các sound lặp** đã kết thúc. Đó là lý do njin::sound_play_loop() chỉ cần gọi một
  lần.
- **Cập nhật các tiếng 3D**: dời tai theo camera, dời tiếng theo entity, tính lại âm lượng,
  trái phải và cao độ, phát lại tiếng 3D lặp và giải phóng tiếng một lần đã phát xong.

Bạn không cần gọi gì để bật nó. Xem @ref architecture để biết module lõi hoạt động thế nào.

@note Vì sound lặp được phát lại **sau khi** nó kết thúc, chỗ nối có một quãng lặng cỡ một
frame. Với tiếng nền ngắn thì không đáng kể. Với nhạc nền dài, dùng njin::music_load().

## Giải phóng

Tài nguyên được giải phóng tự động khi gọi njin::destroy(). Chỉ gọi njin::sound_unload()
hoặc njin::music_unload() khi muốn giải phóng sớm. Thiết bị âm thanh được mở cùng cửa sổ và
đóng sau khi mọi tài nguyên đã được giải phóng.
