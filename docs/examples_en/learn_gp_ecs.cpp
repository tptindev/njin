// A tiny ECS, to see what entity, component and system really are.
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

using entity = std::uint32_t; // an entity is just a number

// Each component type has a store: entity -> data.
template <class T> struct pool {
  std::unordered_map<entity, T> items;
};

struct registry {
  entity next = 1; // 0 is reserved for "no entity"
  std::vector<entity> alive;

  // The store for type T. Each T has exactly one store, living inside this function.
  template <class T> pool<T> &of() {
    static pool<T> p; // enough for a small example; a real registry keeps its stores inside itself
    return p;
  }

  entity create() {
    alive.push_back(next);
    return next++;
  }
  template <class T> T &emplace(entity e, T value) { return of<T>().items[e] = value; }
  template <class T> T &get(entity e) { return of<T>().items.at(e); }
  template <class T> bool has(entity e) { return of<T>().items.count(e) != 0; }

  // Call f for every entity that has both components A and B.
  template <class A, class B, class F> void each(F f) {
    for (const entity e : alive)
      if (has<A>(e) && has<B>(e))
        f(e, get<A>(e), get<B>(e));
  }
};

// Components: data only, no functions.
struct position { float x = 0, y = 0; };
struct velocity { float x = 0, y = 0; };
struct health { int hp = 3; };
struct enemy_tag {}; // an empty component used as a marker

// System: an ordinary function that walks the entities with the components it needs.
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

  const entity rock = reg.create(); // has a position but no velocity: stays put
  reg.emplace(rock, position{4, 4});

  const entity slime = reg.create();
  reg.emplace(slime, position{8, 0});
  reg.emplace(slime, velocity{-5, 0});
  reg.emplace(slime, health{2});
  reg.emplace(slime, enemy_tag{});

  move_system(reg, 0.5f); // half a second

  for (const entity e : reg.alive) {
    const position &p = reg.get<position>(e);
    std::printf("entity %u: (%.1f, %.1f)%s%s\n", e, p.x, p.y, reg.has<health>(e) ? " has health" : "",
                reg.has<enemy_tag>(e) ? " [enemy]" : "");
  }
}
