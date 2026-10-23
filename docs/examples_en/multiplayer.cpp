#include <njin.h>
#include <unordered_map>

// A server holds every player's position. Each tick, a client sends input (an
// unreliable packet: lose one and the next tick brings a new one), the server sends
// everyone's position back. The same module runs either role.
namespace {
constexpr njin::u16 port = 7777;
constexpr njin::u8 ch_state = 0; // input and position, sent continuously
// Channel 1 is reserved for things that must not be lost (chat, deaths...), sent
// `reliable`: on its own channel, a lost chat packet doesn't hold back the position
// on channel 0.

bool hosting = false;
njin::net_host host;
njin::net_peer server; // client: the server
std::unordered_map<njin::u32, njin::vec2> players; // peer id -> position

// Packs input: 2 numbers in -1..1, 2 bits each.
std::string pack_input(njin::vec2 move) {
  njin::bit_writer w;
  w.write_int((njin::i32)move.x, -1, 1);
  w.write_int((njin::i32)move.y, -1, 1);
  return w.data();
}

void server_tick(njin::context &ctx) {
  njin::net_host_event e;
  // Read right in fixed_update: input that arrives is used in this tick.
  while (njin::net_host_poll(ctx, host, e)) {
    if (e.kind == njin::net_host_event_kind::connected)
      players[e.peer.id] = {0, 0};
    else if (e.kind == njin::net_host_event_kind::disconnected)
      players.erase(e.peer.id);
    else if (e.channel == ch_state) {
      njin::bit_reader r(e.data);
      const njin::vec2 move{(njin::f32)r.read_int(-1, 1), (njin::f32)r.read_int(-1, 1)};
      if (r.ok()) // corrupt or malicious packet: drop it
        players[e.peer.id] = players[e.peer.id] + move * (200.0f * njin::fixed_delta(ctx));
    }
  }
  // Everyone's position, 1 cm within ±2000: 18 bits per number instead of 32.
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
  const njin::vec2 move{0, 1}; // from the keyboard, in a real game
  njin::net_peer_send(ctx, server, pack_input(move), njin::net_delivery::unreliable, ch_state);
  njin::net_host_flush(ctx, host); // send right away, without waiting for the end of the frame
}

void setup(njin::context &ctx) {
  if (hosting) {
    host = njin::net_host_create(ctx, {.port = port, .channels = 2});
    njin::ecs_register(ctx, njin::phase_fixed_update, server_tick, "server_tick");
  } else {
    // Test with a bad network right on one machine: 80 ms latency, 5% loss.
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
