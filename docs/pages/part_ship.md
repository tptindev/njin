# Phần 6: Hoàn thiện và phát hành {#part_ship}

**Mức: trung cấp.** Cần biết trước: game đã chơi được (đã làm xong @ref part_world), và nên có menu từ @ref ui. Phần
này biến một màn chơi thành một game: có menu chính, tạm dừng, lưu tiến trình, cài đặt của người chơi và bản phát hành.

## Đi theo thứ tự này

| Thứ tự | Trang | Bạn được gì |
|---|---|---|
| 1 | @subpage scenes | Chia game thành menu, đang chơi, tạm dừng, game over; chuyển cảnh mờ dần |
| 2 | @subpage window_files | Cửa sổ, toàn màn hình, chụp màn hình, đường dẫn tài nguyên, thư mục lưu game |
| 3 | @subpage json | Đọc, ghi JSON: lưu game và file cấu hình |
| 4 | @subpage settings | Âm lượng từng kênh, đổi phím, rung tay cầm, lưu cài đặt của người chơi |
| 5 | @subpage samples | Đọc game mẫu đầy đủ và đóng gói bản phát hành |

@ref samples nên đọc như một **bài kiểm tra cuối**: game mẫu dùng gần như mọi trang trước đó.

## Đọc xong thì làm gì

@ref part_advanced nếu game cần hiệu ứng đồ họa, bản đồ tự sinh hoặc cần đo hiệu năng.
