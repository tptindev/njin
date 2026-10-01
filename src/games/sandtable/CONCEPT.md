# Sa Bàn Chiến Trận: concept

Người chơi là **đại ca** của một băng đảng. Phần lớn thời gian đại ca không
tự tay đánh nhau: đại ca quản lý đàn em, giao việc cho từng người và nhìn cả
vùng từ trên xuống như một sa bàn. Game kết hợp **sim** (quản lý người, tiền,
quan hệ) và **RTS** (điều quân trên bản đồ, theo thời gian thực). Khi muốn,
đại ca có thể **nhập vai Boss** để tự tay ra trận.

Góc nhìn: 3D, nhìn từ trên xuống (camera RTS: kéo, xoay, zoom), người là
tượng đất sét.

## Nhập vai Boss

Boss là một nhân vật có thật trên bản đồ, đi cùng đàn em như mọi người khác.

- Nhập vai lúc nào cũng được, thoát ra lúc nào cũng được. Khi nhập, camera
  chuyển mượt từ sa bàn nhìn xuống sang góc nhìn thứ ba sau lưng Boss, và
  người chơi điều khiển Boss trực tiếp. Khi thoát, camera trở về sa bàn.
- Không nhập thì Boss là bot: tự đi theo lệnh như một đàn em, gặp combat thì
  tự đánh.
- Simulation vẫn chạy khi đang nhập vai: đàn em vẫn làm theo lệnh đã giao,
  bot vẫn tự đánh. Nhập vai là một công cụ chiến thuật, không biến game
  thành action game.

## Vòng chơi

- **Đàn em**: mỗi người là một cá thể, có tên, chỉ số, tính cách và khả
  năng riêng. Đại ca tuyển người, giữ người, giao việc.
- **Thu tiền bảo kê**: giao đàn em đi thu tiền ở các cơ sở trong địa bàn.
  Đây là nguồn thu chính.
- **Giữ địa bàn**: băng khác có thể đưa người sang địa bàn của mình để thu
  tiền bảo kê. Cơ sở và dân cư thường phải trả nếu tại chỗ không có đủ người
  của ta bảo vệ.
- **Bị băng khác thu tiền**: băng địch thu cho đến khi đạt doanh thu mục
  tiêu (quota) rồi rút về địa bàn của chúng. Sau đó chủ cơ sở hoặc người dân
  có thể báo cho đại ca.
- **Trả đũa**: đại ca kéo đàn em sang địa bàn đối phương, thu tiền bảo kê
  ngay tại các cơ sở của chúng, vừa lấy tiền vừa khiêu khích.
- **Phản ứng của đối phương**: khi bị xâm phạm đủ nghiêm trọng, băng chủ địa
  bàn tập hợp đàn em và kéo đến giải quyết. Hai bên gặp nhau thì thành một
  trận đối đầu (xem dưới).
- **Tranh giành địa bàn**: các trận đối đầu dần làm thay đổi ảnh hưởng và
  quyền kiểm soát từng khu vực, không phải một trận là chiếm được cả vùng.
- **Bành trướng**: địa bàn rộng thì thu nhiều hơn, nhưng cần nhiều người hơn
  để tuần tra, bảo vệ và phản ứng khi bị xâm nhập.
- **Phi vụ**: các việc lớn do hệ thống sinh ra (không phải kịch bản viết
  tay), giao cho một nhóm đàn em làm.

## Vòng bảo kê và trả đũa

Vòng lặp này tự phát sinh từ simulation, không cần nhiệm vụ viết tay:

> Băng địch vào địa bàn → thu tiền ở cơ sở của mình → đạt quota → rút đi →
> dân/cơ sở báo cho đại ca → đại ca điều tra hoặc bỏ qua → tập hợp đàn em →
> sang địa bàn địch → thu tiền bảo kê ngược lại → đối phương phát hiện → tập
> hợp lực lượng → trận đối đầu → hoàn thành mục tiêu → rút quân → ảnh hưởng
> giữa hai băng thay đổi.

## Trận đối đầu (combat encounter)

Đánh nhau không phải là hai phe đánh đến khi một bên chết sạch. Khi hai băng
đối đầu, game chuyển sang một **trận có thời gian và mục tiêu cụ thể**.

- Người chơi điều nhóm kiểu RTS từ sa bàn, hoặc nhập vai Boss để tự đánh
  những pha quan trọng, rồi thoát ra để chỉ huy tiếp. Ai không được điều
  khiển (kể cả Boss) thì bot tự đánh.
- Mỗi trận có một hoặc nhiều điều kiện thắng, ví dụ:
  - trong 120 giây, làm ít nhất 60% lực lượng địch mất khả năng chiến đấu;
  - hết giờ, phe mình còn ít nhất 70% sức chiến đấu;
  - thu được ít nhất $5.000 tiền bảo kê trước khi rút;
  - giữ một vị trí trong một khoảng thời gian;
  - đánh gục một thành viên quan trọng của đối phương;
  - bảo vệ một đàn em đang thu tiền;
  - xong mục tiêu và rút khỏi địa bàn trước khi viện binh tới.
- **Sức chiến đấu** (combat capacity) không chỉ là số người còn đứng. Nó tính
  từ: người còn đánh được, stamina, thương tích, morale, trạng thái bị đánh
  ngã (knockdown), người bỏ chạy, người đang bị khống chế.

Ví dụ:

> **Retaliation Run**: thời gian 3:00; thu $8.000; làm mất khả năng chiến
> đấu ≥ 50% lực lượng địch; giữ sức chiến đấu phe mình ≥ 65%. Đạt mục tiêu
> rồi cả nhóm phải rút khỏi khu vực.

Mục tiêu không phải lúc nào cũng là diệt sạch đối thủ, mà là làm xong một
phi vụ có lời rồi rời đi với tổn thất chấp nhận được.

## Ý nghĩa chiến thuật

Trước và trong mỗi trận, người chơi phải cân nhắc:

- đem bao nhiêu đàn em, chọn ai;
- mục tiêu tiền là bao nhiêu;
- đánh nhanh rồi rút, hay cố gây thiệt hại lớn;
- có đưa Boss vào trận không, và khi nào tự nhập vai, khi nào để bot đánh
  để mình rảnh tay chỉ huy;
- có đáng chịu thương vong để trả đũa không;
- ở nhà còn đủ người giữ địa bàn trong lúc lực lượng chính đi đánh không.

Nếu kéo gần hết đàn em sang địa bàn đối phương, một băng thứ ba có thể lợi
dụng mà tiến vào địa bàn đang bỏ trống. Vì vậy combat là một phần của **quản
lý tổ chức và địa bàn**, không phải một minigame tách khỏi simulation.

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
  F4 ghim, T chuyển ngày (12:00) / đêm (21:00) (`weather.cpp` tính màu trời,
  ánh sáng theo `state.hour`; không tự trôi). Rê chuột để xem khối, nhà, cơ sở: quận dưới chuột được tô sáng
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
- Thế giới chạy theo đồng hồ (`clock.*`): một giờ trong game là 60 giây ở
  tốc độ 1 (`seconds_per_hour`; một ngày 24 phút, 8 phút ở tốc độ nhanh), theo tốc độ người chơi chọn, dừng khi mở
  popup. Mỗi loại cơ sở có giờ mở cửa (`business_hours`: chợ 4–18, quán
  ăn 6–23:30, bar 18–3, sòng bạc 20–5, khách sạn cả ngày...); quán đóng thì
  đàn em tới thu về tay không. Thẻ cơ sở trên HUD ghi giờ mở và lần bị băng
  khác thu gần nhất.
- Dân (`crowd.*`, 800 người): mỗi người có nhà, phần lớn có chỗ làm ở một
  cơ sở với ca làm theo giờ mở của nó, có giờ ngủ, giờ dậy (một số cú đêm).
  Rảnh thì đi ăn vào giờ cơm, tối đi chơi (karaoke, bar, cà phê), đi việc
  vặt ở cửa hàng đang mở, về nhà hoặc đi dạo; khuya thì về nhà. Trong nhà
  thì không hiện trên bàn: phố đông buổi sáng, tối đông nhất, khuya gần như
  vắng. Khách ngồi ghế nhựa chỉ có khi quán mở. Người ngoài tầm camera đi
  nhanh gấp 4 (`unseen_boost`), vì đồng hồ chạy nhanh hơn bước chân nhiều.
- Băng đối thủ (`gang_ai.*`): mỗi giờ từ 8:00 đến 23:00 đại ca của chúng
  cử người đi thu ở các quán của mình đang mở và đang nợ, thỉnh thoảng ép
  một quán chưa nộp ai cạnh địa bàn, và mỗi ngày tối đa một lần đánh mối
  sang địa bàn người chơi (`errand::raid`): 2–3 người đi từ quán này sang
  quán khác trong một khối, thu khoảng một ngày tiền bảo kê mỗi quán cho
  đến khi đủ quota. Quán có người của chủ địa bàn ở gần (ngoài đường trong
  12 m, hoặc trụ sở trong 25 m có ít nhất hai người ở nhà) thì không trả,
  và băng kia bỏ cuộc. Thu xong cả nhóm về, một lúc sau chủ quán báo cho
  đại ca (toast). Mỗi ngày mới chúng trả lương và tuyển thêm người nếu đủ
  tiền.
- Kinh tế nuôi quân (`gang.*`): tiền của băng vào từ bảo kê, ra cho lương,
  thuốc men và tuyển người; không bao giờ âm, lương không trả nổi thì thành
  nợ với từng người (trả nợ cũ trước, cấp cao trước). Mỗi đàn em có ví
  riêng: lương vào, ăn uống và thuê nhà ra mỗi ngày (`living_cost`: lính
  70k, tổ trưởng 100k, cánh tay phải 140k). Tinh thần lên khi được trả đủ
  và dư, xuống khi bị nợ lương (càng nhiều ngày càng nặng), không đủ ăn,
  bị thương không được chữa, hay quá mệt; Lì làm nó xuống chậm hơn. Dưới
  25 thì mỗi ngày có thể bỏ đi (về tới trụ sở là đi). Đi làm thì mệt, ở trụ
  sở thì hồi (ban đêm nhanh hơn); mệt từ 80 thì không đi. Ép quán thất bại
  hay đụng người canh thì có thể bị đánh; chữa ở phòng khám 80k/ngày thì
  hồi nhanh, không thì chậm; máu dưới 40 thì không đi được. Quán lâu ngày
  (3 ngày) không ai của băng ghé và không có người gần đó thì dần thôi nộp,
  nên băng ít người thì địa bàn co lại. Đại ca không lương, không bỏ đi, và
  cũng được giao việc như mọi người: còn một mình thì tự làm hết.
- Việc của đàn em (giao từ thẻ cơ sở): thu tiền bảo kê (quán của mình),
  tuần tra (đi vòng các quán trong khối cho tới khi mệt: giữ quán nộp, chặn
  đánh mối), gây hấn (sang quán băng khác thu tiền tại chỗ, đi tiếp các quán
  của băng đó trong khối), bành trướng (ép quán chưa nộp ai, hoặc giành mối
  của băng khác), rút lui (gọi về trụ sở ngay, trong popup đàn em).
- Popup Đàn em là lưới card (tên, cấp bậc, việc đang làm, thanh tinh thần,
  mệt, máu, ví và nợ lương); bấm một card mở thông tin đầy đủ của người đó:
  lương, chi tiêu, ví, nợ, chữa trị, xem trên bản đồ, rút về.
- Log mỗi ngày một dòng `[econ]` cho mỗi băng: tiền, số người, nợ lương,
  tinh thần trung bình, số người bị thương, mệt, số quán, số khối.
- Log mỗi giờ game một dòng `[clock]`: số dân ngoài đường, đang ngủ, đang
  làm, số quán mở, đàn em từng băng; `[ai]` và `[gang]` ghi việc của các
  băng đối thủ.
- `--hour H` (giờ bắt đầu), `--speed S` (tốc độ đồng hồ, để thử nhanh).
- `--seed N`, `--citycheck [seed đầu] [số seed]` (kiểm tra hàng loạt, không
  mở cửa sổ), `--citymap <seed> <file.ppm>` (ảnh raster một pixel một ô).

Việc chưa làm so với concept này: xem `BACKLOG.md`.
