// Compile: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_modern.cpp -o modern
#include <any>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <typeindex>
#include <utility>
#include <vector>

struct vec2 {
  float x, y;
};

// --- 1. auto and range-for ---
void section_auto() {
  std::puts("[1] auto and range-for");
  std::vector<std::string> names{"hero", "slime", "bat"};
  auto count = names.size();          // deduced type: std::size_t
  auto ratio = 2.5f;                  // float
  auto copy = names[0];               // auto drops the reference: this is a COPY
  auto &alias = names[0];             // auto & keeps the reference
  copy += "!";
  alias += "?";
  std::printf("count=%zu ratio=%.1f names[0]=%s copy=%s\n", count, static_cast<double>(ratio),
              names[0].c_str(), copy.c_str());
  for (const auto &n : names)         // loop without copying each string
    std::printf("  %s\n", n.c_str());
}

// --- 2. Structured binding ---
struct Hit {
  int target;
  float damage;
};

void section_bindings() {
  std::puts("[2] structured binding");
  const Hit hit{7, 12.5f};
  const auto [target, damage] = hit; // split a struct
  std::printf("target=%d damage=%.1f\n", target, static_cast<double>(damage));

  std::map<std::string, int> hp{{"hero", 10}, {"slime", 3}};
  for (const auto &[name, value] : hp) // split each (key, value) pair
    std::printf("  %s has %d hp\n", name.c_str(), value);

  // Like EnTT's view<...>().each(): each row is (entity, references to the components).
  vec2 pos{0.0f, 0.0f};
  int life = 3;
  std::tuple<int, vec2 &, int &> row{1, pos, life};
  auto [entity, p, l] = row; // three names for three elements: entity, vec2 &, int &
  p.x += 5.0f;               // p is a reference to pos: it can be changed
  l -= 1;
  std::printf("entity=%d pos.x=%.0f life=%d\n", entity, static_cast<double>(pos.x), life);
}

// --- 3. Lambdas and std::function ---
struct Ctx {
  int frame = 0;
  std::vector<std::pair<int, std::function<void(Ctx &)>>> timers;
};

// Like njin::timer_after: takes ANYTHING callable, including a lambda with captures.
void after(Ctx &ctx, int frames, std::function<void(Ctx &)> fn) {
  ctx.timers.push_back({ctx.frame + frames, std::move(fn)});
}

void tick(Ctx &ctx) {
  ctx.frame++;
  std::vector<std::function<void(Ctx &)>> due;
  for (auto it = ctx.timers.begin(); it != ctx.timers.end();) {
    if (it->first <= ctx.frame) {
      due.push_back(std::move(it->second));
      it = ctx.timers.erase(it);
    } else {
      ++it;
    }
  }
  for (auto &fn : due)
    fn(ctx);
}

using sys_fnc = void (*)(Ctx &); // like njin::sys_fnc: a function pointer, no state

void bump_system(Ctx &ctx) { ctx.frame += 100; }

void section_lambda() {
  std::puts("[3] lambda and std::function");
  int bonus = 5;
  auto by_value = [bonus](int x) { return x + bonus; };
  auto by_ref = [&bonus](int x) { return x + bonus; };
  bonus = 100;
  std::printf("by_value(1)=%d by_ref(1)=%d\n", by_value(1), by_ref(1));

  Ctx ctx;
  const int lives = 3;
  after(ctx, 2, [](Ctx &c) { std::printf("  frame %d: respawn\n", c.frame); });
  after(ctx, 3, [lives](Ctx &c) { std::printf("  frame %d: lives=%d\n", c.frame, lives); });
  for (int i = 0; i < 4; i++)
    tick(ctx);

  sys_fnc sys = bump_system;                        // a plain function: fine
  sys_fnc no_capture = [](Ctx &c) { c.frame += 1; }; // a lambda WITHOUT captures: converts to a function pointer
  Ctx other;
  sys(other);
  no_capture(other);
  std::printf("other.frame = %d\n", other.frame);
}

// --- 4. Designated initializers (C++20) ---
struct Cfg {
  const char *title;
  float width;
  float height;
  float fps = 60.0f;
  bool resizable = false;
};

void section_designated() {
  std::puts("[4] designated initializers");
  const Cfg cfg{.title = "game", .width = 960.0f, .height = 540.0f, .resizable = true};
  std::printf("%s %.0fx%.0f fps=%.0f resizable=%s\n", cfg.title, static_cast<double>(cfg.width),
              static_cast<double>(cfg.height), static_cast<double>(cfg.fps),
              cfg.resizable ? "yes" : "no");
}

// --- 5. std::initializer_list and implicit conversion ---
enum Key { key_space, key_w };
enum PadButton { pad_a, pad_b };

struct Binding {
  int kind; // 0 = key, 1 = gamepad button
  int code;
  Binding(Key k) : kind(0), code(k) {}
  Binding(PadButton b) : kind(1), code(b) {}
};

void define(const char *name, std::initializer_list<Binding> sources) {
  std::printf("%s:", name);
  for (const Binding &b : sources)
    std::printf(" %s%d", b.kind == 0 ? "key" : "pad", b.code);
  std::puts("");
}

void section_initializer_list() {
  std::puts("[5] initializer_list");
  define("jump", {key_space, key_w, pad_a}); // keys and gamepad buttons in the SAME list
  define("pause", {pad_b});
}

// --- 6. constexpr ---
constexpr int max_voices = 8;
constexpr int square(int x) { return x * x; }
static_assert(square(4) == 16, "computed at compile time");
inline constexpr float pi = 3.14159265f; // in a header it must be inline constexpr

void section_constexpr() {
  std::puts("[6] constexpr");
  int table[square(3)]; // array size computed at compile time
  for (int i = 0; i < square(3); i++)
    table[i] = i;
  std::printf("max_voices=%d table=%zu elements, pi=%.2f\n", max_voices, sizeof(table) / sizeof(table[0]),
              static_cast<double>(pi));
}

// --- 7. enum and enum class ---
enum audio_bus { bus_master, bus_music, bus_sfx }; // names live outside, convert to int on their own
enum class ease { linear, in_quad };               // names live inside `ease::`, no automatic conversion to int

void section_enum() {
  std::puts("[7] enum and enum class");
  int a = bus_sfx;
  ease e = ease::in_quad;
  std::printf("bus_sfx=%d ease::in_quad=%d\n", a, static_cast<int>(e));
}

// --- 8. Templates: use them, do not write them ---
class Registry {
  std::map<std::type_index, std::map<int, std::any>> data_;

public:
  template <typename T> void emplace(int entity, T value) { data_[typeid(T)][entity] = std::move(value); }
  template <typename T> T &get(int entity) { return std::any_cast<T &>(data_.at(typeid(T)).at(entity)); }
};

struct Health {
  int hp;
};

void section_template() {
  std::puts("[8] template");
  Registry reg;
  reg.emplace(1, Health{3});         // T deduced from the argument: Health
  reg.emplace<vec2>(1, {4.0f, 5.0f}); // T written out in <>
  reg.get<Health>(1).hp -= 1;         // get has no argument that tells T: <Health> is required
  std::printf("hp=%d pos=%.0f,%.0f\n", reg.get<Health>(1).hp, static_cast<double>(reg.get<vec2>(1).x),
              static_cast<double>(reg.get<vec2>(1).y));
}

// --- 9. std::span: a view into a sequence, without owning it ---
int sum(std::span<const int> values) {
  int total = 0;
  for (int v : values)
    total += v;
  return total;
}

void section_span() {
  std::puts("[9] span");
  const int array[] = {1, 2, 3};
  const std::vector<int> vec{10, 20};
  std::printf("sum(array)=%d sum(vector)=%d sum(first 2 elements)=%d\n", sum(array), sum(vec),
              sum(std::span<const int>(array).first(2)));
}

int main() {
  section_auto();
  section_bindings();
  section_lambda();
  section_designated();
  section_initializer_list();
  section_constexpr();
  section_enum();
  section_template();
  section_span();
  return 0;
}
