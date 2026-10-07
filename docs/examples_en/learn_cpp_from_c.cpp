// Compile: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_from_c.cpp -o learn
#include <cstdio>
#include <string>
#include <vector>

struct vec2 {
  float x, y;
};

// --- 1. References: change an argument without a pointer ---
void move_by_pointer(vec2 *pos, float dx) { pos->x += dx; }  // C style
void move_by_ref(vec2 &pos, float dx) { pos.x += dx; }       // C++ style

// --- 2. const: promise "read only", and no copy ---
float length_squared(const vec2 &v) { return v.x * v.x + v.y * v.y; }

// --- 3. Namespaces: group names together ---
namespace game {
int lives = 3;
namespace ui {
const char *title() { return "Menu"; }
} // namespace ui
} // namespace game

// --- 4. struct with default values and member functions ---
struct transform {
  vec2 pos{};
  float rot = 0.0f;
  float scale = 1.0f;

  void translate(float dx, float dy) {
    pos.x += dx;
    pos.y += dy;
  }
  bool at_origin() const { return pos.x == 0.0f && pos.y == 0.0f; } // const: does not change the transform
};

// --- 5. Overloading and default arguments ---
void show(int v) { std::printf("show(int) = %d\n", v); }
void show(float v) { std::printf("show(float) = %.1f\n", v); }
void show(const char *v) { std::printf("show(const char *) = %s\n", v); }

int spawn_calls = 0;
void spawn(float zoom = 1.0f, vec2 pos = {}) {
  spawn_calls++;
  std::printf("spawn(zoom=%.1f, pos=%.0f,%.0f)\n", zoom, pos.x, pos.y);
}

// --- 6. nullptr: "nothing there" ---
const int *find_value(const std::vector<int> &values, int wanted) {
  for (const int &v : values)
    if (v == wanted)
      return &v;
  return nullptr;
}

// --- 7. Anonymous namespace: only this file sees it ---
namespace {
int helper() { return 42; }
} // namespace

int main() {
  std::printf("[1] references\n");
  vec2 a{1.0f, 2.0f};
  move_by_pointer(&a, 10.0f); // must take the address
  move_by_ref(a, 100.0f);     // call directly
  std::printf("a.x = %.0f\n", a.x);

  std::printf("[2] const\n");
  const vec2 b{3.0f, 4.0f};
  std::printf("length_squared = %.0f\n", length_squared(b));

  std::printf("[3] namespace\n");
  std::printf("%d %s\n", game::lives, game::ui::title());
  {
    using namespace game; // only inside this block
    std::printf("lives = %d\n", lives);
  }

  std::printf("[4] struct\n");
  transform t;
  t.translate(5.0f, 0.0f);
  std::printf("scale = %.1f, at_origin = %s\n", t.scale, t.at_origin() ? "true" : "false");

  std::printf("[5] overloading and default arguments\n");
  show(7);
  show(7.5f);
  show("seven");
  spawn();
  spawn(3.0f);
  spawn(2.0f, {10.0f, 20.0f});

  std::printf("[6] nullptr\n");
  const std::vector<int> scores{10, 20, 30};
  const int *hit = find_value(scores, 20);
  const int *miss = find_value(scores, 99);
  std::printf("hit = %d, miss %s\n", *hit, miss == nullptr ? "is nullptr" : "found");

  std::printf("[7] string and vector\n");
  std::string name = "hero";
  name += "_1";
  std::vector<std::string> names{name, "slime"};
  names.push_back("bat");
  for (const std::string &n : names)
    std::printf("%s (%zu characters)\n", n.c_str(), n.size());

  std::printf("helper = %d\n", helper());
  return 0;
}
