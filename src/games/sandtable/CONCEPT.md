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
  F4 ghim. Rê chuột để xem khối, nhà, cơ sở: quận dưới chuột được tô sáng
  nhẹ, viền sáng quanh ranh giới và hiện tên lớn ngay trên bản đồ; nhà dưới
  chuột có khung sáng quanh nó (`city/render_hover.cpp`). Nhấp chuột vào một nhà để
  focus nó: chỉ nhà đó được mở ra (bỏ mái và các tầng trên, tường tầng trệt
  cắt thấp), camera quay ra phía mặt tiền, nhìn dốc từ trên xuống và zoom
  vào giữa nhà. Nhấp nhà khác thì chuyển focus sang nhà đó (mỗi lúc chỉ một
  nhà); nhấp ra ngoài hoặc Esc thì thôi focus và camera zoom ra lại chỗ cũ
  (`world_focus`, `world_unfocus`). C mở mọi nhà quanh tâm màn hình khi nhìn
  gần và không focus nhà nào (`city/render_cutaway.cpp`).
  Bên trong: `city/interior.cpp` chia nhà thành phòng trên lưới ô vuông
  (nhà ống thành các phòng nối tiếp trước-sau, có lối đi dọc một bên và mọi
  cửa nằm trên lối đi đó, cầu thang sát bên kia; chung cư/khách sạn/trường
  thành hành lang giữa, phòng hai bên; kho/xưởng/chợ/chùa thành sảnh mở,
  kho/xưởng có thêm văn phòng góc). `city/interior_furnish.cpp` kê đồ theo
  từng loại phòng: lưng sát tường, mặt vào phòng, không chồng nhau, không
  chắn cửa và lối đi; đồ nhỏ (đèn, hàng hoá) đặt trên bàn, kệ. Tường cắt
  thấp có nẹp tối ở mặt cắt.
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
  `sharp_radius`, `focus_radius`; hiện là nhà đang chọn), vùng nét là một
  vòng tròn quanh nó, ra ngoài thì mờ dần mịn và phủ sương màu trời
  (`post_fx::dof` quanh một điểm: `dof_center`, `dof_radius`, `dof_haze`);
  chi tiết nhỏ chỉ giữ trong `focus_radius`, viền tối đậm hơn;
  người qua đường ngoài khung hoặc xa tâm không vẽ. Mặt đất, đường, nước
  chia thành model theo ô 400×400 (`mesh_set`), engine bỏ ô ngoài tầm
  nhìn. HUD ghi số ô vẽ, ô chi tiết, số khối.
- Đèn đường (`city/render_props.cpp`): trời tối dần thì đèn sáng dần. Mọi
  đèn trong khung có bóng đèn sáng và một vũng sáng ấm mềm trên mặt đất
  (rẻ, bao nhiêu cũng được); 12 đèn gần giữa màn hình (hoặc nhà đang focus)
  có đèn điểm thật (`light3d_add`, engine nhận tối đa 16 đèn một lần vẽ
  3D), mờ dần về đèn xa nhất trong số đó để không bật tắt đột ngột khi kéo
  camera.
- Mặt ngoài (`city/render_kit.*`): mọi nhà ghép từ Downtown City MegaKit
  của Quaternius (CC0, `assets/models/city/`, giấy phép ở
  `LICENSE-kit.txt`). Mỗi mặt nhà chia thành tấm rộng 2 m hoặc 4 m của kit
  (co giãn cho vừa), mỗi tầng một tấm cao 3 m (một tầng 18 đơn vị, 6 đơn vị
  một mét). Nhà ống và nhà nhỏ: tường vữa sơn màu (bộ Trim, nhuộm theo từng
  nhà; một số nhà gạch ở phố cổ), tầng trệt cửa hàng hoặc cửa ra vào, cửa sổ
  các tầng trên, hông giáp nhà bên là tường trơn. Chung cư, khách sạn: kính
  và kim loại, cột ở góc. Chợ, trường, xưởng: gạch đỏ (chợ cửa vòm); kho:
  kim loại. Trên cùng có gờ mái. Giữ nét Việt Nam từ `city/render_facade.*`:
  ban công (lan can, chậu cây, quần áo phơi), mái ngói dốc, bồn nước, mái
  tôn, biển hiệu và mái che cửa hàng; máy lạnh là model của kit. Ban đêm các
  ô kính sáng ngẫu nhiên. Cả thành phố vẽ bằng kit (khoảng 65 nghìn tấm vẫn
  giữ 60 fps), instanced theo từng loại tấm và theo ô 100×100 như phần
  culling. Tầng dưới của nhà đang mở cũng dựng từ kit.
- `tools/make_city_kit.py "<thư mục kit>"` chép lại các tấm đang dùng: chỉ
  ảnh màu (thu còn 512 px), bỏ màu đỉnh (kit để mask mòn ở đó), kính đục,
  và ghi `kit.json` (kích thước từng tấm, ô kính để thắp đèn cửa sổ).
- Nội thất ghép từ `assets/models/interior/` (một phần "PSX modular house
  interior pack"; điều khoản đã được chủ dự án xác nhận, xem `SOURCE.txt`).
- Tỉ lệ: `city::units_per_metre` = 6 đơn vị một mét cho cả thành phố. Một
  tầng nhà 3 m (18 đơn vị, bằng tầng của kit), người 1,68 m
  (`city::person_height`, khoảng 10 đơn vị).
- `person.*`: hình người. `crowd.*`: người qua đường đi trên vỉa hè và hẻm
  theo nav grid bộ hành, khách ngồi ghế nhựa trước quán.
- Vật lý (`physics.*`, dùng vật lý 3D của njin trên Jolt, tính theo mét):
  mặt đất, mọi nhà và vật cản trên phố (thân cây, cột đèn, cột điện, xe máy
  đỗ, ghế nhựa, sạp, container, ghế đá, tượng đài, lan can cầu) là body tĩnh;
  mỗi người đi bộ là một nhân vật (`character3d`) mà tường, vật cản và người
  khác chặn lại, trượt vòng qua. Mỗi bước cố định (`crowd_step`) đám đông
  đọc vị trí vật lý vừa tính và đặt vận tốc muốn đi tới điểm kế trên đường;
  hướng mặt và nhịp bước theo chuyển động thật. Chỉ người camera thấy (khi
  nhìn gần) mới là nhân vật vật lý (`character3d_set_active`); người ở xa tự
  đi theo đường, không ai thấy. Nav grid bộ hành đóng ô có vật cản (nới thêm
  nửa bề ngang người) trừ khi đóng sẽ cắt lối (hẻm một ô), và chừa lối vào
  mọi cửa: không đỗ xe, không bày ghế chắn cửa. Người kẹt quá 1,5 s thì tìm
  đường khác. `--test` in số người đứng lẫn vào nhau, trong nhà, và số lần
  tìm đường lại.
- `--seed N`, `--citycheck [seed đầu] [số seed]` (kiểm tra hàng loạt, không
  mở cửa sổ), `--citymap <seed> <file.ppm>` (ảnh raster một pixel một ô).

Gameplay đã bị xóa; sẽ làm lại từ concept này, trên thành phố ở trên.
