#pragma once
// Thuộc feature net: game cần bật nó bằng njin_use(<game> FEATURES net)
// (cmake/njin.cmake), hoặc link njin::rt.
#if !defined(NJIN_ENGINE_INTERNAL) && !defined(NJIN_FEATURE_NET)
#error "njin_netcode.h is part of the njin feature 'net', which this game does not use: add it with njin_use(<game> FEATURES net)"
#endif
#include "_types.h"
#include "njin_bits.h"
#include "njin_multiplayer.h"
#include <entt/entity/registry.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace njin {
struct context;

/// @addtogroup grp_netcode
/// @{

/// Cách chạy một phiên chơi mạng (netcode_host(), netcode_join()). Server và mọi
/// máy khách phải đặt giống nhau các trường đánh dấu "giống nhau".
struct netcode_opts {
  /// Phiên bản giao thức của game (giống nhau). Máy khách khác số bị từ chối
  /// (`left` với `code` netcode_code_version).
  u32 version = 1;
  /// Số máy khách tối đa (server).
  u32 max_clients = 16;
  /// Trễ input, tick: input đặt ở tick T được dùng ở tick T + `input_delay` (giống
  /// nhau). Đổi một chút độ nhạy lấy việc input kịp đến server dù mạng chậm.
  u32 input_delay = 0;
  /// Mỗi gói input gửi kèm input của chừng này tick gần nhất, nên mất vài gói liền
  /// vẫn không mất input nào. 1..32.
  u32 input_redundancy = 8;
  /// Input của máy khách nên đến server sớm chừng này tick. Máy khách tự chạy nhanh
  /// hay chậm đi một chút (time_set_fixed_rate()) để giữ đúng khoảng này.
  u32 input_margin = 2;
  /// Server gửi snapshot mỗi chừng này tick (2 ở 60 Hz là 30 snapshot mỗi giây).
  u32 snapshot_interval = 2;
  /// Ngân sách mỗi snapshot cho một máy khách, byte. Thực thể ưu tiên cao gửi
  /// trước; thực thể không vừa thì tích ưu tiên và đi ở snapshot sau. Dưới khoảng
  /// 1200 để một snapshot là một gói UDP.
  u32 snapshot_budget = 1100;
  /// Máy khách vẽ thực thể nội suy lùi lại chừng này giây so với snapshot mới nhất.
  /// 0 là tự chọn: 2 khoảng snapshot cộng 30 ms.
  f32 interp_delay = 0.0f;
  f32 timeout = 10.0f; ///< Như net_host_opts::timeout.
  net_sim sim;         ///< Giả lập mạng xấu trên máy này, như net_host_opts::sim.
};

/// `code` của event `left` khi server từ chối vì khác `netcode_opts::version`.
constexpr u32 netcode_code_version = 1;
/// `code` của event `left` khi server đã đủ người.
constexpr u32 netcode_code_full = 2;

/// Vai của máy này.
enum class netcode_role : u8 {
  none,   ///< Chưa chạy phiên nào.
  server, ///< netcode_host().
  client, ///< netcode_join().
};

/// Loại của một netcode_event.
enum class netcode_event_kind : u8 {
  joined,    ///< Server: một người chơi vào. Máy khách: đã vào được server (`player` là id của ta).
  left,      ///< Server: một người chơi ra. Máy khách: mất server (`code` nói vì sao).
  message,   ///< Một message của game (netcode_send()).
  spawned,   ///< Máy khách: một thực thể replicate vừa xuất hiện (`entity`).
  despawned, ///< Máy khách: một thực thể replicate vừa bị hủy (`entity` đã không còn hợp lệ).
};

/// Một việc xảy ra trong phiên, đọc bằng netcode_poll().
struct netcode_event {
  netcode_event_kind kind = netcode_event_kind::message; ///< Loại.
  u32 player = 0;      ///< Người chơi liên quan (id do server cấp; 0 là server).
  u32 code = 0;        ///< `left`: lý do (netcode_code_version, netcode_code_full, 0 là mất liên lạc...).
  std::string data;    ///< `message`: các byte.
  entt::entity entity = entt::null; ///< `spawned`, `despawned`: thực thể trên máy này.
  u32 net_id = 0;      ///< `spawned`, `despawned`: id chung của thực thể trên mọi máy.
};

/// Số liệu của phiên.
struct netcode_stats {
  u32 tick = 0;              ///< Tick đang mô phỏng (netcode_tick()).
  f32 render_tick = 0.0f;    ///< Máy khách: tick (của server) đang được vẽ cho thực thể nội suy.
  i32 input_margin = 0;      ///< Máy khách: input đến server sớm mấy tick (âm là muộn).
  f32 fixed_rate = 1.0f;     ///< Máy khách: nhịp tick đang chạy (time_fixed_rate()).
  u32 missing_inputs = 0;    ///< Server: số lần input của một máy khách chưa đến kịp tick của nó.
  u32 snapshot_bytes = 0;    ///< Cỡ snapshot gần nhất (server: lớn nhất trong các máy khách).
  u32 entities = 0;          ///< Số thực thể replicate trên máy này.
  f32 rtt = 0.0f;            ///< Máy khách: ping tới server, giây.
};

/// Bắt đầu làm server ở `port`. Server mô phỏng thế giới thật; máy khách gửi input và
/// nhận lại trạng thái. Mỗi máy chỉ chạy một phiên.
/// @param ctx Context của engine.
/// @param port Cổng UDP.
/// @param opts Cách chạy.
/// @return `false` nếu cổng đã bị chiếm hay đang chạy một phiên.
bool netcode_host(context &ctx, u16 port, const netcode_opts &opts = {});

/// Vào server ở `address:port`. Kết quả đến qua netcode_poll(): `joined`, hoặc `left`.
/// @param ctx Context của engine.
/// @param address IP của server.
/// @param port Cổng.
/// @param opts Cách chạy.
/// @return `false` nếu địa chỉ sai hay đang chạy một phiên.
bool netcode_join(context &ctx, const char *address, u16 port, const netcode_opts &opts = {});

/// Dừng phiên: ngắt mọi máy, hủy mọi thực thể replicate trên máy khách, trả nhịp tick
/// về 1. Cách đăng ký component (netcode_component()) được giữ lại.
/// @param ctx Context của engine.
void netcode_stop(context &ctx);

/// Vai của máy này. @param ctx Context của engine. @return Vai.
netcode_role netcode_role_of(const context &ctx);

/// Tick đang mô phỏng. Tăng một ở đầu mỗi `phase_fixed_update`. Trên máy khách đã
/// đồng bộ, nó chạy trước server một chút để input kịp đến.
/// @param ctx Context của engine.
/// @return Tick.
u32 netcode_tick(const context &ctx);

/// Máy khách đã nhận tick từ server chưa (trước đó input chưa được gửi).
/// @param ctx Context của engine.
/// @return `true` trên server, và trên máy khách đã đồng bộ.
bool netcode_synced(const context &ctx);

/// Id của người chơi này: id server cấp cho máy khách này, 0 trên server.
/// @param ctx Context của engine.
/// @return Id.
u32 netcode_self(const context &ctx);

/// Đặt input của máy này cho tick hiện tại (cộng `input_delay`). Gọi trong
/// `phase_fixed_update`, mỗi tick một lần. Nên nhỏ: dùng bit_writer.
/// @param ctx Context của engine.
/// @param input Các byte, tối đa 255.
void netcode_set_input(context &ctx, std::string_view input);

/// Input của một người chơi cho tick hiện tại. Server đọc input của từng máy khách;
/// mọi máy đọc input của chính mình bằng `player` 0 (để tự mô phỏng nhân vật của
/// mình ngay, không chờ server).
/// @param ctx Context của engine.
/// @param player Người chơi; 0 là máy này.
/// @param out Input. Khi input của tick này chưa đến thì là input gần nhất trước đó.
/// @return `true` nếu đúng là input của tick này; `false` nếu là input cũ lặp lại hay
/// chưa có gì (`out` rỗng).
bool netcode_input(context &ctx, u32 player, std::string &out);

/// Đánh dấu một thực thể để replicate tới máy khách (server). Mọi component đã đăng
/// ký bằng netcode_component() mà nó có sẽ được gửi đi; thêm hay bỏ component sau đó
/// cũng vậy. Hủy thực thể là hủy luôn trên máy khách.
/// @param ctx Context của engine.
/// @param entity Thực thể.
/// @param owner Người chơi điều khiển nó (0 là không ai): trên máy đó các component
/// `predicted` không bị ghi đè, để game tự mô phỏng.
/// @return Id chung trên mọi máy, 0 nếu không phải server.
u32 netcode_replicate(context &ctx, entt::entity entity, u32 owner = 0);

/// Thôi replicate một thực thể (server): máy khách hủy bản của nó, thực thể trên
/// server vẫn còn.
/// @param ctx Context của engine.
/// @param entity Thực thể.
void netcode_unreplicate(context &ctx, entt::entity entity);

/// Id chung của một thực thể replicate. @param ctx Context của engine.
/// @param entity Thực thể. @return Id, 0 nếu không replicate.
u32 netcode_id(const context &ctx, entt::entity entity);

/// Thực thể trên máy này có id chung `net_id`. @param ctx Context của engine.
/// @param net_id Id. @return Thực thể, hoặc `entt::null`.
entt::entity netcode_entity(const context &ctx, u32 net_id);

/// Người chơi điều khiển một thực thể replicate. @param ctx Context của engine.
/// @param entity Thực thể. @return Id người chơi, 0 nếu không ai.
u32 netcode_owner(const context &ctx, entt::entity entity);

/// Thực thể có do máy này điều khiển không. @param ctx Context của engine.
/// @param entity Thực thể. @return `true` nếu owner là netcode_self().
bool netcode_is_mine(const context &ctx, entt::entity entity);

/// Server: hàm quyết định một thực thể có được gửi tới một người chơi không (ví dụ
/// chỉ thứ ở gần). Thực thể của chính người chơi đó luôn được gửi. `nullptr` là gửi
/// tất cả.
/// @param ctx Context của engine.
/// @param fn `fn(ctx, player, entity)`.
void netcode_set_relevance(context &ctx, std::function<bool(context &, u32, entt::entity)> fn);

/// Server: độ ưu tiên của một thực thể với một người chơi (mặc định 1). Mỗi snapshot
/// thực thể đã đổi mà chưa được gửi cộng dồn độ ưu tiên; snapshot gửi thực thể có tổng
/// cao nhất trước cho tới hết `snapshot_budget`. Thứ ở gần nên cao hơn thứ ở xa.
/// @param ctx Context của engine.
/// @param fn `fn(ctx, player, entity)`.
void netcode_set_priority(context &ctx, std::function<f32(context &, u32, entt::entity)> fn);

/// Gửi một message của game. Máy khách gửi tới server (`player` bị bỏ qua); server
/// gửi tới người chơi `player`.
/// @param ctx Context của engine.
/// @param player Người nhận (server).
/// @param data Các byte.
/// @param delivery Cách giao.
/// @return `false` nếu không gửi được.
bool netcode_send(context &ctx, u32 player, std::string_view data, net_delivery delivery = net_delivery::reliable);

/// Server: gửi một message tới mọi người chơi.
/// @param ctx Context của engine.
/// @param data Các byte.
/// @param delivery Cách giao.
void netcode_send_all(context &ctx, std::string_view data, net_delivery delivery = net_delivery::reliable);

/// Đọc việc tiếp theo của phiên. Gọi lặp tới khi trả về `false`.
/// @param ctx Context của engine.
/// @param out Việc.
/// @return `true` nếu có việc.
bool netcode_poll(context &ctx, netcode_event &out);

/// Số liệu của phiên. @param ctx Context của engine. @return Số liệu.
netcode_stats netcode_info(const context &ctx);

/// Cách replicate một loại component, cho netcode_component().
template <class T> struct netcode_component_desc {
  /// Ghi component. Dùng bit_writer với độ chính xác vừa đủ.
  void (*write)(const T &value, bit_writer &w) = nullptr;
  /// Đọc lại đúng những gì `write` ghi. `value` là giá trị đang có (hay T{}).
  void (*read)(T &value, bit_reader &r) = nullptr;
  /// Nếu có: trên máy khách, thực thể của người khác được vẽ mượt bằng cách nội suy
  /// giữa hai snapshot (t từ 0 đến 1) thay vì nhảy theo từng snapshot.
  T (*lerp)(const T &a, const T &b, f32 t) = nullptr;
  /// Trên máy điều khiển thực thể, server không ghi đè component này: game tự mô phỏng
  /// nó ngay theo input. Giá trị server xác nhận đọc bằng netcode_confirmed().
  bool predicted = false;
};

/// @cond NJIN_INTERNAL
struct netcode_component_erased {
  u32 type = 0;
  std::function<bool(const entt::registry &, entt::entity, bit_writer &)> write;
  std::function<void(entt::registry &, entt::entity, std::string_view)> read;
  std::function<void(entt::registry &, entt::entity)> remove;
  std::function<void(entt::registry &, entt::entity, std::string_view, std::string_view, f32)> lerp;
  bool predicted = false;
};
void netcode_register_component(context &ctx, netcode_component_erased desc);
bool netcode_confirmed_bytes(const context &ctx, entt::entity entity, u32 type, std::string &out, u32 &tick);
/// @endcond

/// Đăng ký một loại component để replicate. Gọi trên **mọi máy, cùng thứ tự**, trước
/// netcode_host() hay netcode_join() (thường trong `setup` của module). Tối đa 31 loại.
/// @code
/// struct pos { njin::vec2 v; };
/// njin::netcode_component<pos>(ctx, {
///   .write = [](const pos &p, njin::bit_writer &w) { w.write_vec2(p.v, -2000, 2000, 0.01f); },
///   .read = [](pos &p, njin::bit_reader &r) { p.v = r.read_vec2(-2000, 2000, 0.01f); },
///   .lerp = [](const pos &a, const pos &b, njin::f32 t) { return pos{a.v + (b.v - a.v) * t}; },
/// });
/// @endcode
/// @param ctx Context của engine.
/// @param desc Cách ghi, đọc, nội suy.
template <class T> void netcode_component(context &ctx, const netcode_component_desc<T> &desc) {
  netcode_component_erased e;
  e.type = entt::type_hash<T>::value();
  e.predicted = desc.predicted;
  const auto write = desc.write;
  const auto read = desc.read;
  e.write = [write](const entt::registry &reg, entt::entity en, bit_writer &w) {
    const T *c = reg.try_get<T>(en);
    if (!c)
      return false;
    write(*c, w);
    return true;
  };
  e.read = [read](entt::registry &reg, entt::entity en, std::string_view bytes) {
    T value{};
    if (const T *c = reg.try_get<T>(en))
      value = *c;
    bit_reader r(bytes);
    read(value, r);
    if (r.ok())
      reg.emplace_or_replace<T>(en, value);
  };
  e.remove = [](entt::registry &reg, entt::entity en) { reg.remove<T>(en); };
  if (const auto lerp = desc.lerp)
    e.lerp = [read, lerp](entt::registry &reg, entt::entity en, std::string_view a, std::string_view b, f32 t) {
      T va{}, vb{};
      bit_reader ra(a), rb(b);
      read(va, ra);
      read(vb, rb);
      if (ra.ok() && rb.ok())
        reg.emplace_or_replace<T>(en, lerp(va, vb, t));
    };
  netcode_register_component(ctx, std::move(e));
}

/// Giá trị mới nhất server xác nhận của một component `predicted` trên thực thể của
/// máy này: so với giá trị game tự mô phỏng để sửa khi lệch.
/// @param ctx Context của engine.
/// @param entity Thực thể.
/// @param out Giá trị.
/// @param tick Tick của server mà giá trị thuộc về.
/// @param read Hàm đọc, như `netcode_component_desc::read`.
/// @return `false` nếu chưa có.
template <class T>
bool netcode_confirmed(const context &ctx, entt::entity entity, T &out, u32 &tick, void (*read)(T &, bit_reader &)) {
  std::string bytes;
  if (!netcode_confirmed_bytes(ctx, entity, entt::type_hash<T>::value(), bytes, tick))
    return false;
  bit_reader r(bytes);
  T value{};
  read(value, r);
  if (!r.ok())
    return false;
  out = value;
  return true;
}

/// @}
} // namespace njin
