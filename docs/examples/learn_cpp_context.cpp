// Hai cách giữ trạng thái: biến toàn cục, hoặc một context truyền vào hàm.
// Biên dịch: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_context.cpp -o context
#include <cstdio>
#include <vector>

// --- Cách 1: biến toàn cục. Chỉ có MỘT bộ trạng thái trong cả chương trình ---
int g_frame = 0;
std::vector<int> g_events;

void global_tick() {
  g_frame++;
  g_events.push_back(g_frame * 10);
}

// --- Cách 2: context. Trạng thái nằm trong một đối tượng, hàm nhận nó ---
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
  std::printf("toan cuc: frame = %d, %zu su kien\n", g_frame, g_events.size());

  Ctx game;   // một game
  Ctx replay; // một bản chạy lại, độc lập
  tick(game);
  tick(game);
  tick(game);
  tick(replay);
  std::printf("game:   frame = %d, su kien cuoi = %d\n", game.frame, last_event(game));
  std::printf("replay: frame = %d, su kien cuoi = %d\n", replay.frame, last_event(replay));
  return 0;
}
