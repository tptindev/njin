# Netcode: tick, input, nội suy, replicate {#netcode}

@ref multiplayer chở gói đi. Trang này lo phần khó bên trên, cho game realtime mà server quyết định mọi thứ (bắn súng,
hành động, top-down nhiều người): input đi theo tick, hai máy giữ cùng nhịp, người khác được vẽ mượt, và thế giới của
server tự hiện ra trên máy khách. Mọi thứ khai báo trong `njin_netcode.h`. Ý tưởng học từ
[Lightyear](https://github.com/cBournhonesque/lightyear) (Bevy) và các bài của Glenn Fiedler.

Cần biết trước: @ref multiplayer (net_delivery, `net_sim`), @ref ecs (EnTT), @ref time (`phase_fixed_update`).

## Mô hình

- **Server** mô phỏng thế giới thật trong `phase_fixed_update`, mỗi nhịp là một **tick**.
- **Máy khách** gửi input của từng tick, nhận lại **snapshot** trạng thái, và:
  - tự mô phỏng ngay nhân vật của mình (không chờ server: bấm là chạy),
  - vẽ mọi thứ khác lùi lại một chút, nội suy giữa hai snapshot cho mượt.

@include netcode.cpp

## Input theo tick

Trong `phase_fixed_update`, máy khách gọi netcode_set_input() mỗi tick; server đọc netcode_input() của từng người
chơi cho đúng tick đó. Input nên nhỏ (dùng bit_writer): hướng đi, nút bấm, góc nhìn.

- Mỗi gói input gửi kèm input của `input_redundancy` tick gần nhất (mặc định 8). Mất vài gói liền vẫn không mất
  input nào; input giống tick trước chỉ tốn 1 bit.
- Input của một tick chưa đến kịp thì netcode_input() trả về `false` và input gần nhất trước đó (nhân vật đi tiếp
  như cũ). netcode_info() đếm số lần này ở `missing_inputs`.
- `input_delay` dùng input chậm đi vài tick: kém nhạy hơn một chút, nhưng input kịp đến dù mạng chậm. Hợp với game
  đối kháng, RTS.
- Mọi máy đọc input của chính mình bằng `player` 0. Server tự chơi (listen server) cũng vậy.

## Đồng bộ tick

Input của tick T phải đến server **trước** khi server mô phỏng tick T. Vì vậy máy khách chạy **trước** server khoảng
nửa vòng mạng cộng một khoảng an toàn. Server báo lại input của máy khách đến sớm mấy tick, máy khách tự điều chỉnh:

- Lệch ít: chạy nhanh hay chậm đi tới 4% (time_set_fixed_rate()) cho tới khi input đến sớm đúng `input_margin` tick
  (mặc định 2). Người chơi không thấy.
- Lệch nhiều (lúc mới vào, sau một cú giật): nhảy tick một lần.

Server báo khoảng sớm **nhỏ nhất** trong các gói gần đây, nên khi mạng dao động máy khách tự chạy trước xa hơn. Mọi
máy phải chạy cùng `config::fixed_hz`; máy khách cảnh báo nếu khác.

## Nội suy

Component có `lerp` được vẽ trên máy khách lùi lại một khoảng sau snapshot mới nhất, nội suy giữa hai snapshot. Khoảng
lùi tự chọn: khoảng trống lớn nhất giữa các snapshot vừa nhận (vài snapshot liền nhau bị mất) cộng độ dao động thời
gian đến, đo trong khoảng 2 giây gần nhất. Mạng xấu đi thì nó tăng dần (thời gian vẽ chậm lại tối đa 10%, không nhảy),
mạng tốt lên thì giảm chậm. `netcode_opts::interp_delay` đặt cố định nếu muốn.

## Replicate component

Đăng ký mỗi loại component một lần trên **mọi máy, cùng thứ tự**, rồi server đánh dấu thực thể bằng
netcode_replicate(). Thực thể hiện ra trên máy khách (event `spawned`), component được cập nhật, hủy ở server là hủy ở
máy khách (`despawned`). Máy khách chỉ thêm những gì của riêng nó (sprite, âm thanh) vào thực thể đó.

| Trường của netcode_component_desc | Ý nghĩa |
|---|---|
| `write`, `read` | Ghi và đọc bằng bit_writer, độ chính xác vừa đủ |
| `lerp` | Có thì thực thể của người khác được nội suy |
| `predicted` | Trên máy điều khiển thực thể, server không ghi đè: game tự mô phỏng, so với netcode_confirmed() để sửa khi lệch |

Snapshot chỉ chở **phần đã đổi** so với snapshot máy khách đã xác nhận nhận được: thực thể đứng yên không tốn gì. Mỗi
snapshot gói trong `snapshot_budget` byte (mặc định 1100, vừa một gói UDP):

- netcode_set_relevance(): thực thể nào được gửi cho người chơi nào (chỉ thứ ở gần, cùng phòng...). Thực thể của chính
  người chơi luôn được gửi.
- netcode_set_priority(): thực thể đã đổi mà chưa được gửi thì cộng dồn độ ưu tiên; snapshot gửi tổng cao nhất trước.
  Thứ ở xa vẫn được cập nhật, chỉ thưa hơn, không bao giờ bị bỏ quên.

## Message và event

netcode_send() gửi message của game (chat, mua đồ, "đã sẵn sàng"); netcode_poll() đọc mọi việc của phiên: `joined`,
`left` (`code` là netcode_code_version, netcode_code_full hay 0 khi mất liên lạc), `message`, `spawned`,
`despawned`. Gọi lặp tới khi trả về `false`, thường trong `phase_fixed_update` của server.

## Thử

Bật `netcode_opts::sim` trên cả hai máy. Harness của engine chạy server và máy khách ở hai tiến trình với 150 ms trễ,
60 ms dao động, mất 20% gói mỗi chiều: input vẫn đến đủ, đồng bộ tick giữ được khoảng sớm 2 tick, thực thể nội suy
lệch đường đi thật trung bình khoảng 0.05 đơn vị (thực thể đi 2 đơn vị mỗi tick). netcode_info() cho tick, khoảng sớm, nhịp, cỡ snapshot, ping.

Lưu ý: `rtt` của netcode_info() là ping thật của ENet, không gồm độ trễ giả của `sim`.

## Ví dụ đầy đủ: njin_fps

`njin_fps --host` và `njin_fps --join IP` (xem @ref samples) dùng mọi thứ trên trang này trong một game bắn súng
góc nhìn thứ nhất: input là phím đi, góc nhìn và nút bắn của từng tick; chủ phòng di chuyển mọi người chơi bằng đúng
hàm di chuyển của chế độ chơi một mình, xét đạn trúng bia và giữ điểm; máy khách tự đi ngay theo input của mình.

Máy khách sửa lệch bằng cách **tua lại**: nó giữ input và trạng thái của mỗi tick. Khi netcode_confirmed() cho vị trí
(và tốc độ rơi, replicate trong một component `predicted` thứ hai) ở một tick cũ khác với điều nó đã đoán, nó lấy
trạng thái của chủ phòng ở tick đó rồi chạy lại các input từ đó tới nay. Cả hai bên làm tròn vị trí tới 1 cm sau mỗi
bước, như snapshot, nên khi không có gì xảy ra thì không bao giờ phải sửa. Với 150 ms trễ, 50 ms dao động và mất 15%
gói, máy khách chỉ phải sửa một lần lúc mới vào phòng.

## Chưa có

Rollback do engine làm (cả thế giới, cả vật lý): hiện game tự tua lại nhân vật của mình như njin_fps. Lag
compensation (server lùi thời gian khi xét trúng đạn) và spawn trước trên máy khách cũng chưa có.
