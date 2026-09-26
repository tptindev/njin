# Hộp thoại và đa ngôn ngữ {#dialog}

@include dialog_i18n.cpp

## Đa ngôn ngữ

Mỗi ngôn ngữ là một file JSON. Object lồng nhau được nối tên bằng dấu chấm:

@code{.json}
{ "_name": "Tiếng Việt",
  "menu": { "play": "Chơi", "quit": "Thoát" },
  "hud": { "coins": "Vàng: {0}/{1}" } }
@endcode

| Hàm | Việc làm |
|---|---|
| i18n_load() | Nạp một ngôn ngữ; nạp thêm file cùng ngôn ngữ thì ghi đè và bổ sung |
| i18n_set_language() | Đổi ngôn ngữ đang dùng (áp dụng ngay) |
| i18n_languages(), i18n_language_name() | Danh sách để hiện trong menu, dùng khóa `_name` |
| tr() | Chuỗi của một khóa |
| trf() | Như tr(), thay `{0}`, `{1}`... bằng tham số: mỗi ngôn ngữ tự đổi trật tự từ |

Thiếu một khóa trong ngôn ngữ đang dùng thì lấy ở **ngôn ngữ dự phòng** (mặc định là ngôn ngữ nạp đầu
tiên; đổi bằng i18n_set_fallback()); thiếu nữa thì hiện chính khóa và ghi log một lần, nên chữ
thiếu dịch hiện ra rõ ràng thay vì mất hẳn.

Font mặc định của engine chỉ có ASCII. Muốn tiếng Việt, nạp font bằng font_load() (đã có sẵn
các ký tự tiếng Việt) và đặt vào njin::ui_style::font và njin::dialog_style::font.

## Hộp thoại

Một kịch bản là một danh sách nút nối với nhau (njin::dialog_script). Viết bằng JSON:

@code{.json}
{ "start": "hi", "nodes": [
  { "id": "hi", "speaker": "@owl.name", "portrait": "owl", "text": "@owl.hello", "next": "ask" },
  { "id": "ask", "text": "@owl.ask",
    "choices": [ { "text": "@owl.yes", "next": "tip" },
                 { "text": "@owl.no", "event": "refused" } ] },
  { "id": "tip", "text": "@owl.tip", "event": "got_hint", "next": null } ] }
@endcode

- Chữ bắt đầu bằng `@` là khóa dịch (xem tr()); không thì là chữ thường.
- Một nút không có `next` (cũng không có lựa chọn) đi tiếp xuống nút dưới nó. `"next": null` là kết thúc.
- `event` gửi njin::dialog_event khi câu hiện ra (hoặc khi chọn); hội thoại xong gửi njin::dialog_ended.
- `"if": "điều_kiện"` trên nút hoặc lựa chọn: game quyết định bằng dialog_set_condition().

dialog_start() chạy một kịch bản; dialog_say() nói một câu lẻ (biển báo, đồ vật). Hộp thoại được
engine vẽ và điều khiển: chữ chạy từng ký tự (bấm một lần để hiện hết), mũi tên chọn, Enter / Space /
nút A / chuột trái để sang câu. Trong lúc mở, các phím đó **không tới game**; phím di chuyển thì
vẫn tới, nên kiểm tra dialog_active() hoặc bật `dialog_style::pause_game`.

Giao diện là njin::dialog_style: font, cỡ chữ, số dòng, tốc độ chữ, tiếng "blip", chân dung. Hộp,
thẻ tên và lựa chọn dùng njin::ui_look như UI, nên dùng được ảnh 9-slice và shader riêng.
Chân dung đăng ký bằng dialog_portrait().

Để ngắt một đoạn chữ dài ở chỗ khác, dùng text_wrap() và draw_text_wrapped().
