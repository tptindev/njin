#include <njin.h>
#include <cmath>
#include <unordered_map>

// Một đấu trường nhìn từ trên xuống: mỗi người chơi điều khiển một chấm. Server
// quyết định mọi thứ; máy khách gửi input, tự đoán chuyển động của chính mình và
// vẽ người khác bằng nội suy. Cùng module chạy được cả hai vai.
namespace {
struct pos {
  njin::vec2 v;
};

constexpr njin::f32 speed = 200.0f; // đơn vị mỗi giây

void write_pos(const pos &p, njin::bit_writer &w) { w.write_vec2(p.v, -2000.0f, 2000.0f, 0.01f); }
void read_pos(pos &p, njin::bit_reader &r) { p.v = r.read_vec2(-2000.0f, 2000.0f, 0.01f); }

// Input: hướng đi, mỗi trục -1..1 (2 bit).
njin::vec2 decode_move(const std::string &input) {
  njin::bit_reader r(input);
  const njin::vec2 move{(njin::f32)r.read_int(-1, 1), (njin::f32)r.read_int(-1, 1)};
  return r.ok() ? move : njin::vec2{0, 0}; // input hỏng hay chưa có: đứng yên
}

std::unordered_map<njin::u32, entt::entity> players; // server: người chơi -> nhân vật

void server_tick(njin::context &ctx) {
  njin::netcode_event e;
  while (njin::netcode_poll(ctx, e)) {
    entt::registry &reg = njin::world(ctx);
    if (e.kind == njin::netcode_event_kind::joined) {
      const entt::entity hero = reg.create();
      reg.emplace<pos>(hero);
      njin::netcode_replicate(ctx, hero, e.player); // người chơi này điều khiển nó
      players[e.player] = hero;
    } else if (e.kind == njin::netcode_event_kind::left) {
      reg.destroy(players[e.player]); // hủy ở server là hủy ở mọi máy khách
      players.erase(e.player);
    }
  }
  // Input của từng người cho đúng tick này (đã đến từ trước nhờ đồng bộ tick).
  for (const auto &[player, hero] : players) {
    std::string input;
    njin::netcode_input(ctx, player, input);
    njin::world(ctx).get<pos>(hero).v += decode_move(input) * (speed * njin::fixed_delta(ctx));
  }
}

void client_tick(njin::context &ctx) {
  if (!njin::netcode_synced(ctx))
    return;
  // Gửi input của tick này: phím trong game thật.
  njin::bit_writer w;
  w.write_int(1, -1, 1);
  w.write_int(0, -1, 1);
  njin::netcode_set_input(ctx, w.data());

  // Tự đoán chuyển động của mình ngay, không chờ server (pos là `predicted`).
  std::string input;
  njin::netcode_input(ctx, 0, input);
  for (auto [entity, p] : njin::world(ctx).view<pos>().each()) {
    if (!njin::netcode_is_mine(ctx, entity))
      continue; // người khác: netcode nội suy họ
    p.v += decode_move(input) * (speed * njin::fixed_delta(ctx));
    // Lệch xa so với server (bị đẩy, bị chặn) thì về theo server.
    pos confirmed;
    njin::u32 tick = 0;
    if (njin::netcode_confirmed(ctx, entity, confirmed, tick, read_pos) && njin::length(p.v - confirmed.v) > 50.0f)
      p.v = confirmed.v;
  }
}

void setup_common(njin::context &ctx) {
  // Mọi máy, cùng thứ tự, trước netcode_host()/netcode_join().
  njin::netcode_component<pos>(ctx, {
                                        .write = write_pos,
                                        .read = read_pos,
                                        .lerp = [](const pos &a, const pos &b, njin::f32 t) {
                                          return pos{a.v + (b.v - a.v) * t};
                                        },
                                        .predicted = true,
                                    });
}

bool hosting = false;

void setup(njin::context &ctx) {
  setup_common(ctx);
  if (hosting) {
    njin::netcode_host(ctx, 7777);
    // Chỉ gửi thứ ở gần mỗi người; thứ gần hơn được gửi trước khi băng thông chật.
    njin::netcode_set_relevance(ctx, [](njin::context &c, njin::u32 player, entt::entity e) {
      const auto it = players.find(player);
      return it == players.end() ||
             njin::length(njin::world(c).get<pos>(e).v - njin::world(c).get<pos>(it->second).v) < 800.0f;
    });
    njin::netcode_set_priority(ctx, [](njin::context &c, njin::u32 player, entt::entity e) {
      const auto it = players.find(player);
      const njin::f32 d = it == players.end() ? 0.0f
                                              : njin::length(njin::world(c).get<pos>(e).v -
                                                             njin::world(c).get<pos>(it->second).v);
      return 1.0f + 100.0f / (d + 10.0f);
    });
    njin::ecs_register(ctx, njin::phase_fixed_update, server_tick, "server_tick");
  } else {
    // Thử với mạng xấu ngay trên một máy.
    njin::netcode_join(ctx, "127.0.0.1", 7777, {.sim = {.latency = 0.08f, .jitter = 0.02f, .loss = 0.05f}});
    njin::ecs_register(ctx, njin::phase_fixed_update, client_tick, "client_tick");
  }
}
} // namespace

njin::mod_desc arena_netcode_module(bool host_game) {
  hosting = host_game;
  return {.name = "arena", .setup = setup};
}
