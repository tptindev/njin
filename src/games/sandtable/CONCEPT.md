# Sa Bàn Chiến Trận: concept

Người chơi là **đại ca** của một băng đảng. Không tự tay đánh nhau: đại ca
quản lý đàn em, giao việc cho từng người và nhìn cả vùng từ trên xuống như
một sa bàn. Game kết hợp **sim** (quản lý người, tiền, quan hệ) và **RTS**
(điều quân trên bản đồ, theo thời gian thực).

Góc nhìn: 3D, nhìn từ trên xuống (camera RTS: kéo, xoay, zoom), người là
tượng đất sét.

## Vòng chơi

- **Đàn em**: mỗi người là một cá thể, có tên và chỉ số riêng. Đại ca tuyển
  người, giữ người, giao việc.
- **Thu tiền bảo kê**: giao đàn em đi thu tiền ở các cơ sở trong địa bàn.
  Đây là nguồn thu chính.
- **Giữ địa bàn**: băng khác và các thế lực khác sẽ đến lấn; phải cắt người
  canh giữ.
- **Tranh giành địa bàn**: đem người sang đánh chiếm địa bàn của băng khác.
- **Bành trướng**: địa bàn rộng thì thu nhiều hơn, nhưng cũng phải giữ nhiều
  chỗ hơn.
- **Phi vụ**: các việc lớn có hệ thống (không phải kịch bản viết tay),
  giao cho một nhóm đàn em làm.

## Đã có (phần hình và tiếng)

- `view.*`: bàn phẳng, camera RTS, đổi qua lại giữa màn hình và bàn.
- `render.*`: bàn cát, khung gỗ, ánh sáng theo giờ, skin HUD.
- `weather.*`: màu trời theo giờ. `audio.*`: tiếng động.

## Thành phố (sinh theo seed)

`city/`: bộ sinh thành phố kiểu Việt Nam, cùng seed ra cùng một thành phố.
Sông, hồ, đại lộ, bùng binh, phố, hẻm; khối (đơn vị địa bàn) gom thành quận
(phố cổ, chợ, xóm lao động, phố đêm, bến cảng, xưởng, khu mới); nhà ống,
chợ, kho, chùa, trường; cơ sở kinh doanh có thu nhập và tiền bảo kê; bãi
trống, bãi xe, sân bóng, cầu, cuối hẻm; nav grid cho người đi bộ và cho xe;
các chỗ có thể đặt trụ sở băng.

- `city/city.h`: dữ liệu và API (gameplay đọc từ đây). `gen_*.cpp`: mỗi file
  một nhóm bước sinh. `check.cpp`: kiểm tra. `render_*.cpp`: phần vẽ.
- `world.*`: thành phố đang chơi. Phím: N/B seed sau/trước, R seed ngẫu
  nhiên, F1 lớp phủ (quận, khối, đi bộ, xe), F2 nhãn, F3 đồ thị đường,
  F4 ghim. Rê chuột để xem khối, nhà, cơ sở. Nhấp chuột vào một nhà để mở
  nó ra (bỏ mái và các tầng trên, tường tầng trệt cắt thấp), Esc để thôi;
  C mở mọi nhà quanh tâm màn hình khi nhìn gần (`city/render_cutaway.cpp`).
- `person.*`: người là model có xương của Universal Animation Library
  (Quaternius, CC0, `assets/models/person.glb`), cao 13 đơn vị (khoảng
  1,75 m; một tầng nhà 18). `crowd.*`: người qua đường đi trên vỉa hè và
  hẻm bằng nav grid, khách ngồi ghế nhựa trước quán.
- `--seed N`, `--citycheck [seed đầu] [số seed]` (kiểm tra hàng loạt, không
  mở cửa sổ), `--citymap <seed> <file.ppm>` (ảnh raster một pixel một ô).

Gameplay đã bị xóa; sẽ làm lại từ concept này, trên thành phố ở trên.
