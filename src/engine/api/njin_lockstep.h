#pragma once
#include "_types.h"
#include "njin_multiplayer.h"
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_lockstep
/// @{

/// Cách chạy một phiên lockstep (lockstep_host(), lockstep_join()). Mọi máy phải đặt
/// giống nhau các trường đánh dấu "giống nhau".
///
/// Lockstep: mọi máy chạy **cùng một mô phỏng**, tick này qua tick khác, và chỉ gửi
/// cho nhau **lệnh** của người chơi, không gửi trạng thái. Hợp với game có rất nhiều
/// thứ chuyển động (RTS, mô phỏng thành phố) mà gửi trạng thái thì quá nặng. Đổi lại,
/// mô phỏng phải **tất định**: cùng lệnh, cùng tick thì ra cùng kết quả trên mọi máy.
struct lockstep_opts {
  /// Phiên bản giao thức của game (giống nhau). Máy khác số bị từ chối
  /// (`left` với `code` lockstep_code_version).
  u32 version = 1;
  /// Số người chơi tối đa, kể cả máy chủ, 2..32.
  u32 max_players = 4;
  /// Trễ lệnh, tick (giống nhau): máy chủ xếp lệnh nó nhận lúc đang ở tick T vào tick
  /// T + `delay`. Nhỏ thì lệnh nhạy hơn; lớn thì máy khách ít phải chờ hơn.
  u32 delay = 2;
  /// Máy khách giữ chừng này tick đã chốt mà chưa chạy, để mạng giật một chút không làm
  /// game khựng. Nó tự chạy nhanh hay chậm đi một chút (time_set_fixed_rate()) để giữ
  /// đúng khoảng này.
  u32 buffer = 4;
  /// Máy chủ dừng chờ khi một máy khách chạy sau nó quá chừng này tick.
  u32 max_lag = 180;
  /// So mã trạng thái (lockstep_hash()) mỗi chừng này tick (giống nhau). 0 là không so.
  u32 hash_interval = 60;
  /// Lệnh lớn nhất, byte.
  u32 max_command = 4096;
  f32 timeout = 10.0f; ///< Như net_host_opts::timeout.
  net_sim sim;         ///< Giả lập mạng xấu trên máy này, như net_host_opts::sim.
  /// Phục vụ mạng trên luồng nền, như net_host_opts::service_thread: ping đúng với
  /// mạng thật thay vì cộng thêm một tới hai frame.
  bool service_thread = false;
};

/// `code` của event `left` khi máy chủ từ chối vì khác lockstep_opts::version.
constexpr u32 lockstep_code_version = 1;
/// `code` của event `left` khi máy chủ đã đủ người.
constexpr u32 lockstep_code_full = 2;
/// `code` của event `left` khi ván đã bắt đầu: không vào giữa chừng được.
constexpr u32 lockstep_code_started = 3;
/// `code` của event `left` khi máy chủ dừng phiên (lockstep_stop()).
constexpr u32 lockstep_code_stopped = 4;

/// Vai của máy này.
enum class lockstep_role : u8 {
  none,  ///< Chưa chạy phiên nào.
  host,  ///< lockstep_host(): giữ nhịp, xếp lệnh của mọi người vào tick.
  guest, ///< lockstep_join(): chạy theo các tick máy chủ đã chốt.
};

/// Loại của một lockstep_event.
enum class lockstep_event_kind : u8 {
  /// Một người chơi vào phiên. Máy khách vừa vào được nhận `joined` cho chính nó
  /// (lockstep_self()) và cho từng người đã ở đó, máy chủ là người chơi 0.
  joined,
  /// Một người chơi rời phiên (`code` là lý do, 0 là mất liên lạc). Trên máy khách,
  /// `player` 0 là máy chủ đi: phiên kết thúc, lockstep_step() không chạy tick nào nữa.
  left,
  /// Ván bắt đầu, tick 0; `data` là thứ máy chủ đưa vào lockstep_start() (seed, ...).
  started,
  /// Mã trạng thái của `player` ở tick `tick` khác của máy chủ: mô phỏng đã lệch.
  /// Mỗi người chơi chỉ báo một lần.
  desync,
  /// Một tin lockstep_say() của `player`, ngoài mô phỏng.
  message,
};

/// Một việc xảy ra trong phiên, đọc bằng lockstep_poll().
struct lockstep_event {
  lockstep_event_kind kind = lockstep_event_kind::message; ///< Loại.
  u32 player = 0;   ///< Người chơi liên quan.
  u32 code = 0;     ///< `left`: lý do.
  u32 tick = 0;     ///< `desync`: tick lệch.
  std::string data; ///< `started`, `message`: các byte.
};

/// Một lệnh của một người chơi, chạy ở tick hiện tại.
struct lockstep_command {
  u32 player = 0;   ///< Ai ra lệnh.
  std::string data; ///< Các byte game đưa vào lockstep_send().
};

/// Số liệu của phiên.
struct lockstep_stats {
  u32 tick = 0;      ///< Tick đã chạy.
  u32 ready = 0;     ///< Máy khách: số tick đã chốt mà chưa chạy.
  u32 lag = 0;       ///< Máy chủ: máy khách chậm nhất chạy sau bao nhiêu tick.
  u32 stalls = 0;    ///< Số lần lockstep_step() phải chờ, từ đầu ván.
  u32 players = 0;   ///< Số người chơi, kể cả máy này.
  f32 rtt = 0.0f;    ///< Máy khách: ping tới máy chủ, giây. Máy chủ: ping lớn nhất.
};

/// Mở phòng ở `port`. Máy này là người chơi 0; người khác vào bằng lockstep_join()
/// cho tới khi lockstep_start().
/// @param ctx Context của engine.
/// @param port Cổng UDP.
/// @param opts Cách chạy.
/// @return `false` nếu cổng đã bị chiếm hay đang chạy một phiên.
bool lockstep_host(context &ctx, u16 port, const lockstep_opts &opts = {});

/// Vào phòng ở `address:port`. Kết quả đến qua lockstep_poll(): `joined`, hoặc `left`.
/// @param ctx Context của engine.
/// @param address IP của máy chủ.
/// @param port Cổng.
/// @param opts Cách chạy.
/// @return `false` nếu địa chỉ sai hay đang chạy một phiên.
bool lockstep_join(context &ctx, const char *address, u16 port, const lockstep_opts &opts = {});

/// Dừng phiên: ngắt mọi máy, trả nhịp tick về 1. Mô phỏng của máy này vẫn còn nguyên:
/// game có thể chạy tiếp một mình.
/// @param ctx Context của engine.
void lockstep_stop(context &ctx);

/// Vai của máy này. @param ctx Context của engine. @return Vai.
lockstep_role lockstep_role_of(const context &ctx);

/// Id của người chơi này: 0 trên máy chủ, số máy chủ cấp trên máy khách (0 khi chưa vào).
/// @param ctx Context của engine. @return Id.
u32 lockstep_self(const context &ctx);

/// Mọi người chơi đang ở trong phiên, kể cả máy này, xếp tăng dần.
/// @param ctx Context của engine. @return Các id.
std::vector<u32> lockstep_players(const context &ctx);

/// Máy chủ: bắt đầu ván. Mọi máy nhận `started` với `setup` (seed, ván đã lưu...),
/// rồi dựng thế giới từ đó. Sau đó không ai vào được nữa.
/// @param ctx Context của engine.
/// @param setup Các byte cho mọi máy.
/// @return `false` nếu không phải máy chủ hay đã bắt đầu.
bool lockstep_start(context &ctx, std::string_view setup);

/// Ván đã bắt đầu chưa (`started` đã đến). @param ctx Context của engine.
/// @return `true` nếu đã.
bool lockstep_started(const context &ctx);

/// Đọc việc tiếp theo đã xảy ra trong phiên, nếu có. Gọi lặp tới khi trả về `false`.
/// @param ctx Context của engine.
/// @param out Việc đọc được.
/// @return `true` nếu có việc.
bool lockstep_poll(context &ctx, lockstep_event &out);

/// Ra một lệnh. Mọi máy chạy nó ở cùng một tick do máy chủ chọn, đọc qua
/// lockstep_commands(). Game **không** tự làm theo lệnh lúc gọi: làm theo nó khi nó
/// đến, như mọi máy khác.
/// @param ctx Context của engine.
/// @param command Các byte, tối đa lockstep_opts::max_command.
/// @return `false` nếu ván chưa bắt đầu, phiên đã kết thúc hay lệnh quá lớn.
bool lockstep_send(context &ctx, std::string_view command);

/// Gọi ở đầu mỗi `phase_fixed_update`, trước mô phỏng. `true`: chạy một tick ngay bây
/// giờ, sau khi làm theo lockstep_commands(). `false`: chưa được (lệnh của tick sau chưa
/// đến, hay một máy khách chậm quá xa): bỏ qua nhịp này, mô phỏng đứng yên.
///
/// Máy chủ chỉ chạy tick 1 khi mọi máy khách đã gọi hàm này lần đầu, tức là đã dựng
/// xong thế giới. Ngoài phiên, hay khi ván chưa bắt đầu, nó trả về `false`.
/// @param ctx Context của engine.
/// @return `true` nếu chạy tick.
bool lockstep_step(context &ctx);

/// Các lệnh của tick vừa được lockstep_step() cho chạy, theo đúng thứ tự trên mọi máy.
/// @param ctx Context của engine.
/// @return Các lệnh; rỗng nếu tick này không ai ra lệnh.
const std::vector<lockstep_command> &lockstep_commands(const context &ctx);

/// Tick đang chạy: 1 ở tick đầu tiên, tăng một mỗi lần lockstep_step() trả về `true`.
/// @param ctx Context của engine. @return Tick.
u32 lockstep_tick(const context &ctx);

/// Tick này có phải so mã trạng thái không (bội của lockstep_opts::hash_interval).
/// @param ctx Context của engine. @return `true` nếu phải.
bool lockstep_hash_due(const context &ctx);

/// Đưa mã trạng thái của tick vừa chạy, sau mô phỏng của tick đó, khi
/// lockstep_hash_due(). Máy chủ so mã của mọi máy; khác thì mọi máy nhận `desync`.
/// Dùng lockstep_hasher để làm mã.
/// @param ctx Context của engine.
/// @param hash Mã.
void lockstep_hash(context &ctx, u64 hash);

/// Gửi một tin tới mọi người chơi khác, **ngoài mô phỏng**: con trỏ, chỗ đang nhìn,
/// chat. Đến qua event `message`, không theo tick nào.
/// @param ctx Context của engine.
/// @param data Các byte.
/// @param delivery Cách giao.
void lockstep_say(context &ctx, std::string_view data, net_delivery delivery = net_delivery::unreliable);

/// Số liệu của phiên. @param ctx Context của engine. @return Số liệu.
lockstep_stats lockstep_stats_of(const context &ctx);

/// Làm mã trạng thái cho lockstep_hash() (FNV-1a 64 bit): đưa vào mọi thứ mô phỏng
/// có, theo một thứ tự cố định. Số thực được băm theo đúng các bit của nó.
struct lockstep_hasher {
  u64 value = 14695981039346656037ull; ///< Mã đến giờ.

  /// Thêm các byte. @param data Các byte. @param size Số byte.
  void add_bytes(const void *data, usize size) {
    const unsigned char *p = static_cast<const unsigned char *>(data);
    for (usize i = 0; i < size; ++i) {
      value ^= p[i];
      value *= 1099511628211ull;
    }
  }
  /// Thêm một giá trị kiểu đơn giản (số, enum, vec2...). Struct có byte đệm thì thêm
  /// từng trường: byte đệm không xác định. @param v Giá trị.
  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void add(const T &v) {
    add_bytes(&v, sizeof(T));
  }
  /// Thêm một chuỗi. @param s Chuỗi.
  void add(std::string_view s) {
    add(static_cast<u64>(s.size()));
    add_bytes(s.data(), s.size());
  }
};

/// @}
} // namespace njin
