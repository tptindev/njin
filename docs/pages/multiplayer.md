# Chơi mạng realtime {#multiplayer}

Trang này dành cho game mà mọi người chơi cùng lúc, từng tick: bắn súng, đua xe, đánh nhau, co-op hành động. Mọi
thứ khai báo trong `njin_multiplayer.h` và `njin_bits.h`. Bên dưới là [ENet](http://enet.bespin.org/) (MIT) trên UDP,
nằm trong engine.

Cần biết trước: @ref network (TCP, UDP và vì sao game không bao giờ chờ mạng), @ref time (`phase_fixed_update`).

## Trang này hay TCP

| Game | Dùng |
|---|---|
| Theo lượt, chat, lobby, RTS lockstep (chỉ gửi lệnh) | `tcp_*` (xem @ref network), đơn giản, mọi message chắc chắn đến |
| Realtime: vị trí, input mỗi tick | `net_host_*` ở trang này |

TCP đảm bảo đúng thứ tự cho **mọi** byte, nên một gói mất làm mọi thứ sau nó đứng chờ (giật). Game realtime cần
trộn hai loại: vị trí mất một gói cũng không sao vì gói sau đã mới hơn, còn "bắn trúng" thì không được mất. ENet
cho cả hai trên cùng một kết nối UDP.

## Host, peer và kênh

Mỗi máy tạo một **net_host**. Máy làm server đặt `port`; máy khách để `port = 0` rồi gọi net_host_connect(). Mỗi máy
ở đầu bên kia là một **net_peer**.

@include multiplayer.cpp

Mỗi gói đi trên một **kênh** (`channel`) với một cách giao (net_delivery):

| net_delivery | Đảm bảo | Dùng cho |
|---|---|---|
| `reliable` | Chắc chắn đến, đúng thứ tự trong kênh | Vào phòng, bắn trúng, nhặt đồ, chat |
| `unreliable` | Có thể mất; gói cũ đến sau gói mới bị bỏ | Vị trí, input mỗi tick |
| `unordered` | Có thể mất, đến thứ tự bất kỳ | Sự kiện nhỏ tự đủ nghĩa: tiếng động, hiệu ứng |

Các kênh không chờ nhau: gói `reliable` bị mất ở kênh 1 không giữ chân vị trí ở kênh 0. Đặt số kênh bằng
`net_host_opts::channels`, giống nhau trên mọi máy.

## Đọc gói ngay trong tick

Engine nhận gói trong `phase_pre_update` và gửi những gì game đã xếp trong `phase_post_update`. Gói đến được giữ cho
tới khi game đọc bằng net_host_poll(). Hàm này tự nhận thêm gói từ mạng khi hàng đợi rỗng, nên gọi trong
`phase_fixed_update` thì mô phỏng thấy input ngay trong tick đó. Sau khi gửi input, net_host_flush() đẩy nó đi luôn
thay vì chờ cuối frame. Hai việc này bớt được tới một frame trễ ở mỗi đầu.

| Việc | Hàm |
|---|---|
| Tạo, hủy | net_host_create(), net_host_destroy() |
| Nối, ngắt | net_host_connect(), net_peer_disconnect() |
| Đọc | net_host_poll(): `connected`, `disconnected`, `packet` |
| Gửi | net_peer_send(), net_host_broadcast(), net_host_flush() |
| Số liệu | net_peer_info() (ping, mất gói), net_host_info() (băng thông), net_host_port(), net_clock() |

`code` của net_host_connect() đến tay server trong event `connected`: dùng để từ chối bản game khác phiên bản. `code`
của net_peer_disconnect() đến tay máy kia trong `disconnected` (ví dụ lý do bị kích); 0 là mất liên lạc, nên các lý
do của game bắt đầu từ 1.

## Gói nhỏ: njin_bits.h

Băng thông là giới hạn thật của game mạng: 16 người, 60 tick mỗi giây, mỗi người 100 byte là gần 100 KB/s cho mỗi
máy khách. bit_writer ghi mỗi giá trị bằng đúng số bit nó cần:

| Ghi | Tốn |
|---|---|
| write_bool() | 1 bit |
| write_int(v, 0, 63) | 6 bit |
| write_float(x, -2000, 2000, 0.01f): 1 cm trong ±2 km | 19 bit thay vì 32 |
| write_vec3(p, -500, 500, 0.01f) | 3 × 17 bit |
| write_angle(a, 10): sai số 0.35° | 10 bit |
| write_varint(): id, số đếm | 1 byte cho 0..127 |

bit_reader đọc lại **đúng thứ tự và đúng tham số** đã ghi. Gói đến từ máy khác có thể sai hay cố ý độc hại, nên
bit_reader không bao giờ đọc ra ngoài dữ liệu: đọc quá cuối, số ngoài khoảng hay chuỗi quá dài làm ok() thành `false`
và từ đó mọi lần đọc trả về 0. Đọc hết rồi kiểm tra `ok()` một lần, hỏng thì bỏ cả gói.

bit_writer cũng dùng được ngoài mạng: replay (ghi input mỗi tick), save nhị phân nhỏ gọn.

## Thử mạng xấu trên một máy

Trong mạng LAN mọi thứ đều mượt; trên Internet thì không. `net_host_opts::sim` làm chậm và làm mất các gói nhận được
ngay trên máy này:

```cpp
njin::net_host_opts opts;
opts.sim = {.latency = 0.1f, .jitter = 0.03f, .loss = 0.05f}; // 100-130 ms, mất 5%
```

Đặt trên cả hai máy để có độ trễ hai chiều. Gói `reliable` không bị mất (ENet gửi lại), chỉ bị trễ. Bật nó suốt lúc
làm game, không chỉ trước khi phát hành: code mạng viết trong LAN thường hỏng ngay khi có 100 ms trễ.

## An toàn

- `max_packet` (mặc định 1 MB): máy gửi gói lớn hơn bị ngắt, kèm một cảnh báo. Đặt giống nhau trên mọi máy.
- `max_peers`: số máy nối cùng lúc tối đa.
- `max_queued`: game không đọc net_host_poll() thì gói cũ nhất bị bỏ (không bao giờ bỏ `connected` hay
  `disconnected`), thay vì để bộ nhớ phình mãi.
- `timeout`: không nghe gì quá chừng ấy giây thì máy kia bị coi là mất; cũng là thời gian chờ nối tối đa.

ENet không mã hóa: ai cùng mạng cũng đọc được gói. Không gửi mật khẩu hay thông tin thanh toán qua đây.

## Không có ở đây

Đây là lớp vận chuyển: gói đi được, đi nhanh, đi gọn. Input theo tick, đồng bộ tick, nội suy và replicate component
có sẵn ở @ref netcode, xây trên trang này. Lùi thời gian khi xét trúng đạn (lag compensation) chưa có. Chơi qua Internet mà không mở cổng router (NAT traversal) cũng chưa
có. ENet chỉ dùng IPv4.
