// Two ways to keep state: global variables, or a context passed to functions.
// Compile: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_context.cpp -o context
#include <cstdio>
#include <vector>

// --- Way 1: global variables. There is only ONE set of state in the whole program ---
int g_frame = 0;
std::vector<int> g_events;

void global_tick() {
  g_frame++;
  g_events.push_back(g_frame * 10);
}

// --- Way 2: a context. The state lives in an object, and functions take it ---
struct Ctx {
  int frame = 0;
  std::vector<int> events;
};

void tick(Ctx &ctx) {
  ctx.frame++;
  ctx.events.push_back(ctx.frame * 10);
}

int last_event(const Ctx &ctx) { return ctx.events.empty() ? -1 : ctx.events.back(); }

int main() {
  global_tick();
  global_tick();
  std::printf("global: frame = %d, %zu events\n", g_frame, g_events.size());

  Ctx game;   // a game
  Ctx replay; // a replay, independent
  tick(game);
  tick(game);
  tick(game);
  tick(replay);
  std::printf("game:   frame = %d, last event = %d\n", game.frame, last_event(game));
  std::printf("replay: frame = %d, last event = %d\n", replay.frame, last_event(replay));
  return 0;
}
