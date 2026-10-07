// State machine and timers: two things every game needs.
#include <cstdio>
#include <functional>
#include <vector>

// --- State machine: an enum and a single place that changes the state ---
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

  // Every state change goes through here: the one place for enter and exit work.
  void change(state next) {
    if (next == current)
      return;
    std::printf("  %s -> %s (after %.2f seconds)\n", name(current), name(next), time_in_state);
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

// --- Timers: "after N seconds, do this" without a separate counter variable ---
struct timers {
  struct item {
    float left;
    std::function<void()> fn;
  };
  std::vector<item> items;

  void after(float seconds, std::function<void()> fn) { items.push_back({seconds, std::move(fn)}); }

  void update(float dt) {
    // Collect the timers that are due first, then call them: a called function may create new timers.
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
  clock.after(0.25f, [&] { std::printf("  [%.2f] start running\n", t); });
  clock.after(0.75f, [&] {
    std::printf("  [%.2f] timer inside a timer: jump in 0.25 seconds\n", t);
    clock.after(0.25f, [&] { std::printf("  [%.2f] jump!\n", t); });
  });

  const float dt = 0.0625f; // 1/16: exact in binary floating point, so the timing does not drift
  for (int frame = 0; frame < 24; frame++) { // 24 frames = 1.5 seconds
    t += dt;
    const bool running = t >= 0.25f;
    const bool in_air = t >= 1.0f && t < 1.25f;
    hero.update(dt, !in_air, running ? 1.0f : 0.0f);
    clock.update(dt);
  }
}
