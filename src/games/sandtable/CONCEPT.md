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
  Bên trong: `city/interior.cpp` chia nhà thành phòng trên lưới ô vuông
  (nhà ống thành các phòng nối tiếp trước-sau, có lối đi dọc một bên và mọi
  cửa nằm trên lối đi đó, cầu thang sát bên kia; chung cư/khách sạn/trường
  thành hành lang giữa, phòng hai bên; kho/xưởng/chợ/chùa thành sảnh mở,
  kho/xưởng có thêm văn phòng góc). `city/interior_furnish.cpp` kê đồ theo
  từng loại phòng: lưng sát tường, mặt vào phòng, không chồng nhau, không
  chắn cửa và lối đi; đồ nhỏ (đèn, hàng hoá) đặt trên bàn, kệ. Tường cắt
  thấp có nẹp tối ở mặt cắt. Chọn một nhà thì các nhà che nó phía camera
  cũng được mở.
- Nhà nhiều tầng: PgUp/PgDn (hoặc `]` `[`) đổi tầng đang xem; các tầng
  dưới hiện thành khối đặc, camera nâng lên theo. Mỗi tầng một bố cục
  (`build_interior(b, floor)`): tầng trệt cửa hàng/phòng khách và bếp, tầng
  giữa phòng ngủ và nhà tắm có cửa ra ban công, tầng trên cùng có phòng thờ;
  cầu thang cùng một chỗ ở mọi tầng.
- Hiệu suất (`city/render_lod.*`): bản đồ chia ô 100×100; nhà và đồ vật xếp
  theo ô nên mỗi ô là một đoạn liền trong batch instance. Mỗi frame:
  frustum culling (góc màn hình chiếu xuống mặt đất và độ cao mái), LOD (cửa
  sổ, ban công, biển hiệu, xe máy, ghế chỉ vẽ ở gần; nhìn xa chỉ còn thân
  và mái). Khi tập trung vào một thứ (`view_options::focused`, `focus`,
  `focus_radius`; hiện là nhà đang chọn), chi tiết chỉ giữ quanh nó, phần
  gần hơn và xa hơn mờ đi thật (độ sâu trường ảnh, `post_fx::dof`, lấy nét
  vào nhà đang chọn) cùng một lớp sương nhẹ (fx3d) và viền tối đậm hơn;
  người qua đường ngoài khung hoặc xa tâm không vẽ. Mặt đất, đường, nước
  chia thành model theo ô 400×400 (`mesh_set`), engine bỏ ô ngoài tầm
  nhìn. HUD ghi số ô vẽ, ô chi tiết, số khối.
- Mặt ngoài: `city/render_facade.*` là các chi tiết mặt tiền (cửa sổ khung
  kính, ban công lan can, chậu cây, quần áo phơi, máy lạnh, cửa cuốn, gờ
  tầng, tường chắn mái, mái ngói dốc); `city/render_buildings.cpp` ghép
  chúng theo loại nhà: nhà ống thân bê tông xám, mặt tiền sơn màu.
  Ghép từ `assets/models/interior/` (một phần "PSX modular house interior
  pack" — xem `SOURCE.txt` trong đó, gói gốc chưa có giấy phép, cần hỏi lại
  trước khi chia sẻ ra ngoài máy này).
- `person.*`: người là model có xương của Universal Animation Library
  (Quaternius, CC0, `assets/models/person.glb`), cao 13 đơn vị (khoảng
  1,75 m; một tầng nhà 18). `crowd.*`: người qua đường đi trên vỉa hè và
  hẻm bằng nav grid, khách ngồi ghế nhựa trước quán.
- `--seed N`, `--citycheck [seed đầu] [số seed]` (kiểm tra hàng loạt, không
  mở cửa sổ), `--citymap <seed> <file.ppm>` (ảnh raster một pixel một ô).

Gameplay đã bị xóa; sẽ làm lại từ concept này, trên thành phố ở trên.
