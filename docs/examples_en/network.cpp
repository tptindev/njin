#include <njin.h>
#include <vector>

// A small lobby room: the server rebroadcasts every message to everyone, the client
// sends its own position every frame. The same module runs either role.
namespace {
constexpr njin::u16 port = 7777;
bool hosting = false;
njin::net_server server;
njin::net_conn to_server;              // client: connection to the server
std::vector<njin::net_conn> players;   // server: every machine that has joined

void on_connected(const njin::net_connected &e) {
  if (e.server.id != 0) // a machine just joined our room
    players.push_back(e.conn);
  NJIN_INFO("connected to %s:%u", e.remote.host.c_str(), (unsigned)e.remote.port);
}

void on_disconnected(const njin::net_disconnected &e) {
  std::erase_if(players, [&](njin::net_conn c) { return c.id == e.conn.id; });
  NJIN_INFO("lost connection: %s", e.reason.c_str());
}

void on_failed(const njin::net_connect_failed &e) { NJIN_WARN("could not join room: %s", e.reason.c_str()); }

// Takes ctx as its first parameter: connected below with connect<&on_message>(ctx).
void on_message(njin::context &ctx, const njin::net_message &e) {
  if (!hosting)
    return; // client: e.data is another player's state
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
