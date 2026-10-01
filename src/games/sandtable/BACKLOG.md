# Sa Bàn Chiến Trận: backlog

Việc đã nhận ra là cần làm nhưng chưa làm, theo CONCEPT.md. Xoá một mục khi
làm xong; thêm mục mới khi nhận ra việc còn thiếu, không cần đợi dọn file.

## Nhịp thời gian

- Đàn em đi bộ bằng tốc độ thật trong khi đồng hồ chạy 60 giây một giờ game:
  một chuyến giữa hai trụ sở vẫn mất nhiều giờ game. Dân thường đã có
  `unseen_boost` (đi nhanh gấp 4 khi ngoài tầm camera); đàn em thì chưa, vì
  làm vậy sẽ không công bằng cho người chơi (nhanh hay chậm phụ thuộc chỗ
  camera đang nhìn, FOW thì không).
  - Hướng đã chọn: xe máy cho đàn em đi quãng xa, trên lưới đường xe có sẵn
    (`city::city_map::car`). Hợp bối cảnh Việt Nam, và là tính năng của
    game chứ không phải engine.

## Kit nhà và đạo cụ đất sét

- Rule clay (`procedural_building/clay/rules/building_rules.json`) thiếu 8
  archetype (tube_house, tube_shop_house, apartment_block, hotel, school,
  workshop_hall, warehouse_hall, market_hall) và 4 loại phòng (classroom,
  hall, hotel_room, market_floor). `pbk::load_rules` đang lấy chúng từ rule
  của kit cũ (`legacy_rules_path`). Phải chép sang rule clay trước khi xoá
  kit cũ, rồi bỏ đường dẫn đó.
- Batch instance (`draw_instanced3d` với model) không dùng normal map và ORM,
  nên vân hạt đất sét không hiện trên tường trong phố. Chỉ cánh cửa và cánh
  chớp (vẽ bằng `draw_model`) là có.
- Cánh chớp chưa theo lịch trong `clay/rules/shutter_behavior.json` (xác suất
  mở theo giờ, thời gian giữ tối thiểu, giới hạn số lần mỗi ngày, ngân sách
  chuyển động trên màn hình). Hiện mỗi cánh đóng/mở theo seed lúc dựng nhà
  và chỉ đổi khi gọi `pbk::shutter_toggle`.
- `pbk_assemble` chưa đặt `CornerWindow90` vào nhà sinh tự động vì rule chưa
  nói đặt ở đâu; module này mới chỉ được thử trong cảnh 4 của `--pbk-test`.
- Đạo cụ street_clay chưa có trong phố: trụ cứu hoả, thùng rác, ô dù, gánh
  trà và các cụm quán (`*Space`). Chưa dùng `distribution_rules.json` (xác
  suất theo khu, khoảng cách slot) để rải chúng; mới chỉ thay đèn, cột điện,
  ghế đá, ghế nhựa và xe hàng có sẵn.
- raylib in cảnh báo "can only load one skin" cho `CornerWindow90` (vô hại:
  cánh chớp do game tự lấy mẫu rig).

## Kinh tế và đàn em

- Đàn em chưa thật sự đi ăn hay về phòng trọ trên bản đồ: tiền ăn và tiền
  nhà (`living_cost`) hiện chỉ trừ thẳng vào ví mỗi ngày, không gắn với một
  chuyến đi hay một chỗ cụ thể.
- Vốn khởi đầu (1.500k) chỉ đủ trả lương khoảng 2 ngày nếu không đi thu ngay;
  cân nhắc tăng vốn hoặc giảm lương khởi điểm để phần mở đầu nhẹ nhàng hơn.
- Dân số 800 người: một lần chạy `--test` thấy 1 người đứng chồng lên người
  khác lúc chụp ảnh (các lần trước là 0) — xem lại ngưỡng va chạm hay mật độ.

## Băng đối thủ (gang_ai)

- Chưa biết tuần tra (chỉ biết thu, bành trướng, đánh mối).
- Chưa "phản ứng của đối phương" theo CONCEPT.md: khi người chơi gây hấn đủ
  nhiều, băng bị gây hấn phải tập hợp đàn em và kéo đến đối đầu. Hiện tại
  phần phản ứng duy nhất là một shop tự ngừng trả nếu có người canh
  (`gang_guards`), không có phản công chủ động.
- Khi bị nợ lương/bất mãn, các băng đối thủ cũng tự thu hẹp như người chơi
  (đã có cơ chế bỏ việc dùng chung); chưa thấy việc này xảy ra thật trong
  một lần chạy dài, cần theo dõi thêm khi chúng gặp khó khăn tài chính.

## Trận đối đầu (combat encounter) — CONCEPT.md

Toàn bộ phần này của concept chưa có code:

- Chuyển cảnh sang một trận có thời gian và mục tiêu cụ thể khi hai băng
  đối đầu (không phải đánh tới người cuối cùng).
- Sức chiến đấu (combat capacity): stamina, thương tích, morale, knockdown,
  bỏ chạy, bị khống chế — khác với máu/mệt/tinh thần hiện có của
  `lackey`, cần thiết kế riêng cho combat.
- Điều kiện thắng theo từng trận (thu đủ tiền, hạ tỉ lệ địch, giữ vị trí,
  giữ sức chiến đấu, rút trước viện binh...).

## Nhập vai Boss — CONCEPT.md

- Boss hiện chỉ là một `lackey` với `rank::boss`, không có nhân vật hay góc
  nhìn riêng trên bản đồ.
- Chưa có: nhập/thoát vai lúc nào cũng được, camera chuyển mượt giữa sa bàn
  và góc nhìn thứ ba sau lưng Boss, điều khiển trực tiếp.
- Chưa có: khi không nhập vai, Boss tự làm bot (đi theo lệnh, tự đánh khi
  gặp combat) — cần chờ hệ thống combat ở trên trước.

## Tranh giành và phản ứng địa bàn

- "Tranh giành địa bàn" hiện chỉ là tỉ lệ cơ sở trả tiền trong một khối
  (`update_turf`), không có trận đối đầu nào thật sự thay đổi quyền kiểm
  soát — phụ thuộc hệ thống combat ở trên.
- Chưa có việc băng thứ ba lợi dụng lúc người chơi kéo gần hết đàn em đi
  đánh chỗ khác để tiến vào địa bàn đang bỏ trống.
