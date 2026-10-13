#include <njin.h>
#include <unordered_map>

// Một server giữ vị trí của mọi người chơi. Mỗi tick, máy khách gửi input (gói
// không tin cậy: mất một gói thì tick sau có gói mới), server gửi lại vị trí của
// tất cả. Cùng module chạy được cả hai vai.
namespace {
constexpr njin::u16 port = 7777;
constexpr njin::u8 ch_state = 0; // input và vị trí, gửi liên tục
// Kênh 1 để dành cho việc không được mất (chat, chết...), gửi `reliable`: nằm ở
// kênh riêng thì một gói chat bị mất không giữ chân vị trí ở kênh 0.

bool hosting = false;
njin::net_host host;
njin::net_peer server; // máy khách: server
std::unordered_map<njin::u32, njin::vec2> players; // id của peer -> vị trí

// Đóng gói input: 2 số trong -1..1, mỗi số 2 bit.
std::string pack_input(njin::vec2 move) {
  njin::bit_writer w;
  w.write_int((njin::i32)move.x, -1, 1);
  w.write_int((njin::i32)move.y, -1, 1);
  return w.data();
}

void server_tick(njin::context &ctx) {
  njin::net_host_event e;
  // Đọc ngay trong fixed_update: input đến là dùng trong tick này.
  while (njin::net_host_poll(ctx, host, e)) {
    if (e.kind == njin::net_host_event_kind::connected)
      players[e.peer.id] = {0, 0};
    else if (e.kind == njin::net_host_event_kind::disconnected)
      players.erase(e.peer.id);
    else if (e.channel == ch_state) {
      njin::bit_reader r(e.data);
      const njin::vec2 move{(njin::f32)r.read_int(-1, 1), (njin::f32)r.read_int(-1, 1)};
      if (r.ok()) // gói hỏng hay độc hại: bỏ
        players[e.peer.id] = players[e.peer.id] + move * (200.0f * njin::fixed_delta(ctx));
    }
  }
  // Vị trí của mọi người, 1 cm trong ±2000: 18 bit mỗi số thay vì 32.
  njin::bit_writer w;
  w.write_int((njin::i32)players.size(), 0, 64);
  for (const auto &[id, pos] : players) {
    w.write_varint(id);
    w.write_vec2(pos, -2000.0f, 2000.0f, 0.01f);
  }
  njin::net_host_broadcast(ctx, host, w.data(), njin::net_delivery::unreliable, ch_state);
}

void client_tick(njin::context &ctx) {
  njin::net_host_event e;
  while (njin::net_host_poll(ctx, host, e)) {
    if (e.kind != njin::net_host_event_kind::packet || e.channel != ch_state)
      continue;
    njin::bit_reader r(e.data);
    const njin::i32 count = r.read_int(0, 64);
    for (njin::i32 i = 0; i < count && r.ok(); ++i) {
      const auto id = (njin::u32)r.read_varint();
      players[id] = r.read_vec2(-2000.0f, 2000.0f, 0.01f);
    }
  }
  const njin::vec2 move{0, 1}; // từ bàn phím, trong game thật
  njin::net_peer_send(ctx, server, pack_input(move), njin::net_delivery::unreliable, ch_state);
  njin::net_host_flush(ctx, host); // gửi ngay, không chờ cuối frame
}

void setup(njin::context &ctx) {
  if (hosting) {
    host = njin::net_host_create(ctx, {.port = port, .channels = 2});
    njin::ecs_register(ctx, njin::phase_fixed_update, server_tick, "server_tick");
  } else {
    // Thử với mạng xấu ngay trên một máy: 80 ms trễ, mất 5% gói.
    host = njin::net_host_create(ctx, {.channels = 2, .sim = {.latency = 0.08f, .loss = 0.05f}});
    server = njin::net_host_connect(ctx, host, "127.0.0.1", port);
    njin::ecs_register(ctx, njin::phase_fixed_update, client_tick, "client_tick");
  }
}
} // namespace

njin::mod_desc arena_module(bool host_game) {
  hosting = host_game;
  return {.name = "arena", .setup = setup};
}
