# Phần 5: Âm thanh và giao diện {#part_ui_audio}

**Mức: trung cấp.** Cần biết trước: @ref part_core, và @ref input cho phần UI. Phần này làm game "nghe được" và có
menu, hộp thoại. Các trang độc lập với nhau, đọc trang nào cần trước cũng được.

## Các trang

| Trang | Bạn được gì |
|---|---|
| @subpage audio | Sound cho hiệu ứng ngắn, music cho nhạc nền, kênh âm lượng |
| @subpage ui | Menu, nút, thanh trượt, popup, toast; dùng được với chuột, bàn phím và tay cầm |
| @subpage ui_editor | Dựng menu bằng kéo thả trong njin_ui_editor, lưu `.ui.json` và nạp vào game |
| @subpage dialog | Hộp thoại, chữ chạy từng ký tự, lựa chọn, chân dung; đa ngôn ngữ `tr`/`trf` |

Thứ tự gợi ý: @ref ui trước (game nào cũng cần menu), @ref audio, rồi @ref dialog nếu game có nhân vật nói chuyện.

## Đọc xong thì làm gì

@ref part_ship để chia game thành các màn, lưu tiến trình và đóng gói.
