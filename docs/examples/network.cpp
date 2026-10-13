#include <njin.h>
#include <vector>

// Một phòng chơi nhỏ: máy chủ phát lại mọi message cho mọi người, máy khách gửi
// vị trí của mình mỗi frame. Cùng một module chạy được cả hai vai.
namespace {
constexpr njin::u16 port = 7777;
bool hosting = false;
njin::net_server server;
njin::net_conn to_server;              // máy khách: kết nối tới máy chủ
std::vector<njin::net_conn> players;   // máy chủ: mọi máy đã vào

void on_connected(const njin::net_connected &e) {
  if (e.server.id != 0) // một máy vừa vào phòng của ta
    players.push_back(e.conn);
  NJIN_INFO("đã nối với %s:%u", e.remote.host.c_str(), (unsigned)e.remote.port);
}

void on_disconnected(const njin::net_disconnected &e) {
  std::erase_if(players, [&](njin::net_conn c) { return c.id == e.conn.id; });
  NJIN_INFO("mất kết nối: %s", e.reason.c_str());
}

void on_failed(const njin::net_connect_failed &e) { NJIN_WARN("không vào được phòng: %s", e.reason.c_str()); }

// Nhận ctx làm tham số đầu: nối bằng connect<&on_message>(ctx) bên dưới.
void on_message(njin::context &ctx, const njin::net_message &e) {
  if (!hosting)
    return; // máy khách: e.data là trạng thái của người khác
  for (const njin::net_conn c : players)
    if (c.id != e.conn.id)
      njin::tcp_send(ctx, c, e.data);
}

void send_position(njin::context &ctx) {
  if (!njin::tcp_connected(ctx, to_server))
    return;
  njin::json_value state;
  state.set("x", 120.0).set("y", 64.0);
  njin::tcp_send(ctx, to_server, njin::json_dump(state, false));
}

void setup(njin::context &ctx) {
  if (hosting)
    server = njin::tcp_listen(ctx, port);
  else
    to_server = njin::tcp_connect(ctx, "127.0.0.1", port);
  njin::events(ctx).sink<njin::net_connected>().connect<&on_connected>();
  njin::events(ctx).sink<njin::net_disconnected>().connect<&on_disconnected>();
  njin::events(ctx).sink<njin::net_connect_failed>().connect<&on_failed>();
  njin::events(ctx).sink<njin::net_message>().connect<&on_message>(ctx);
  njin::ecs_register(ctx, njin::phase_update, send_position, "send_position");
}
} // namespace

njin::mod_desc lobby_module(bool host) {
  hosting = host;
  return {.name = "lobby", .setup = setup};
}
