# Quy hoạch và diện mạo nhà

Nhà mặt đường thử các chiều rộng/chiều sâu theo lưới 2 m trước khi bỏ lô.
Khu ở cho phép nhà ống sâu 14 m; khu thương mại giữ chiều sâu lớn hơn để
đủ cửa hàng, phòng ở và cầu thang. Khoảng lùi trước nhà 1,4 m dành cho
ban công 1,2 m, tránh đặt model ra lòng đường.

Quy hoạch dành trước một số lô lớn trong khu dân cư/khu đô thị mới cho
nhà chữ L và chữ T. Mẫu này dùng cùng module thẳng của kit, footprint có
góc lõm, mái và sàn theo footprint; các phòng, cửa và core vẫn qua kiểm tra
diện tích, ánh sáng và đường đi. Lô giáp nhiều đường chọn nhà góc, nhà góc
bo tròn hoặc mẫu ba mặt phố khi đủ kích thước. Lô nhỏ dùng nhà ống.

Lượt lấp đất cuối chỉ dùng khoảng trống chứa được nhà 10 x 10 m cùng
lối đi quanh nhà; giữ sân công nghiệp, bãi cảng, công viên và đường.
Các lô lớn được ưu tiên trước, không rải nhà ngẫu nhiên lên đất thừa.

Diện mạo đọc `retro/rules/building_rules.json`:

- Cửa sổ được thiết kế trong plan theo nhu cầu lấy sáng từng phòng;
  renderer không tự thêm cửa ngẫu nhiên. Luật game đặt diện
  tích kính tối thiểu bằng 5% diện tích phòng; ưu tiên các bay luân phiên
  để mặt tiền có nhịp tường kín/cửa thay vì cả dãy cửa liền nhau.
- Ban công: xác suất 0,55–0,80 chọn cho cả nhà, tối đa một bay trên mặt trước mỗi tầng
  trên, chỉ vào phòng ở/phòng ngủ và khi khoảng nhô ra đủ trống. Dùng
  module Balcony có cửa lớn sát sàn, slab và lan can. Ban công ghi vào
  aperture/module của plan, cùng dữ liệu cho render và ánh sáng.
- Thay cửa sổ bằng cửa ban công có diện tích kính lớn hơn có thể bỏ bớt
  cửa sổ khác trong phòng, nhưng không hạ mức lấy sáng tối thiểu.
- Instancing, chunk culling và LOD hiện có được giữ. LOD xa lấy hình khối
  từ phân rã slab mái nên vẫn giữ đường bao L/T và góc bo tròn.
- Collider các đoạn tường thẳng đồng phẳng được gộp bằng hợp hộp chính
  xác; không gộp xuyên qua lỗ cửa. Không tăng giới hạn vật lý của engine.

Kiểm tra: `--citycheck 1 5`, `--pbkcheck 10`, `--pbkcheck 100001` (đếm
toàn map seed 1), `--pbk-city-test` (ảnh toàn khu, góc gần, nhà cánh và
nhà góc). Script `tools/make_building_rules.py` chứa cùng cấu hình để
tạo lại rule; không đổi model hay tự export GLB.

Courtyard/U chưa có layout nội thất hỗ trợ; các mẫu này vẫn trả NoFit.
Cửa của module Balcony là bộ phận tĩnh của asset, chưa có animation riêng.

## Lưới thiết kế mặt tiền, phân cấp và nhịp

Lưới xây dựng vẫn là 2 m để ghép đúng asset, collider và phòng. Lưới thiết
kế đặt trục cửa theo nhóm bay phản chiếu từ hai đầu mặt đứng: trên mặt
đứng rộng, bay góc ưu tiên là tường kín; các trục tiếp theo cách nhau một
bay đặc, cặp giữa có thể liền nhau thành một nhóm. Mặt tiền hẹp 2–3 bay
không bị ép để trống góc vì sẽ mất cửa/lấy sáng cần thiết.

Các tầng dùng cùng trục thiết kế, ưu tiên cửa nằm trên cửa tầng dưới.
Ưu tiên trục chính ở cả hông/rear được mở cửa trước khi thêm bay phụ
ở mặt tiền. Lấy sáng và ranh giới phòng được ưu tiên nếu phải dùng thêm bay phụ;
không dịch cửa xuyên vách chỉ để tạo đối xứng. Ban công ưu tiên trục của
ban công tầng dưới, rồi trục thiết kế gần cửa chính. Các tầng đủ điều kiện
thuộc cùng một cụm ban công; không tung xác suất riêng cho từng tầng.

Phân cấp ngang: chân tường kín có đế 28 cm, gờ đầu tầng trệt mặt phố
14 cm; gờ tầng giữa chỉ 4,5 cm cùng màu tường; gờ kết thân nhà 18 cm
nhô 10 cm. Gờ chạy qua cả bay cửa chính, tránh bị đứt đoạn tại lối vào.
Hai đầu mặt đứng có trụ nhấn mảnh 12 cm, không lặp trụ trên mọi bay.
Parapet/coping giữ socket và hình học kit hiện có; đoạn cong dùng trim
của asset cong. Tầng trệt thương mại giữ chuỗi shopfront thành phần đế
mở, các tầng ở phía trên có nhịp đặc/rỗng nhẹ hơn.

`window_density` trong rule cũ được giữ để tương thích dữ liệu; không còn
được dùng để tạo cửa ngoài plan ở bước assemble. Không đổi tỷ lệ hoặc
co giãn model cửa, tránh lệch rig, kích thước kính và lỗ mở.
