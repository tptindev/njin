// Event (observer) và action (tách "bấm phím gì" khỏi "muốn làm gì").
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

// --- Event: người gửi không cần biết ai nghe ---
struct enemy_died {
  int id;
  int score;
};

struct bus {
  std::vector<std::function<void(const enemy_died &)>> listeners;
  std::vector<enemy_died> queue; // event xếp hàng, chờ phát

  void listen(std::function<void(const enemy_died &)> f) { listeners.push_back(std::move(f)); }
  void enqueue(enemy_died e) { queue.push_back(e); } // xếp hàng: an toàn khi đang duyệt danh sách quái

  // Phát tất cả một lượt, ở một chỗ cố định trong frame.
  void flush() {
    std::vector<enemy_died> now;
    now.swap(queue);
    for (const enemy_died &e : now)
      for (auto &f : listeners)
        f(e);
  }
};

// --- Action: game hỏi "jump có bấm không", không hỏi "phím Space" ---
enum class key { space, w, up, left, a };

struct actions {
  std::map<std::string, std::vector<key>> binds;
  std::vector<key> down; // các phím đang bấm ở frame này

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
  events.listen([](const enemy_died &e) { std::printf("  âm thanh: quái %d chết\n", e.id); });

  events.enqueue({1, 100});
  events.enqueue({2, 250});
  std::printf("trước flush: điểm = %d\n", score);
  events.flush();
  std::printf("sau flush:   điểm = %d\n", score);

  actions in;
  in.bind("jump", {key::space, key::w, key::up}); // đổi phím chỉ sửa một dòng này
  in.down = {key::w};
  std::printf("bấm W:       jump = %s\n", in.pressed("jump") ? "có" : "không");
  in.down = {key::left};
  std::printf("bấm mũi tên trái: jump = %s\n", in.pressed("jump") ? "có" : "không");
  in.bind("jump", {key::left}); // người chơi đổi phím
  std::printf("sau khi đổi phím: jump = %s\n", in.pressed("jump") ? "có" : "không");
}
