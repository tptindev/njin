// Máy trạng thái và hẹn giờ: hai thứ mọi game đều cần.
#include <cstdio>
#include <functional>
#include <vector>

// --- Máy trạng thái: một enum và một chỗ duy nhất đổi trạng thái ---
enum class state { idle, run, jump };

const char *name(state s) {
  switch (s) {
  case state::idle: return "idle";
  case state::run: return "run";
  case state::jump: return "jump";
  }
  return "?";
}

struct character {
  state current = state::idle;
  float time_in_state = 0.0f;

  // Mọi lần đổi trạng thái đi qua đây: chỗ duy nhất để làm việc lúc vào và ra.
  void change(state next) {
    if (next == current)
      return;
    std::printf("  %s -> %s (sau %.2f giây)\n", name(current), name(next), time_in_state);
    current = next;
    time_in_state = 0.0f;
  }

  void update(float dt, bool on_ground, float move_x) {
    time_in_state += dt;
    if (!on_ground)
      change(state::jump);
    else if (move_x != 0.0f)
      change(state::run);
    else
      change(state::idle);
  }
};

// --- Hẹn giờ: "sau N giây thì làm việc này" mà không cần một biến đếm riêng ---
struct timers {
  struct item {
    float left;
    std::function<void()> fn;
  };
  std::vector<item> items;

  void after(float seconds, std::function<void()> fn) { items.push_back({seconds, std::move(fn)}); }

  void update(float dt) {
    // Gom những hẹn giờ đã đến hạn trước, rồi mới gọi: hàm được gọi có thể tạo hẹn giờ mới.
    std::vector<std::function<void()>> due;
    for (item &t : items)
      t.left -= dt;
    for (std::size_t i = 0; i < items.size();) {
      if (items[i].left <= 0.0f) {
        due.push_back(std::move(items[i].fn));
        items.erase(items.begin() + (std::ptrdiff_t)i);
      } else {
        i++;
      }
    }
    for (auto &fn : due)
      fn();
  }
};

int main() {
  character hero;
  timers clock;
  float t = 0.0f;
  clock.after(0.25f, [&] { std::printf("  [%.2f] bắt đầu chạy\n", t); });
  clock.after(0.75f, [&] {
    std::printf("  [%.2f] hẹn giờ trong hẹn giờ: 0.25 giây nữa nhảy\n", t);
    clock.after(0.25f, [&] { std::printf("  [%.2f] nhảy!\n", t); });
  });

  const float dt = 0.0625f; // 1/16: chia chính xác trong số thực nhị phân, nên đếm giờ không lệch
  for (int frame = 0; frame < 24; frame++) { // 24 frame = 1,5 giây
    t += dt;
    const bool running = t >= 0.25f;
    const bool in_air = t >= 1.0f && t < 1.25f;
    hero.update(dt, !in_air, running ? 1.0f : 0.0f);
    clock.update(dt);
  }
}
