// Biên dịch: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_modern.cpp -o modern
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

// --- 1. auto và range-for ---
void section_auto() {
  std::puts("[1] auto va range-for");
  std::vector<std::string> names{"hero", "slime", "bat"};
  auto count = names.size();          // kiểu suy ra: std::size_t
  auto ratio = 2.5f;                  // float
  auto copy = names[0];               // auto bỏ tham chiếu: đây là BẢN SAO
  auto &alias = names[0];             // auto & giữ tham chiếu
  copy += "!";
  alias += "?";
  std::printf("count=%zu ratio=%.1f names[0]=%s copy=%s\n", count, static_cast<double>(ratio),
              names[0].c_str(), copy.c_str());
  for (const auto &n : names)         // duyệt không sao chép từng chuỗi
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
  const auto [target, damage] = hit; // tách một struct
  std::printf("target=%d damage=%.1f\n", target, static_cast<double>(damage));

  std::map<std::string, int> hp{{"hero", 10}, {"slime", 3}};
  for (const auto &[name, value] : hp) // tách từng cặp (khóa, giá trị)
    std::printf("  %s has %d hp\n", name.c_str(), value);

  // Giống view<...>().each() của EnTT: mỗi hàng là (entity, tham chiếu tới các component).
  vec2 pos{0.0f, 0.0f};
  int life = 3;
  std::tuple<int, vec2 &, int &> row{1, pos, life};
  auto [entity, p, l] = row; // ba tên cho ba phần tử: entity, vec2 &, int &
  p.x += 5.0f;               // p là tham chiếu tới pos: sửa được
  l -= 1;
  std::printf("entity=%d pos.x=%.0f life=%d\n", entity, static_cast<double>(pos.x), life);
}

// --- 3. Lambda và std::function ---
struct Ctx {
  int frame = 0;
  std::vector<std::pair<int, std::function<void(Ctx &)>>> timers;
};

// Giống njin::timer_after: nhận BẤT KỲ thứ gì gọi được, kể cả lambda có capture.
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

using sys_fnc = void (*)(Ctx &); // giống njin::sys_fnc: con trỏ hàm, không có trạng thái

void bump_system(Ctx &ctx) { ctx.frame += 100; }

void section_lambda() {
  std::puts("[3] lambda va std::function");
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

  sys_fnc sys = bump_system;                        // hàm thường: được
  sys_fnc no_capture = [](Ctx &c) { c.frame += 1; }; // lambda KHÔNG capture: đổi được sang con trỏ hàm
  Ctx other;
  sys(other);
  no_capture(other);
  std::printf("other.frame = %d\n", other.frame);
}

// --- 4. Khởi tạo chỉ định (designated initializers, C++20) ---
struct Cfg {
  const char *title;
  float width;
  float height;
  float fps = 60.0f;
  bool resizable = false;
};

void section_designated() {
  std::puts("[4] khoi tao chi dinh");
  const Cfg cfg{.title = "game", .width = 960.0f, .height = 540.0f, .resizable = true};
  std::printf("%s %.0fx%.0f fps=%.0f resizable=%s\n", cfg.title, static_cast<double>(cfg.width),
              static_cast<double>(cfg.height), static_cast<double>(cfg.fps),
              cfg.resizable ? "yes" : "no");
}

// --- 5. std::initializer_list và chuyển kiểu ngầm ---
enum Key { key_space, key_w };
enum PadButton { pad_a, pad_b };

struct Binding {
  int kind; // 0 = phím, 1 = nút tay cầm
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
  define("jump", {key_space, key_w, pad_a}); // phím và nút tay cầm trong CÙNG một danh sách
  define("pause", {pad_b});
}

// --- 6. constexpr ---
constexpr int max_voices = 8;
constexpr int square(int x) { return x * x; }
static_assert(square(4) == 16, "tinh luc bien dich");
inline constexpr float pi = 3.14159265f; // trong header phải là inline constexpr

void section_constexpr() {
  std::puts("[6] constexpr");
  int table[square(3)]; // kích thước mảng tính lúc biên dịch
  for (int i = 0; i < square(3); i++)
    table[i] = i;
  std::printf("max_voices=%d table=%zu phan tu, pi=%.2f\n", max_voices, sizeof(table) / sizeof(table[0]),
              static_cast<double>(pi));
}

// --- 7. enum và enum class ---
enum audio_bus { bus_master, bus_music, bus_sfx }; // tên nằm ngoài, tự đổi sang int
enum class ease { linear, in_quad };               // tên nằm trong `ease::`, không tự đổi sang int

void section_enum() {
  std::puts("[7] enum va enum class");
  int a = bus_sfx;
  ease e = ease::in_quad;
  std::printf("bus_sfx=%d ease::in_quad=%d\n", a, static_cast<int>(e));
}

// --- 8. Template: dùng, không viết ---
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
  reg.emplace(1, Health{3});         // T suy ra từ đối số: Health
  reg.emplace<vec2>(1, {4.0f, 5.0f}); // T ghi rõ trong <>
  reg.get<Health>(1).hp -= 1;         // get không có đối số nào cho biết T: phải ghi <Health>
  std::printf("hp=%d pos=%.0f,%.0f\n", reg.get<Health>(1).hp, static_cast<double>(reg.get<vec2>(1).x),
              static_cast<double>(reg.get<vec2>(1).y));
}

// --- 9. std::span: nhìn vào một dãy, không sở hữu ---
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
  std::printf("sum(array)=%d sum(vector)=%d sum(2 phan tu dau)=%d\n", sum(array), sum(vec),
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
