#pragma once
// Thuộc feature net: game cần bật nó bằng njin_use(<game> FEATURES net)
// (cmake/njin.cmake), hoặc link njin::rt.
#if !defined(NJIN_ENGINE_INTERNAL) && !defined(NJIN_FEATURE_NET)
#error "njin_network.h is part of the njin feature 'net', which this game does not use: add it with njin_use(<game> FEATURES net)"
#endif
#include "_types.h"
#include <string>
#include <string_view>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_network
/// @{

/// Một kết nối TCP, tạo bởi tcp_connect() hoặc nhận được qua net_connected khi
/// một server chấp nhận máy khác.
///
/// `id == 0` là handle không hợp lệ. Kết nối đã đóng cũng bị bỏ qua; id không
/// bao giờ được dùng lại.
struct net_conn {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Một server TCP đang nghe, tạo bởi tcp_listen().
///
/// `id == 0` là handle không hợp lệ. Server đã đóng cũng bị bỏ qua.
struct net_server {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Một socket UDP, tạo bởi udp_open().
///
/// `id == 0` là handle không hợp lệ. Socket đã đóng cũng bị bỏ qua.
struct net_udp {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Địa chỉ của một máy: IP dạng chữ (`"192.168.1.5"`, `"::1"`) và cổng.
struct net_endpoint {
  std::string host; ///< Địa chỉ IP (udp_send() nhận cả tên máy).
  u16 port = 0;     ///< Cổng.
};

/// @name Event
/// Engine đẩy các event này vào events() trong `phase_pre_update`; chúng được
/// phát sau `phase_post_update` của cùng frame, như mọi event đã `enqueue`.
/// @{

/// Một kết nối TCP đã mở: tcp_connect() đã nối được, hoặc một server vừa chấp
/// nhận một máy khác (khi đó `server` là server đó).
struct net_connected {
  net_conn conn;       ///< Kết nối.
  net_server server;   ///< Server đã chấp nhận nó; `id == 0` nếu là tcp_connect().
  net_endpoint remote; ///< Máy ở đầu bên kia.
};

/// tcp_connect() không nối được (sai tên máy, không có ai nghe, hết thời gian).
/// Handle đã bị bỏ, không cần tcp_close().
struct net_connect_failed {
  net_conn conn;      ///< Kết nối.
  std::string reason; ///< Lý do, tiếng Anh, để ghi log.
};

/// Một kết nối đang mở bị đóng từ phía bên kia hoặc bị lỗi. Handle đã bị bỏ.
/// Kết nối do chính game đóng bằng tcp_close() không phát event này.
struct net_disconnected {
  net_conn conn;      ///< Kết nối.
  std::string reason; ///< Lý do, tiếng Anh, để ghi log.
};

/// Một message đã nhận đủ trên kết nối TCP: đúng các byte mà bên kia đã đưa
/// vào một lần tcp_send(), không bị cắt hay dính với message khác.
struct net_message {
  net_conn conn;    ///< Kết nối.
  std::string data; ///< Các byte của message (có thể là nhị phân).
};

/// Một gói UDP đã nhận.
struct net_datagram {
  net_udp socket;    ///< Socket đã nhận.
  net_endpoint from; ///< Máy đã gửi; trả lời bằng udp_send() tới đó.
  std::string data;  ///< Các byte của gói.
};
/// @}

/// Cách mở một kết nối hay một server TCP.
struct tcp_opts {
  /// Message lớn nhất được nhận, byte. Bên kia gửi message lớn hơn thì kết nối
  /// bị đóng (net_disconnected), để một máy lạ không bắt game cấp phát tùy ý.
  u32 max_message = 1u << 20;
  /// Số byte được chờ gửi tối đa của một kết nối. Quá mức này tcp_send() trả về
  /// `false` và bỏ message, thay vì để bộ nhớ phình khi bên kia đọc chậm.
  u32 max_pending = 8u << 20;
  /// Thời gian chờ nối tối đa của tcp_connect(), giây.
  f32 connect_timeout = 10.0f;
  /// Gửi ngay từng message thay vì gom lại (tắt thuật toán Nagle). Game cần độ
  /// trễ thấp hơn là gói lớn, nên mặc định bật.
  bool no_delay = true;
};

/// Nối tới một server TCP. Hàm trả về ngay; việc phân giải tên máy và nối chạy
/// trên luồng mạng của engine, kết quả đến sau qua net_connected hoặc
/// net_connect_failed.
/// @param ctx Context của engine.
/// @param host Tên máy hoặc IP (`"localhost"`, `"example.com"`, `"10.0.0.2"`).
/// @param port Cổng.
/// @param opts Tùy chọn.
/// @return Handle của kết nối, hoặc handle không hợp lệ nếu `host` rỗng.
net_conn tcp_connect(context &ctx, const char *host, u16 port, const tcp_opts &opts = {});

/// Gửi một message. Message được gửi kèm độ dài, nên bên nhận nhận nó nguyên
/// vẹn trong một net_message. Hàm không chờ: message được xếp hàng và gửi
/// trên luồng mạng. Có thể gửi ngay sau tcp_connect(), trước khi nối xong.
///
/// Bên kia phải cũng dùng njin (hoặc cùng cách đóng gói: 4 byte độ dài,
/// little-endian, rồi đến các byte của message).
/// @param ctx Context của engine.
/// @param conn Kết nối.
/// @param data Các byte cần gửi.
/// @return `false` nếu handle không còn mở, message lớn hơn `tcp_opts::max_message`
/// hoặc hàng đợi gửi đã đầy (`tcp_opts::max_pending`).
bool tcp_send(context &ctx, net_conn conn, std::string_view data);

/// Đóng một kết nối. Message đã xếp hàng nhưng chưa gửi bị bỏ. Không phát
/// net_disconnected. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param conn Kết nối.
void tcp_close(context &ctx, net_conn conn);

/// Kết nối đã nối xong và chưa đóng chưa. Đây là trạng thái lúc gọi, nên nó có thể
/// thành `true` trước khi net_connected đến (event đến ở frame sau).
/// @param ctx Context của engine.
/// @param conn Kết nối.
/// @return `true` nếu đang mở.
bool tcp_connected(const context &ctx, net_conn conn);

/// Số byte đang chờ gửi trên một kết nối: để biết mạng có theo kịp không.
/// @param ctx Context của engine.
/// @param conn Kết nối.
/// @return Số byte, 0 nếu handle không hợp lệ.
usize tcp_pending(const context &ctx, net_conn conn);

/// Mở một server TCP. Việc mở cổng xong ngay khi hàm trả về; mỗi máy nối vào
/// đến sau qua net_connected (với `server` là server này).
/// @param ctx Context của engine.
/// @param port Cổng; 0 là để hệ điều hành chọn một cổng trống (xem tcp_server_port()).
/// @param host Địa chỉ nghe: `nullptr` là mọi địa chỉ IPv4 của máy, `"127.0.0.1"`
/// chỉ cho máy này, `"::"` cho IPv6.
/// @param opts Tùy chọn cho các kết nối mà server nhận.
/// @return Handle, hoặc handle không hợp lệ nếu cổng đã bị chiếm hay địa chỉ sai
/// (cảnh báo nói vì sao).
net_server tcp_listen(context &ctx, u16 port, const char *host = nullptr, const tcp_opts &opts = {});

/// Cổng mà server đang nghe.
/// @param ctx Context của engine.
/// @param server Server.
/// @return Cổng, 0 nếu handle không hợp lệ.
u16 tcp_server_port(const context &ctx, net_server server);

/// Đóng một server: không nhận máy mới nữa. Các kết nối nó đã nhận vẫn mở.
/// Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param server Server.
void tcp_server_close(context &ctx, net_server server);

/// Mở một socket UDP. Mỗi gói nhận được đến qua net_datagram.
/// @param ctx Context của engine.
/// @param port Cổng; 0 là để hệ điều hành chọn (đủ cho máy chỉ gửi đi rồi nhận trả lời).
/// @param host Địa chỉ nghe: `nullptr` là mọi địa chỉ IPv4, `"::"` cho IPv6.
/// @return Handle, hoặc handle không hợp lệ nếu cổng đã bị chiếm hay địa chỉ sai.
net_udp udp_open(context &ctx, u16 port = 0, const char *host = nullptr);

/// Gửi một gói UDP. Hàm không chờ. UDP không hứa gói sẽ đến, đến đúng thứ tự
/// hay chỉ đến một lần; gói lớn hơn khoảng 1200 byte có thể bị bỏ trên đường.
/// @param ctx Context của engine.
/// @param socket Socket.
/// @param to Máy nhận. `host` là IP hoặc tên máy (tên được phân giải trên luồng mạng).
/// @param data Các byte cần gửi, tối đa 65507.
/// @return `false` nếu handle không hợp lệ hoặc gói quá lớn.
bool udp_send(context &ctx, net_udp socket, const net_endpoint &to, std::string_view data);

/// Cổng của một socket UDP.
/// @param ctx Context của engine.
/// @param socket Socket.
/// @return Cổng, 0 nếu handle không hợp lệ.
u16 udp_port(const context &ctx, net_udp socket);

/// Đóng một socket UDP. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param socket Socket.
void udp_close(context &ctx, net_udp socket);

/// Các địa chỉ IPv4 của máy này, trừ 127.0.0.1: để hiện cho người chơi khác gõ vào
/// khi vào phòng ("192.168.1.20"). Hỏi hệ điều hành ngay, có thể mất vài mili giây:
/// gọi một lần rồi giữ lại, đừng gọi mỗi frame.
/// @return Các địa chỉ; rỗng nếu máy không có mạng.
std::vector<std::string> net_local_addresses();

/// @}
} // namespace njin
