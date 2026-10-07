// Events (observer) and actions (separating "which key is pressed" from "what the player wants to do").
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

// --- Event: the sender does not need to know who is listening ---
struct enemy_died {
  int id;
  int score;
};

struct bus {
  std::vector<std::function<void(const enemy_died &)>> listeners;
  std::vector<enemy_died> queue; // queued events, waiting to be sent

  void listen(std::function<void(const enemy_died &)> f) { listeners.push_back(std::move(f)); }
  void enqueue(enemy_died e) { queue.push_back(e); } // queue it: safe while walking the enemy list

  // Send them all in one go, at a fixed point in the frame.
  void flush() {
    std::vector<enemy_died> now;
    now.swap(queue);
    for (const enemy_died &e : now)
      for (auto &f : listeners)
        f(e);
  }
};

// --- Action: the game asks "is jump pressed", not "is the Space key pressed" ---
enum class key { space, w, up, left, a };

struct actions {
  std::map<std::string, std::vector<key>> binds;
  std::vector<key> down; // the keys held down this frame

  void bind(const std::string &action, std::vector<key> keys) { binds[action] = std::move(keys); }

  bool pressed(const std::string &action) const {
    const auto it = binds.find(action);
    if (it == binds.end())
      return false;
    for (const key k : it->second)
      for (const key d : down)
        if (k == d)
          return true;
    return false;
  }
};

int main() {
  int score = 0;
  bus events;
  events.listen([&](const enemy_died &e) { score += e.score; });
  events.listen([](const enemy_died &e) { std::printf("  sound: enemy %d died\n", e.id); });

  events.enqueue({1, 100});
  events.enqueue({2, 250});
  std::printf("before flush: score = %d\n", score);
  events.flush();
  std::printf("after flush:  score = %d\n", score);

  actions in;
  in.bind("jump", {key::space, key::w, key::up}); // changing keys only edits this one line
  in.down = {key::w};
  std::printf("press W:       jump = %s\n", in.pressed("jump") ? "yes" : "no");
  in.down = {key::left};
  std::printf("press left arrow: jump = %s\n", in.pressed("jump") ? "yes" : "no");
  in.bind("jump", {key::left}); // the player rebinds the key
  std::printf("after rebinding: jump = %s\n", in.pressed("jump") ? "yes" : "no");
}
