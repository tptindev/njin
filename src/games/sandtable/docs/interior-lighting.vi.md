# Rule đèn trong nhà và camera

Rule chạy trực tiếp trong C++ nằm ở
`assets/models/procedural_building/retro/rules/interior_lighting.json`, được
liên kết qua `building_rules.json.runtime_rule_packs.interior_lighting`.
Sửa rule rồi khởi động lại game để tải lại; không cần export GLB.

Mỗi phòng nhận một profile theo `room.type`: phòng khách và ngủ dùng ánh
sáng ấm, bếp và văn phòng sáng trung tính, phòng tắm hơi lạnh; shop sáng
trong giờ hoạt động. Profile quy định RGB, cường độ, radius, giờ bật/tắt
và mức sáng ban ngày. Giờ kết thúc nhỏ hơn giờ bắt đầu chạy qua nửa đêm.
Phòng ngủ mặc định bật 18–23h, phòng khách 17–24h, bếp 5–23h, shop 7–23h.
Đây là lịch thiết kế cơ bản, chưa phụ thuộc vào người đang ở trong phòng.

Vị trí đèn được sinh từ polygon phòng, theo thứ tự ổn định. Một đèn cho
mỗi 12 m², tối đa 4 đèn/phòng; cao 2.7 m so với sàn, cách tường ít nhất
0.25 m và không nằm trên ô thông tầng. Đèn đầu gần tâm bounding box của
phòng, các đèn tiếp theo phân tán xa nhau. Radius được giữ đủ lớn để ánh
sáng từ trần tới sàn. Dữ liệu nằm trong `building3d.lights` gồm room ID,
tầng, vị trí local, màu, cường độ, radius và lịch hoạt động.

`view_options.camera` phân biệt ba mode:

| Mode | Kính | Hình học nội thất |
| --- | --- | --- |
| `observation` | Opaque, phản chiếu trời/điểm sáng mặt trời ban ngày, phát sáng khi tối | Lớp vỏ ngoài; đầy đủ khi cutaway |
| `third_person` | Trong ở gần, opaque phản chiếu ở xa; không emission | Nội thất gần camera |
| `live` | Như third person | Nội thất gần camera |

Camera live qua vai từng thành viên đi qua `view_draw_eye` và tự dùng mode
`live`. Mỗi pass đặt trạng thái kính trước khi vẽ; pass quan sát tiếp theo
khôi phục emission. Cánh chớp đóng vẫn che nội thất bằng hình học thật.
Mode third person có thể dùng cùng `view_draw_eye`, hoặc đặt
`view_options.camera = camera_mode::third_person` khi gọi renderer chính.

`glass_lod.clear_radius_m` mặc định **8 mét**, tính khoảng cách 3D từ vị
trí camera tới tâm bounds của module. Kính ngoài bán kính luôn opaque;
kính trong bán kính dùng `near_opacity` (mặc định 0.4). Tầng cao ngoài
bán kính cũng giữ phản chiếu. Mode quan sát luôn dùng kính phản chiếu.
Đây là phản chiếu bầu trời cách điệu bằng viền Fresnel màu trời và specular
của mặt trời; không phản chiếu hình ảnh các vật thể xung quanh bằng ray tracing/SSR.

Cùng một asset có thể xuất hiện trong và phản chiếu trong cùng một pass:
runtime chia các range instance và dùng hai model/material cache riêng.
Biến thể trong chỉ tải khi có instance ở gần; hình học biến thể này chiếm
thêm GPU memory nhưng không làm mọi kính trong cảnh vào transparency pass.

Nội thất được cache lần đầu camera tới gần: sàn, vách ngăn, cầu thang,
đồ đạc và đèn trần. Không bỏ mái hoặc cắt tường ở camera live. Housing
đèn và panel sáng dùng instancing; panel chỉ upload lại khi lịch/độ tối
đổi. Một view mặc định vẽ nội thất tối đa 8 nhà có footprint trong bán kính
kính cộng 1 mét; đồng thời giữ trần giới hạn 140 world units.
Nhà cần có plan procedural đầy đủ; nhà fallback/NoFit vẫn dùng lớp vỏ.

Các fixture đều có lịch và panel sáng, nhưng view chọn tối đa 4 nguồn
sáng phòng gần camera để chiếu sáng thực; đèn đường giảm xuống tối đa 8
trong mode immersive. Các giới hạn trong `runtime_budget` được đọc từ
JSON và clamp: room lights 0–4, street lights 0–8, houses 1–16, reach
20–260 world units. Đèn phòng hiện không đổ bóng để giữ camera live nhẹ;
ánh sáng có thể lan qua vách gần nhau. Đây là ánh sáng trực tiếp với
ngân sách cố định, chưa bake lightmap/GI cho mọi phòng.

`--pbk-test` kiểm tra lịch, vị trí, tính lặp lại và chuyển trạng thái kính
giữa camera. `--pbk-city-test` render thêm ảnh
`sandtable_pbk_city_interior_live.png` từ camera ngang tầm mắt nhìn qua kính.
`sandtable_pbk_city_glass_far.png` chụp cùng cửa ở xa để kiểm tra kính opaque.
