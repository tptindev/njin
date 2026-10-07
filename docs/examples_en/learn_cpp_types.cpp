// Compile: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_types.cpp -o types
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct vec2 {
  float x, y;
};

// --- 1. Values and references ---
void section_values() {
  std::puts("[1] values and references");
  vec2 a{1.0f, 2.0f};
  vec2 copy = a;   // a copy: two independent variables
  vec2 &alias = a; // a reference: the same variable
  copy.x = 100.0f;
  alias.y = 50.0f;
  std::printf("a = %.0f,%.0f  copy = %.0f,%.0f\n", a.x, a.y, copy.x, copy.y);
}

// --- 2. Copy and move ---
void section_move() {
  std::puts("[2] copy and move");
  std::vector<int> a(1000, 7);
  const int *buffer = a.data();
  std::vector<int> copy = a;             // copies 1000 numbers into new memory
  std::vector<int> moved = std::move(a); // "takes" a's memory, copies nothing
  std::printf("copy uses new memory:   %s\n", copy.data() != buffer ? "yes" : "no");
  std::printf("moved takes old memory: %s\n", moved.data() == buffer ? "yes" : "no");
  std::printf("a.size() after the move = %zu\n", a.size());
}

// --- 3. RAII: a resource tied to the lifetime of a variable ---
struct Sound {
  std::string name;
  explicit Sound(const char *n) : name(n) { std::printf("  load %s\n", name.c_str()); }
  ~Sound() { std::printf("  unload %s\n", name.c_str()); }
  Sound(const Sound &) = delete; // a copy would free the same thing twice
  Sound &operator=(const Sound &) = delete;
};

bool play(bool fail_early) {
  Sound hit("hit.wav");
  if (fail_early)
    return false; // ~Sound still runs
  Sound bgm("bgm.ogg");
  std::puts("  playing");
  return true;
} // destroyed in reverse order of creation: bgm, then hit

void section_raii() {
  std::puts("[3] RAII");
  std::puts(" play(true):");
  play(true);
  std::puts(" play(false):");
  play(false);
}

// --- 4. Ownership: unique_ptr, or better, a value ---
struct Enemy {
  int hp;
  explicit Enemy(int h) : hp(h) { std::printf("  enemy %d appears\n", hp); }
  ~Enemy() { std::printf("  enemy %d gone\n", hp); }
};

struct Slime {
  int hp = 3;
};

void section_ownership() {
  std::puts("[4] ownership");
  {
    auto boss = std::make_unique<Enemy>(50);
    std::unique_ptr<Enemy> owner = std::move(boss); // transfer ownership
    std::printf("  boss is %s, owner has hp %d\n", boss ? "set" : "null", owner->hp);
  } // owner leaves the block: the Enemy is destroyed exactly once
  std::vector<Slime> wave(3); // values: no pointer needed
  wave[1].hp = 1;
  int total = 0;
  for (const Slime &s : wave)
    total += s.hp;
  std::printf("  total hp = %d\n", total);
}

// --- 5. Dangling references: the vector reallocates ---
void section_dangling() {
  std::puts("[5] dangling references");
  std::vector<int> scores{10, 20, 30};
  const std::uintptr_t before = reinterpret_cast<std::uintptr_t>(&scores[0]);
  const std::size_t capacity_before = scores.capacity();
  for (int i = 0; i < 100; i++)
    scores.push_back(i); // past capacity: the vector moves to new memory
  const std::uintptr_t after = reinterpret_cast<std::uintptr_t>(&scores[0]);
  std::printf("capacity %zu -> %zu\n", capacity_before, scores.capacity());
  std::printf("first element moved: %s\n", before != after ? "yes" : "no");
  // The safe way: keep an index, fetch the element again each time you need it.
  const std::size_t index = 0;
  std::printf("scores[index] = %d\n", scores[index]);
}

// --- 6. Three ways to say "maybe nothing" ---
std::optional<int> find_index(const std::vector<int> &v, int wanted) {
  for (std::size_t i = 0; i < v.size(); i++)
    if (v[i] == wanted)
      return static_cast<int>(i);
  return std::nullopt;
}

const int *find_ptr(const std::vector<int> &v, int wanted) {
  for (const int &x : v)
    if (x == wanted)
      return &x;
  return nullptr;
}

struct sound_handle {
  unsigned id = 0; // 0 means invalid
};

void section_maybe() {
  std::puts("[6] maybe nothing");
  const std::vector<int> v{10, 20, 30};
  const std::optional<int> i = find_index(v, 30);
  const std::optional<int> j = find_index(v, 99);
  std::printf("optional: %d, %s\n", i.value_or(-1), j.has_value() ? "has value" : "empty");
  const int *p = find_ptr(v, 20);
  const int *q = find_ptr(v, 99);
  std::printf("pointer:  %d, %s\n", *p, q == nullptr ? "nullptr" : "found");
  const sound_handle none{};
  std::printf("handle:   id %u is %s\n", none.id, none.id == 0 ? "invalid" : "valid");
}

int main() {
  section_values();
  section_move();
  section_raii();
  section_ownership();
  section_dangling();
  section_maybe();
  return 0;
}
