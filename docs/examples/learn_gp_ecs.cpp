// Một ECS thu nhỏ, để thấy entity, component và system thật ra là gì.
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

using entity = std::uint32_t; // một entity chỉ là một con số

// Mỗi loại component có một kho: entity -> dữ liệu.
template <class T> struct pool {
  std::unordered_map<entity, T> items;
};

struct registry {
  entity next = 1; // 0 để dành cho "không có entity"
  std::vector<entity> alive;

  // Kho của kiểu T. Mỗi T có đúng một kho, nằm trong hàm này.
  template <class T> pool<T> &of() {
    static pool<T> p; // đủ cho ví dụ nhỏ; một registry thật giữ kho trong chính nó
    return p;
  }

  entity create() {
    alive.push_back(next);
    return next++;
  }
  template <class T> T &emplace(entity e, T value) { return of<T>().items[e] = value; }
  template <class T> T &get(entity e) { return of<T>().items.at(e); }
  template <class T> bool has(entity e) { return of<T>().items.count(e) != 0; }

  // Gọi f cho mọi entity có đủ hai component A và B.
  template <class A, class B, class F> void each(F f) {
    for (const entity e : alive)
      if (has<A>(e) && has<B>(e))
        f(e, get<A>(e), get<B>(e));
  }
};

// Component: chỉ có dữ liệu, không có hàm.
struct position { float x = 0, y = 0; };
struct velocity { float x = 0, y = 0; };
struct health { int hp = 3; };
struct enemy_tag {}; // component rỗng dùng để đánh dấu

// System: một hàm thường, duyệt những entity có component nó cần.
void move_system(registry &reg, float dt) {
  reg.each<position, velocity>([&](entity, position &p, velocity &v) {
    p.x += v.x * dt;
    p.y += v.y * dt;
  });
}

int main() {
  registry reg;

  const entity player = reg.create();
  reg.emplace(player, position{0, 0});
  reg.emplace(player, velocity{10, 0});
  reg.emplace(player, health{5});

  const entity rock = reg.create(); // có vị trí nhưng không có velocity: đứng yên
  reg.emplace(rock, position{4, 4});

  const entity slime = reg.create();
  reg.emplace(slime, position{8, 0});
  reg.emplace(slime, velocity{-5, 0});
  reg.emplace(slime, health{2});
  reg.emplace(slime, enemy_tag{});

  move_system(reg, 0.5f); // nửa giây

  for (const entity e : reg.alive) {
    const position &p = reg.get<position>(e);
    std::printf("entity %u: (%.1f, %.1f)%s%s\n", e, p.x, p.y, reg.has<health>(e) ? " có máu" : "",
                reg.has<enemy_tag>(e) ? " [quái]" : "");
  }
}
