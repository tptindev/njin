# Chọn và phân bố 25 đạo cụ

`distribution_rules.json` là rule chính; `asset_manifest.json` cung cấp kích thước,
polygon clearance, service/work anchors, vị trí ngồi, đèn và socket dây điện.
`sidewalk_layout.json` vẫn là cảnh mẫu, không phải danh sách placement bắt buộc.
Các khoảng cách là mặc định gameplay, có thể tinh chỉnh theo nhân vật/camera.

## Trình tự áp dụng trong game

1. Tạo ID ổn định cho đoạn phố và các slot. Chọn profile residential, commercial,
   market hoặc park từ zoning. Dùng mét trong toàn bộ phép thử placement.
2. Chia mặt vỉa hè thành dải sát lề, lối đi liên tục rộng tối thiểu 1,6 m và
   dải sát nhà. Bề rộng dải phải phù hợp footprint thật; không đủ chỗ thì bỏ món.
   Cấm đặt trong vùng góc giao lộ/crossing, lối cửa và khoảng quét cánh cửa.
3. Xử lý nhóm theo thứ tự hydrant → pole → lamp → vendor → bin → bench.
   Mỗi nhóm có spacing riêng. Sinh slot theo spacing; jitter vị trí tối đa
   ±20% spacing. Thử tối đa 8 vị trí cho mỗi slot. RNG dùng key cố định trong
   rule; thứ tự camera/frame không được thay đổi kết quả.
4. Với mỗi slot, thử xác suất spawn của profile rồi chọn asset theo trọng số.
   Tạo vendor slot chung: chọn complete_cluster hoặc decomposed một lần,
   không sinh cả hai. Các ghế nhựa, đôn, bàn, dù và bộ trà chỉ là child trong
   composition; không rải chúng độc lập ngẫu nhiên khắp vỉa hè.
5. Với mode decomposed, chọn count trong range nguyên đóng, chọn ghế/đôn theo
   high_seat hoặc low_seat. Dùng service/work anchor để đặt các child. Ghế thấp
   đi với bàn thấp; quầy hủ tiếu/mì Quảng không tự dùng bàn thấp. Xoay ghế về
   mặt phục vụ/bàn. Bánh mì có thể không cần ghế; trà đá bắt buộc bàn + bộ trà.
6. Tính footprint của toàn group bằng bounds và clearance_polygon, thêm vùng
   thao tác/khách. Chiếu polygon/bounds đã xoay vào mặt vỉa hè. Kiểm tra containment,
   overlap, khoảng cách, cửa, lối đi, headroom và mật độ trước khi commit.
   Chỉ dùng AABB để loại sớm; polygon/navmesh là kiểm tra cuối cùng trong game.
7. Vendor cách nhau ít nhất 8 m, tối đa 5 cụm/100 m và chiếm tối đa 35% mặt tiền.
   Tổng ground coverage tối đa 35%. Thùng rác cách điểm phục vụ đồ ăn ít nhất
   2 m; trụ chữa cháy giữ khoảng tiếp cận 0,65 m. Kiểm tra cả vùng đã reserved,
   không chỉ tâm model. Dù có thể che vùng ghế riêng, nhưng không lấn lối đi
   nếu thiếu headroom 2,3 m.
8. Nếu group không vừa, giảm child tùy chọn trước; child bắt buộc vẫn không
   vừa thì bỏ cả group. Không thu nhỏ mesh hoặc lối đi để ép placement.
   Lưu ID, mode, pose và child list; LOD/culling không được sinh lại RNG.

Asset nguồn và các anchors/bounds trong manifest street dùng +Z lên, mặt trước
-Y. GLB đã chuyển sang +Y lên, mặt trước +Z. Chuyển metadata bằng `[x,z,-y]`
đúng một lần trước khi so với mesh, hoặc làm toàn bộ placement trong trục
authoring rồi chuyển placement sang renderer. Không dùng bounds authoring
như bounds glTF trực tiếp.

`tools/retro_rule_reference.py` có sampler chọn slot và phép thử rectangle đơn
giản để tham khảo khi port C++; không thay solver polygon của game.
`runtime_rules_validation.json` kiểm tra đủ 25 ID và các trường hợp loại bỏ.

## Cánh chớp theo giờ game

Rule tại `../procedural_building/retro/rules/shutter_behavior.json`.

| Giờ game | Xác suất trạng thái mở khi được xét | Cơ hội xét mỗi giờ game |
|---|---:|---:|
| 00–06 | 8% | 0,005 |
| 06–09 | 75% | 0,12 |
| 09–17 | 85% | 0,025 |
| 17–21 | 25% | 0,10 |
| 21–24 | 10% | 0,005 |

Các xác suất trên chỉ áp dụng khi có sự kiện xét. Không ép mọi cửa mở lúc 6h
hoặc đóng lúc 21h, không roll mỗi frame và không bật/tắt theo vòng lặp animation.
Chọn pose ban đầu theo giờ hiện tại một lần. Lịch sự kiện dùng clock game tuyệt
đối, tăng liên tục; không dùng giờ máy. Mỗi rig/window có key riêng; hai cánh
cùng rig đổi cùng nhau, còn hai rig của cửa góc có lịch độc lập.

Scheduler tạo ứng viên bằng exponential interval ở rate 0,12/giờ, rồi nhận ứng
viên theo rate của khung giờ ứng viên. Khi nhận, roll target OPEN/CLOSED; target
trùng trạng thái thì không làm gì. Đổi trạng thái thật chỉ được phép sau ít nhất
8 giờ game kể từ lần đổi trước và tối đa 2 lần/ngày game. Đừng reset state mỗi
ngày; chỉ reset counter ngày. Bình quân mô phỏng 1.000 cửa trong 30 ngày là
0,444 lần đổi/cửa/ngày, tức khoảng một lần mỗi 2,25 ngày game.

Lưu state/event_index/next_candidate cùng save game. Khi load/time skip, chạy
logic theo timestamp rồi áp pose cuối cho cửa offscreen; không phát lại chuỗi
animation cũ. Xử lý backlog bằng task có budget, không chạy vòng lặp catch-up
lớn trong một frame. Khi quay ngược clock phải restore state từ cùng save.

GLB có clip `Shutter_CloseOpen`, không chứa hai clip riêng Open/Close:

- Đóng: đọc đoạn t=0 đến 23/24 giây trong clip.
- Mở: đọc đoạn t=47/24 đến 71/24 giây trong clip.
- Pose mở: t=0; pose đóng: t=23/24. Phát mỗi transition trong 1,25 giây thực,
  giữ pose ở cuối; không loop. Giữ dấu xoay đối xứng của hai bone.
- Cửa gần/visible mới animate, tối đa 4 animation bắt đầu/phút thực trong
  một neighborhood; pending target được gộp. Cửa offscreen chỉ đổi pose.
- Nếu cánh bị vật cản trong vùng quét, hoãn đổi trạng thái. Tương tác người
  chơi override scheduler nhưng vẫn ghi last transition và counter ngày.

Chưa nối các rule này vào C++ Sandtable. Claude cần implement sampler/solver,
scheduler, persistence, animation budget và collision theo hợp đồng trên.
