#pragma once
#include "_types.h"
#include "njin_network.h"
#include <string>
#include <string_view>

namespace njin {
struct context;

/// @addtogroup grp_multiplayer
/// @{

/// Một máy chơi mạng realtime, tạo bởi net_host_create(): nhận người vào (server),
/// nối tới máy khác (client), hoặc cả hai.
///
/// `id == 0` là handle không hợp lệ. Host đã hủy cũng bị bỏ qua.
struct net_host {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Một máy ở đầu bên kia của một net_host.
///
/// `id == 0` là handle không hợp lệ. Máy đã ngắt cũng bị bỏ qua; id không bao giờ
/// được dùng lại.
struct net_peer {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Cách một gói được giao.
enum class net_delivery : u8 {
  /// Chắc chắn đến, đúng thứ tự trong kênh của nó; mất thì tự gửi lại. Cho thứ
  /// không được mất: vào phòng, bắn trúng, nhặt đồ, chat.
  reliable,
  /// Có thể mất; gói đến muộn hơn một gói mới hơn (cùng kênh) bị bỏ. Cho trạng thái
  /// gửi lại liên tục, chỉ cần cái mới nhất: vị trí, input mỗi tick.
  unreliable,
  /// Có thể mất, đến theo thứ tự bất kỳ, không gói nào bị bỏ vì đến muộn. Cho sự
  /// kiện nhỏ mà mỗi gói tự đủ nghĩa: tiếng bước chân, hiệu ứng.
  unordered,
};

/// Giả lập mạng xấu trên chính máy này, để thử game với độ trễ và mất gói khi chỉ
/// có một máy. Áp lên **các gói nhận được**: đặt trên cả hai máy để có độ trễ hai
/// chiều. Gói `reliable` không bị mất (ENet sẽ gửi lại), chỉ bị trễ.
struct net_sim {
  f32 latency = 0.0f; ///< Độ trễ thêm, giây.
  f32 jitter = 0.0f;  ///< Độ trễ ngẫu nhiên thêm, 0..jitter giây. Gói không tin cậy có thể đến lệch thứ tự.
  f32 loss = 0.0f;    ///< Tỉ lệ gói không tin cậy bị bỏ, 0..1.
};

/// Cách tạo một host.
struct net_host_opts {
  /// Cổng nhận người vào. 0 là để hệ điều hành chọn một cổng trống: đủ cho máy chỉ
  /// đi nối (client).
  u16 port = 0;
  /// Địa chỉ nghe: `nullptr` là mọi địa chỉ IPv4 của máy, `"127.0.0.1"` chỉ cho
  /// máy này. ENet chỉ dùng IPv4.
  const char *host = nullptr;
  /// Số máy nối cùng lúc tối đa (cả máy vào lẫn máy nối đi), 1..4095.
  u32 max_peers = 32;
  /// Số kênh, 1..255. Gói ở các kênh khác nhau không chờ nhau: một gói `reliable`
  /// bị mất ở kênh 0 không giữ chân vị trí gửi ở kênh 1.
  u32 channels = 2;
  /// Nén gói (range coder của ENet). **Mọi máy phải đặt giống nhau.**
  bool compress = false;
  /// Gói lớn nhất được nhận, byte. Lớn hơn thì máy gửi bị ngắt (`disconnected`, kèm
  /// một cảnh báo), để một máy lạ không bắt game cấp phát tùy ý. Nên đặt giống nhau
  /// trên mọi máy: net_peer_send() chỉ kiểm tra giới hạn của máy gửi.
  u32 max_packet = 1u << 20;
  /// Băng thông nhận và gửi tối đa, byte mỗi giây; 0 là không giới hạn. ENet dàn
  /// gói theo đó thay vì làm nghẽn đường truyền.
  u32 in_bandwidth = 0;
  u32 out_bandwidth = 0; ///< Xem `in_bandwidth`.
  /// Không nghe gì từ một máy quá chừng này giây thì coi như mất (net_host_event
  /// `disconnected`, `code` 0). Cũng là thời gian chờ tối đa của net_host_connect().
  f32 timeout = 10.0f;
  /// Số event chờ tối đa khi game chưa đọc (net_host_poll()). Quá thì gói cũ nhất bị
  /// bỏ, kèm một cảnh báo.
  u32 max_queued = 16384;
  net_sim sim; ///< Giả lập mạng xấu; mặc định tắt.
};

/// Loại của một net_host_event.
enum class net_host_event_kind : u8 {
  connected,    ///< Một máy đã nối (vào ta, hoặc ta nối tới nó).
  disconnected, ///< Một máy đã ngắt, hoặc nối không được, hoặc mất liên lạc.
  packet,       ///< Một gói đã đến.
};

/// Một việc xảy ra trên host, đọc bằng net_host_poll().
struct net_host_event {
  net_host_event_kind kind = net_host_event_kind::packet; ///< Loại.
  net_peer peer;                                          ///< Máy liên quan.
  /// `connected`: số mà máy kia đưa vào net_host_connect(). `disconnected`: số mà máy
  /// kia đưa vào net_peer_disconnect(); 0 cũng là mất liên lạc hay nối không được.
  u32 code = 0;
  u8 channel = 0;   ///< `packet`: kênh.
  std::string data; ///< `packet`: các byte.
  f64 time = 0.0;   ///< Lúc đến, giây theo net_clock().
};

/// Số liệu của một máy ở đầu bên kia.
struct net_peer_stats {
  f32 rtt = 0.0f;          ///< Thời gian đi và về trung bình, giây (ping).
  f32 rtt_variance = 0.0f; ///< Độ dao động của rtt, giây.
  f32 packet_loss = 0.0f;  ///< Tỉ lệ gói tin cậy phải gửi lại, 0..1.
  net_endpoint address;    ///< Địa chỉ.
};

/// Số liệu của một host.
struct net_host_stats {
  u32 peers = 0;               ///< Số máy đang nối.
  f32 sent_per_second = 0.0f;  ///< Byte gửi đi mỗi giây (đo mỗi giây, gồm phần đầu gói của ENet).
  f32 received_per_second = 0.0f; ///< Byte nhận mỗi giây.
  u64 sent_total = 0;          ///< Tổng byte đã gửi.
  u64 received_total = 0;      ///< Tổng byte đã nhận.
};

/// Tạo một host mạng realtime (ENet trên UDP).
///
/// Engine phục vụ host mỗi frame: nhận gói trong `phase_pre_update`, gửi đi những
/// gì game đã xếp trong `phase_post_update`. Gói đến được giữ cho tới khi game đọc
/// bằng net_host_poll().
/// @param ctx Context của engine.
/// @param opts Cách tạo.
/// @return Handle, hoặc handle không hợp lệ nếu cổng đã bị chiếm hay tham số sai
/// (cảnh báo nói vì sao).
net_host net_host_create(context &ctx, const net_host_opts &opts = {});

/// Hủy host: ngắt ngay mọi máy (không chờ chúng xác nhận) và đóng socket. Handle
/// không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param host Host.
void net_host_destroy(context &ctx, net_host host);

/// Nối tới một host khác. Hàm trả về ngay; kết quả đến qua net_host_poll():
/// `connected`, hoặc `disconnected` nếu không nối được.
///
/// `address` nên là IP (`"192.168.1.20"`). Tên máy (`"localhost"`) phải được phân
/// giải ngay, có thể làm frame đó chậm.
/// @param ctx Context của engine.
/// @param host Host của ta.
/// @param address Máy cần nối.
/// @param port Cổng.
/// @param code Số gửi kèm, bên kia thấy trong `net_host_event::code` (ví dụ phiên bản game).
/// @return Handle của máy kia, hoặc handle không hợp lệ nếu địa chỉ sai hay hết chỗ.
net_peer net_host_connect(context &ctx, net_host host, const char *address, u16 port, u32 code = 0);

/// Đọc việc tiếp theo đã xảy ra trên host, nếu có. Gọi lặp tới khi trả về `false`.
///
/// Hàm tự nhận gói mới từ mạng khi hàng đợi rỗng, nên gọi được ở bất kỳ phase nào.
/// Gọi trong `phase_fixed_update` thì mô phỏng thấy gói ngay trong tick đó, không trễ
/// thêm frame nào.
/// @param ctx Context của engine.
/// @param host Host.
/// @param out Việc đọc được.
/// @return `true` nếu có việc.
bool net_host_poll(context &ctx, net_host host, net_host_event &out);

/// Gửi một gói tới một máy. Gói được xếp hàng và gửi ở cuối frame (hoặc ngay khi
/// gọi net_host_flush()).
/// @param ctx Context của engine.
/// @param peer Máy nhận.
/// @param data Các byte.
/// @param delivery Cách giao.
/// @param channel Kênh, nhỏ hơn `net_host_opts::channels`.
/// @return `false` nếu máy không còn nối, kênh sai hay gói quá lớn.
bool net_peer_send(context &ctx, net_peer peer, std::string_view data, net_delivery delivery = net_delivery::reliable,
                   u8 channel = 0);

/// Gửi một gói tới mọi máy đang nối với host.
/// @param ctx Context của engine.
/// @param host Host.
/// @param data Các byte.
/// @param delivery Cách giao.
/// @param channel Kênh.
/// @return `false` nếu handle hay kênh sai.
bool net_host_broadcast(context &ctx, net_host host, std::string_view data,
                        net_delivery delivery = net_delivery::reliable, u8 channel = 0);

/// Gửi ngay mọi gói đã xếp hàng của host, thay vì chờ cuối frame: ví dụ ngay sau khi
/// gửi input trong `phase_fixed_update`.
/// @param ctx Context của engine.
/// @param host Host.
void net_host_flush(context &ctx, net_host host);

/// Ngắt một máy. Gói đã xếp hàng vẫn được gửi trước; máy kia nhận `disconnected` với
/// `code`. Handle bị bỏ ngay; ta không nhận event cho máy này nữa.
/// @param ctx Context của engine.
/// @param peer Máy.
/// @param code Số gửi kèm (ví dụ lý do bị kích). Nên khác 0: 0 cũng là mất liên lạc.
void net_peer_disconnect(context &ctx, net_peer peer, u32 code = 0);

/// Máy này còn nối không.
/// @param ctx Context của engine.
/// @param peer Máy.
/// @return `true` nếu đã nối xong và chưa ngắt.
bool net_peer_connected(const context &ctx, net_peer peer);

/// Số liệu của một máy: ping, mất gói, địa chỉ.
/// @param ctx Context của engine.
/// @param peer Máy.
/// @return Số liệu; toàn 0 nếu handle không hợp lệ.
net_peer_stats net_peer_info(const context &ctx, net_peer peer);

/// Số liệu của một host: số máy, băng thông.
/// @param ctx Context của engine.
/// @param host Host.
/// @return Số liệu; toàn 0 nếu handle không hợp lệ.
net_host_stats net_host_info(const context &ctx, net_host host);

/// Cổng mà host đang dùng (cổng hệ điều hành chọn khi `port` là 0).
/// @param ctx Context của engine.
/// @param host Host.
/// @return Cổng, 0 nếu handle không hợp lệ.
u16 net_host_port(const context &ctx, net_host host);

/// Đồng hồ của mạng: giây thật, tăng đều, không theo time_set_scale() và không dừng
/// khi pause. `net_host_event::time` đo bằng đồng hồ này.
/// @param ctx Context của engine.
/// @return Giây.
f64 net_clock(const context &ctx);

/// @}
} // namespace njin
