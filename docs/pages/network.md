# Mạng TCP và UDP {#network}

Trang này nối nhiều máy chơi với nhau: một máy làm chủ phòng (server), các máy khác vào phòng (client), gửi qua lại
vị trí, lượt đi, tin nhắn chat. Mọi thứ khai báo trong `njin_network.h`. Bên dưới là [asio](https://think-async.com/Asio/)
(bản standalone, không cần Boost), nằm trong engine.

Cần biết trước: @ref ecs (event qua `events(ctx)`) và @ref json (đóng gói dữ liệu để gửi).

## Không hàm nào chờ mạng

Mạng chậm và hay đứt, còn game phải vẽ đủ 60 frame mỗi giây. Vì thế mọi socket chạy trên **một luồng mạng riêng**
của engine (asio, viết bằng coroutine C++20). Game không bao giờ đợi:

- tcp_connect(), tcp_send(), udp_send() trả về ngay; việc thật làm sau, trên luồng mạng.
- Những gì xảy ra trên mạng (đã nối, nhận được message, bị đứt) đến dưới dạng **event**. Engine đẩy chúng vào
  `events(ctx)` ở `phase_pre_update`; chúng được phát sau `phase_post_update` của cùng frame, như mọi event đã
  `enqueue`.

Luồng mạng chỉ bắt đầu khi game gọi hàm mạng đầu tiên; game không dùng mạng thì không có luồng nào. Khi context bị
hủy, mọi socket được đóng và luồng dừng.

| Event | Khi nào |
|---|---|
| net_connected | Một kết nối TCP đã mở: tcp_connect() nối được, hoặc server vừa nhận một máy (`server` khác 0) |
| net_connect_failed | tcp_connect() không nối được: sai tên máy, không ai nghe, hết `connect_timeout` |
| net_disconnected | Kết nối đang mở bị đóng từ phía bên kia hoặc bị lỗi |
| net_message | Đã nhận đủ một message TCP |
| net_datagram | Đã nhận một gói UDP |

`reason` của hai event lỗi là tiếng Anh ngắn (`"connection refused"`, `"timed out"`, `"closed by peer"`), để ghi
log.

## TCP: message nguyên vẹn, đúng thứ tự

TCP chắc chắn đến và đến đúng thứ tự. Dùng nó cho thứ không được mất: vào phòng, lượt đi, chat, mua đồ.

Bản thân TCP là một dòng byte, không có ranh giới: hai lần gửi có thể dính làm một, một lần gửi có thể đến làm hai
mảnh. njin gửi kèm độ dài của mỗi message (4 byte, little-endian), nên **mỗi tcp_send() đến bên kia đúng thành một
net_message**, không bao giờ bị cắt hay dính. Bên kia vì thế cũng phải là njin (hoặc đọc cùng cách đóng gói).

@include network.cpp

| Hàm | Làm gì |
|---|---|
| tcp_listen() | Mở một server. Trả về handle không hợp lệ ngay nếu cổng đã bị chiếm |
| tcp_connect() | Nối tới một server. Tên máy được phân giải trên luồng mạng |
| tcp_send() | Xếp một message để gửi. Gửi được cả trước khi nối xong |
| tcp_close(), tcp_server_close() | Đóng một kết nối, một server |
| tcp_connected(), tcp_pending() | Đang mở chưa; còn bao nhiêu byte chờ gửi |
| tcp_server_port() | Cổng thật của server mở với cổng 0 |

Kết nối do chính game đóng bằng tcp_close() **không** phát net_disconnected: game đã biết. Đóng server thì không nhận
máy mới, nhưng các máy đã vào vẫn nối.

### Giới hạn để một máy lạ không làm hỏng game

`tcp_opts` đặt cho mỗi kết nối hai giới hạn:

- `max_message` (mặc định 1 MB): bên kia gửi message lớn hơn thì kết nối bị đóng. Không có giới hạn này, một máy
  báo "message dài 4 GB" là bắt được game cấp phát chừng ấy bộ nhớ.
- `max_pending` (mặc định 8 MB): bên kia đọc quá chậm và hàng đợi gửi đầy thì tcp_send() trả về `false` và bỏ
  message, thay vì để bộ nhớ phình mãi.

Server nghe ở mọi địa chỉ của máy khi `host` là `nullptr`, tức là máy khác trong mạng LAN (và Internet, nếu router
mở cổng) nối vào được. Chỉ chơi thử trên một máy thì nghe ở `"127.0.0.1"`. Message nhận được là dữ liệu từ máy
khác: kiểm tra nó trước khi tin (json_parse() trả về `false`, số nằm ngoài khoảng hợp lệ...).

## UDP: nhanh, nhưng có thể mất

UDP gửi từng gói rời, không cần nối trước. Gói **có thể mất, đến sai thứ tự hay đến hai lần**, nhưng không bị chậm
lại vì một gói mất trước đó. Dùng nó cho thứ gửi lại liên tục, mất một gói cũng không sao: vị trí người chơi mỗi
frame, tìm phòng trong mạng LAN.

```cpp
const njin::net_udp sock = njin::udp_open(ctx, 7778); // cổng 0 nếu chỉ gửi đi rồi nhận trả lời
njin::udp_send(ctx, sock, {"192.168.1.20", 7778}, "ping");

void on_datagram(njin::context &ctx, const njin::net_datagram &e) {
  if (e.data == "ping")
    njin::udp_send(ctx, e.socket, e.from, "pong"); // trả lời đúng máy đã gửi
}
```

Giữ mỗi gói dưới khoảng 1200 byte: gói lớn hơn bị chia nhỏ trên đường, và mất một mảnh là mất cả gói. udp_send()
từ chối gói trên 65507 byte. `host` nên là địa chỉ IP: tên máy phải được phân giải lại ở mỗi lần gửi.

## IPv4 và IPv6

`host` của tcp_listen() và udp_open() mặc định là mọi địa chỉ **IPv4**. Truyền `"::"` để nghe IPv6. Một socket UDP
IPv4 chỉ gửi được tới địa chỉ IPv4; tcp_connect() tự thử mọi địa chỉ của tên máy cho đến khi nối được.

net_local_addresses() cho các địa chỉ IPv4 của chính máy này (trừ 127.0.0.1), để hiện cho người chơi khác gõ vào
khi vào phòng. Nó hỏi hệ điều hành ngay: gọi một lần rồi giữ lại.

## Không có ở đây

Trang này là lớp vận chuyển: đưa byte từ máy này sang máy kia. Game realtime (vị trí mỗi tick cùng với việc không
được mất trên một kết nối) xem @ref multiplayer. Phần trên nữa do game tự chọn theo thể loại: đồng bộ trạng thái, dự
đoán phía client, sửa lệch, chống gian lận. HTTP (bảng xếp hạng, đăng nhập) cũng chưa có.
