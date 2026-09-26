# Ghi log {#logging}

njin có logger riêng, dùng qua các macro `NJIN_*` có định dạng như `printf`:

```cpp
NJIN_INFO("người chơi xuất hiện tại %.1f, %.1f", pos.x, pos.y);
NJIN_WARN("texture thiếu: %s", path);
NJIN_ERROR("mã lỗi %d", code);
```

Mỗi dòng in ra dạng:

```
[  1.234] WARN  file.cpp:42  texture thiếu: assets/player.png
```

gồm thời gian (giây), mức độ, file và dòng gọi, nội dung. Log của chính raylib
cũng đi qua logger này và được gắn nhãn `raylib`.

## Mức độ

Từ chi tiết nhất đến nghiêm trọng nhất (njin::log_level):

| Macro | Mức | Dùng khi |
|---|---|---|
| NJIN_TRACE | njin::log_trace | Lần theo lỗi chi tiết |
| NJIN_DEBUG | njin::log_debug | Thông tin gỡ lỗi |
| NJIN_INFO | njin::log_info | Sự kiện bình thường |
| NJIN_WARN | njin::log_warn | Có vấn đề nhưng vẫn chạy tiếp |
| NJIN_ERROR | njin::log_error | Một thao tác thất bại |
| NJIN_FATAL | njin::log_fatal | Không thể tiếp tục. **Dừng chương trình** |

## Lọc và đổi nơi nhận

- njin::log_set_level() bỏ qua mọi dòng thấp hơn một mức. Mặc định là
  `log_debug` ở bản debug và `log_info` khi định nghĩa `NDEBUG`.
- njin::log_set_sink() thay nơi nhận log (mặc định là stderr), ví dụ để ghi ra file.
- Khi njin_inspector đang kết nối vào game (xem @ref debug), log hiện ở inspector thay vì stderr.
  Chưa có inspector thì log ra stderr như bình thường. Nơi nhận do game đặt bằng
  njin::log_set_sink() vẫn nhận log như cũ.

@include logging.cpp

@note `file` là `nullptr` và `line` là `0` khi không biết nguồn gốc dòng log, ví dụ log của raylib.

Danh sách đầy đủ các hàm và macro nằm ở nhóm @ref grp_log.
