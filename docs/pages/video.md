# Video {#video}

Trang này phát video trong game: cutscene đầu màn, đoạn phim kết thúc, màn hình TV hay bảng quảng cáo trong thế
giới 3D. Mọi thứ khai báo trong `njin_video.h`; giải mã bằng pl_mpeg, nằm trong engine.

Cần biết trước: @ref rendering (texture) và @ref audio (kênh âm lượng).

## Chỉ MPEG-1

njin chỉ đọc **MPEG-1**: file `.mpg` có hình MPEG-1 và tiếng MP2. Đổi một video khác (mp4, mov, webm...) sang dạng
này bằng ffmpeg:

```sh
ffmpeg -i in.mp4 -c:v mpeg1video -q:v 4 -c:a mp2 -b:a 192k -f mpeg out.mpg
```

`-q:v` từ 2 (đẹp nhất, file lớn) đến 31; 4 là cân bằng. Thêm `-vf scale=1280:-2` để thu nhỏ hình, `-an` để bỏ
tiếng. MPEG-1 cũ nhưng giải mã rất nhẹ, không cần thư viện ngoài, và không vướng bản quyền codec.

## Mở và phát

```cpp
const njin::video_handle intro = njin::video_open(ctx, {.path = "videos/intro.mpg"});
// mỗi frame, trong phase_render:
njin::video_draw_fit(ctx, intro, {{0, 0}, njin::window_size(ctx)});
if (njin::video_finished(ctx, intro)) { /* vào game */ }
```

video_open() mở và phát ngay (đặt `play = false` để mở rồi đứng ở hình đầu). Engine giải mã video trong
`phase_post_update` theo **thời gian thật**: không theo time_set_scale(), vẫn chạy khi game pause, nên cutscene
không chậm theo slow motion. Hình và tiếng khớp nhau.

| Hàm | Làm gì |
|---|---|
| video_play(), video_pause() | Phát tiếp, tạm dừng. Phát một video đã hết thì phát lại từ đầu |
| video_seek() | Nhảy tới một giây (hình hiện là frame gần nhất trước chỗ đó) |
| video_set_loop(), video_set_volume() | Lặp, âm lượng riêng |
| video_time(), video_duration(), video_finished() | Đang ở đâu, dài bao lâu, đã hết chưa |
| video_size(), video_framerate(), video_has_audio() | Cỡ hình, frame mỗi giây, có tiếng không |
| video_close() | Đóng, giải phóng hình và tiếng |

Tiếng đi qua kênh `video_desc::bus` (mặc định `bus_music`) và `bus_master`, nên thanh âm lượng trong menu cài
đặt áp cho nó như mọi âm thanh khác.

## Vẽ

- **2D:** video_draw() co giãn hình vào một hình chữ nhật; video_draw_fit() đặt vừa khít một vùng, giữ tỉ lệ, tô
  dải thừa (dải đen của cutscene).
- **3D:** video_texture() là texture chứa frame hiện tại, dùng như mọi texture: dán lên hình khối qua
  `material3d::texture` (bật `unlit` để màn hình không bị tối theo ánh sáng), hay lên model qua `model_material`.
  Texture thuộc về video: đừng texture_unload() nó.

## Ví dụ đầy đủ

Cutscene toàn màn hình, Enter để bỏ qua; xong thì vào một căn phòng có TV phát một video lặp.

@include video.cpp

## Giới hạn

- Chỉ MPEG-1/MP2. Không đọc được mp4, webm hay mkv trực tiếp: đổi bằng ffmpeg như trên.
- Giải mã trên CPU: video 720p chạy tốt, 1080p 60 frame/giây tốn một nhân CPU đáng kể trên máy yếu.
- Tua (video_seek()) dừng ở keyframe gần nhất trước chỗ cần, nên có thể sớm hơn vài phần mười giây.
- Không có phụ đề: vẽ chữ theo video_time() bằng UI.
