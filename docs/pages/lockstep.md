# Lockstep: cùng một mô phỏng trên mọi máy {#lockstep}

@ref netcode gửi **trạng thái**: server mô phỏng, máy khách nhận lại vị trí của từng thứ. Hợp với game bắn súng có vài
chục thứ chuyển động. Game có hàng nghìn thứ chuyển động (RTS, mô phỏng thành phố, quản lý) thì gửi trạng thái quá nặng.
Lockstep làm ngược lại: mọi máy chạy **cùng một mô phỏng**, tick này qua tick khác, và chỉ gửi cho nhau **lệnh** của
người chơi. Hai nghìn người dân đi lại không tốn một byte nào trên mạng. Mọi thứ khai báo trong `njin_lockstep.h`.

Cần biết trước: @ref multiplayer (net_delivery, `net_sim`), @ref time (`phase_fixed_update`), bit_writer.

## Mô hình

- **Máy chủ** (lockstep_host()) giữ nhịp. Lệnh của mọi người (cả của nó) được nó xếp vào một tick: lệnh nhận lúc nó
  đang ở tick T chạy ở tick T + `delay`. Mỗi tick chạy xong, nó gửi cho mọi máy: "tick này đã chốt, có những lệnh này".
- **Máy khách** (lockstep_join()) chỉ chạy một tick khi tick đó đã được chốt. Nó tự chạy nhanh hay chậm đi một chút
  (time_set_fixed_rate()) để luôn có khoảng `buffer` tick đã chốt trong tay, nên mạng giật nhẹ không làm game khựng.
- Mỗi `hash_interval` tick, mọi máy đưa một **mã trạng thái** (lockstep_hash()); máy chủ so, khác thì mọi máy nhận event
  `desync`. Lệch là lỗi của game (xem dưới), không phải của mạng: lệnh đi bằng kênh tin cậy, không mất.

@include lockstep.cpp

## Một tick

Trong `phase_fixed_update`, trước mô phỏng:

1. lockstep_step(): `false` thì bỏ qua nhịp này, thế giới đứng yên (lệnh chưa đến, hay máy khách chậm quá `max_lag`).
2. Làm theo lockstep_commands() của tick, theo đúng thứ tự trả về: thứ tự đó giống nhau trên mọi máy. `player` cho biết
   ai ra lệnh.
3. Chạy mô phỏng của tick.
4. Khi lockstep_hash_due(): lockstep_hash() với mã của thế giới, làm bằng lockstep_hasher.

**Không bao giờ** đổi thế giới ngay lúc người chơi bấm. Nút bấm gọi lockstep_send(); thế giới đổi khi lệnh quay về, ở
tick máy chủ chọn, trên mọi máy cùng lúc. Chơi một mình cũng nên đi cùng đường đó (ghi lệnh ra byte rồi đọc lại và làm
ngay): đường của chế độ nhiều người được chạy thử mỗi ván.

Lệnh ra mà chưa chạy thì thế giới chưa đổi. Nếu game chọn thứ gì theo trạng thái lúc bấm (người rảnh gần nhất để giao
việc), nhớ những thứ đã giao mà lệnh chưa về, để hai lệnh liền nhau không chọn trùng.

## Phòng và lúc bắt đầu

- Máy chủ nhận người cho tới lockstep_start(). Sau đó không ai vào được nữa (`left` với lockstep_code_started): vào giữa
  ván cần chép cả thế giới sang, game tự làm nếu cần.
- lockstep_start() gửi `setup` (seed, bản đồ, độ khó...) tới mọi máy trong event `started`. Mọi máy dựng thế giới từ đó.
  Máy chủ chỉ chạy tick 1 khi mọi máy khách đã gọi lockstep_step() lần đầu, tức là đã dựng xong.
- Máy chủ đi (`left` với `player` 0 trên máy khách): phiên kết thúc. Thế giới trên máy khách vẫn nguyên vẹn, giống hệt
  máy chủ tới tick cuối: game có thể lockstep_stop() rồi chạy tiếp một mình.
- lockstep_say() gửi thứ **ngoài mô phỏng** (con trỏ chuột, chỗ đang nhìn, chat) tới mọi người; đến qua event `message`.
- net_local_addresses() (@ref network) cho các địa chỉ IPv4 của máy, để hiện cho người kia gõ vào.

## Giữ mô phỏng tất định

Cùng một file exe, cùng seed, cùng lệnh ở cùng tick: kết quả phải giống nhau từng bit. Những thứ hay làm lệch:

| Làm lệch | Thay bằng |
|---|---|
| Mô phỏng đọc camera, kích thước cửa sổ, thứ đang hiện trên màn hình | Mô phỏng như nhau dù màn hình thấy gì |
| `delta()`, giờ thật, đếm frame | Chỉ `fixed_delta()` và tick |
| RNG dùng chung với UI hay âm thanh | RNG riêng cho mô phỏng; UI có RNG của nó |
| RNG không đặt lại khi bắt đầu ván mới | Đặt lại theo seed ở mỗi lần bắt đầu |
| Đổi thế giới trong `phase_update`, trong code vẽ | Chỉ trong tick, theo lệnh |
| Duyệt `unordered_map` theo con trỏ, đọc biến chưa khởi tạo | Thứ tự cố định |
| Luồng chạy song song mà kết quả tùy thứ tự | Kết quả không tùy lịch luồng |

Mã trạng thái nên gồm những thứ đổi nhiều (vị trí, tiền, máu): lệch ở đâu cũng sớm hiện ra. lockstep_hasher băm số thực
theo đúng bit của nó; struct có byte đệm thì thêm từng trường.

## Thử

Bật `lockstep_opts::sim` (trễ, dao động, mất gói) trên cả hai máy. Harness của engine chạy hai tiến trình với 40 ms trễ,
30 ms dao động và mất 10% gói, mỗi bên gửi lệnh ngẫu nhiên vài lần mỗi giây: tới tick 900 cả hai có cùng trạng thái sau
hơn 4600 lệnh; làm lệch một bit ở tick 300 thì cả hai máy nhận `desync` đúng tick 300. lockstep_stats_of() cho tick,
số tick đã chốt trong tay, số lần phải chờ, ping.

## Ví dụ đầy đủ: Turf

Co-op của Turf (`njin_turf --host`, `njin_turf --join IP`) chạy một thành phố 2000 người dân, xe cộ và các băng trên
mọi máy bằng trang này: lệnh là việc giao cho anh em, tốc độ đồng hồ, xin người ra đồn; con trỏ của người kia đi qua
lockstep_say().

## Chưa có

Vào giữa ván (chép thế giới cho người mới), chơi tiếp ván đã lưu, tua lại khi lệch (rollback). Máy chủ là một người
chơi: nó đi thì phiên kết thúc. Chơi qua Internet mà không mở cổng router (NAT traversal) cũng chưa có.
