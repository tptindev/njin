# Bài 6: C++ hiện đại trong code njin {#learn_cpp_modern}

**Bài này dạy gì**: những cú pháp C++11 đến C++20 làm code njin đọc gọn: `auto`, structured binding, lambda và
`std::function`, khởi tạo chỉ định, `std::initializer_list`, `constexpr`, `enum class`, template (để **dùng**), `std::span`.

**Cần biết trước**: tham chiếu, `const`, `std::vector` (@ref learn_cpp_from_c), và ý niệm giá trị so với tham chiếu
(@ref learn_cpp_types).

Mỗi mục có một ví dụ chạy được và một đoạn code njin dùng đúng cú pháp đó. Toàn bộ biên dịch với
`g++ -std=c++20 -Wall -Wextra -Wpedantic` (GCC 15.2), không có cảnh báo; các thông báo lỗi là đầu ra thật.

## Chương trình gom chín mục

@include learn_cpp_modern.cpp

Đầu ra:

```
[1] auto va range-for
count=3 ratio=2.5 names[0]=hero? copy=hero!
  hero?
  slime
  bat
[2] structured binding
target=7 damage=12.5
  hero has 10 hp
  slime has 3 hp
entity=1 pos.x=5 life=2
[3] lambda va std::function
by_value(1)=6 by_ref(1)=101
  frame 2: respawn
  frame 3: lives=3
other.frame = 101
[4] khoi tao chi dinh
game 960x540 fps=60 resizable=yes
[5] initializer_list
jump: key0 key1 pad0
pause: pad1
[6] constexpr
max_voices=8 table=9 phan tu, pi=3.14
[7] enum va enum class
bus_sfx=2 ease::in_quad=1
[8] template
hp=2 pos=4,5
[9] span
sum(array)=6 sum(vector)=30 sum(2 phan tu dau)=3
```

## 1. `auto` và vòng lặp range-for

`auto` để trình biên dịch **tự suy ra kiểu** từ giá trị khởi tạo: `auto count = names.size();` là `std::size_t`,
`auto ratio = 2.5f;` là `float`. Nó không làm code lỏng hơn: kiểu vẫn cố định lúc biên dịch, chỉ là bạn khỏi viết
ra. Dùng nó khi kiểu dài hoặc hiển nhiên; giữ kiểu tường minh khi kiểu là thông tin cần cho người đọc.

**Cạm bẫy: `auto` bỏ tham chiếu.**

```cpp
auto copy = names[0];     // std::string: BẢN SAO
auto &alias = names[0];   // std::string &: cùng một chuỗi
copy += "!";
alias += "?";             // names[0] thành "hero?", copy là "hero!"
```

Đầu ra `[1]` xác nhận: `names[0]=hero? copy=hero!`. Muốn giữ tham chiếu, viết `auto &`; muốn tham chiếu chỉ đọc, viết
`const auto &`.

Vòng **range-for** duyệt trực tiếp một container: `for (const auto &n : names)`. Có `&` thì mỗi vòng không sao chép
chuỗi; có `const` thì không sửa được.

## 2. Structured binding

`auto [a, b] = giá_trị;` **tách** một đối tượng thành nhiều tên. Dùng được với `struct`, `std::pair`, `std::tuple` và
mảng. Thấy nhiều nhất là khi duyệt một `std::map`, hoặc duyệt kết quả của `view` trong EnTT:

```cpp
const auto [target, damage] = hit;              // hit là struct Hit { int target; float damage; }
for (const auto &[name, value] : hp)            // hp là std::map<std::string, int>
  std::printf("%s has %d hp\n", name.c_str(), value);
```

**Trong njin**, đây là cách duyệt entity (`src/games/pong/pong.cpp`):

```cpp
for (auto [e, tr, b] : reg.view<transform, ball>().each()) {
  tr.pos = field * 0.5f;
  ...
}
```

Mỗi vòng là một hàng: entity, rồi các component đã yêu cầu ở dạng **tham chiếu**. Ví dụ `[2]` mô phỏng điều đó bằng
`std::tuple<int, vec2 &, int &>`: `p` và `l` là tham chiếu, nên `p.x += 5` sửa được `pos` thật (`pos.x=5 life=2`).

**Cạm bẫy: số tên phải bằng số phần tử.** `view<A, B, C>` cho ra **entity cộng ba component**, tức bốn phần tử, nên
`auto [e, a, b, c]` mới đủ. Với `std::tuple<int, vec2 &, int &>` (ba phần tử) mà chỉ viết hai tên:

File `sb_count.cpp`:

```cpp
#include <tuple>

struct vec2 { float x, y; };

int main() {
  vec2 pos{0, 0};
  int hp = 3;
  int entity = 1;
  std::tuple<int, vec2 &, int &> row{entity, pos, hp};
  auto [e, p] = row;
  return e;
}
```

```
sb_count.cpp:10:8: error: only 2 names provided for structured binding
   10 |   auto [e, p] = row;
      |        ^~~~~~
sb_count.cpp:10:8: note: while 'std::tuple<int, vec2&, int&>' decomposes into 3 elements
```

Thông báo `note:` nói rõ phần tử thứ ba là gì. Đây là lỗi hay gặp nhất khi viết vòng `each()` lần đầu, xem thêm
@ref learn_errors. Nếu không dùng tên `e`, viết `(void)e;` để tránh cảnh báo biến không dùng.

## 3. Lambda và `std::function`

**Lambda** là hàm không tên viết ngay tại chỗ: `[bonus](int x) { return x + bonus; }`. Phần trong `[ ]` là **capture**:
những biến bên ngoài mà lambda được dùng.

| Viết | Nghĩa |
|---|---|
| `[]` | Không capture gì |
| `[bonus]` | Chép `bonus` vào lambda (theo giá trị) |
| `[&bonus]` | Giữ tham chiếu tới `bonus` |
| `[=]`, `[&]` | Chép, hoặc tham chiếu, mọi biến bên ngoài mà lambda dùng |

Khác biệt hiện trong đầu ra `[3]`: sau khi `bonus = 100`, lambda `[bonus]` (chép lúc tạo, giữ 5) cho `by_value(1)=6`,
lambda `[&bonus]` (thấy giá trị mới) cho `by_ref(1)=101`.

**Cạm bẫy: capture tham chiếu rồi lambda sống lâu hơn biến.** `[&x]` trỏ vào `x`; nếu lambda được gọi sau khi `x` đã hết
sống, đó là tham chiếu treo (@ref learn_cpp_types). Với lambda được lưu lại để chạy sau (timer, callback), **capture
theo giá trị** (`[lives]`).

**`std::function<void(Ctx &)>`** là kiểu chứa được **bất kỳ thứ gì gọi được** với chữ ký đó: hàm thường, lambda, lambda
có capture. Hàm `after` trong ví dụ mô phỏng `njin::timer_after`:

```cpp
after(ctx, 2, [](Ctx &c) { std::printf("frame %d: respawn\n", c.frame); });
after(ctx, 3, [lives](Ctx &c) { std::printf("frame %d: lives=%d\n", c.frame, lives); });
```

Sau 4 lần `tick`, đầu ra cho thấy `frame 2: respawn` và `frame 3: lives=3`, đúng số frame đã hẹn, và lambda thứ
hai vẫn nhớ `lives`. **Trong njin**, chữ ký thật (`src/engine/api/njin_timer.h`):

```cpp
timer_handle timer_after(context &ctx, f32 seconds, std::function<void(context &)> fn,
                         const timer_desc &desc = {});
// dùng:
njin::timer_after(ctx, 0.4f, [](njin::context &c) { njin::scene_fade(c, next); });
```

**Con trỏ hàm so với lambda.** njin có **hai** loại gọi được, vì hai nhu cầu khác nhau:

- **System** (`sys_fnc`) là con trỏ hàm thuần, không có trạng thái (`using sys_fnc = void (*)(context &ctx);`
  trong `_mod.h`). Trạng thái của game nằm trong `ctx` và registry, không nằm trong hàm.
- **Hẹn giờ** là `std::function`: cần nhớ ngữ cảnh lúc hẹn (`lives`, entity cần hồi sinh).

Lambda **không capture** đổi được sang con trỏ hàm (đó là cách platformer viết `.setup = [](context &c) { ... }` cho
`mod_desc`). Lambda **có capture** thì không, vì nó mang theo dữ liệu mà con trỏ hàm không chứa được:

File `lambda_ptr.cpp`:

```cpp
struct Ctx { int frame = 0; };

using sys_fnc = void (*)(Ctx &);

int main() {
  int bonus = 5;
  sys_fnc plain = [](Ctx &c) { c.frame += 100; };
  sys_fnc capturing = [bonus](Ctx &c) { c.frame += bonus; };
  Ctx ctx;
  plain(ctx);
  capturing(ctx);
  return ctx.frame;
}
```

```
lambda_ptr.cpp:8:23: error: cannot convert 'main()::<lambda(Ctx&)>' to 'sys_fnc' {aka 'void (*)(Ctx&)'} in initialization
    8 |   sys_fnc capturing = [bonus](Ctx &c) { c.frame += bonus; };
      |                       ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

Dòng 7 (lambda không capture) biên dịch được; chỉ dòng 8 (có capture `[bonus]`) bị từ chối.

## 4. Khởi tạo chỉ định (designated initializers)

C++20 cho phép ghi **tên trường** khi khởi tạo một `struct`: `Cfg cfg{.title = "game", .width = 960.0f, ...};`. Trường
bỏ qua nhận giá trị mặc định của nó (`fps` = 60, `resizable` = `false`), và code tự giải thích, khỏi đếm dấu phẩy.

```cpp
struct Cfg {
  const char *title;
  float width;
  float height;
  float fps = 60.0f;
  bool resizable = false;
};

const Cfg cfg{.title = "game", .width = 960.0f, .height = 540.0f, .resizable = true};
```

Quy tắc: **phải viết đúng thứ tự khai báo** của các trường (C không bắt buộc, C++ thì có):

File `desig_order.cpp`:

```cpp
struct Cfg {
  const char *title;
  float width;
  float height;
};

int main() {
  Cfg cfg{.title = "game", .height = 540.0f, .width = 960.0f};
  return static_cast<int>(cfg.width);
}
```

```
desig_order.cpp:8:61: warning: missing initializer for member 'Cfg::width' [-Wmissing-field-initializers]
desig_order.cpp:8:61: error: designator order for field 'Cfg::width' does not match declaration order in 'Cfg'
```

Chỉ áp dụng cho `struct` đơn giản (aggregate): không có hàm khởi tạo do bạn viết.

**Trong njin**, đây là cách mở game (`src/games/pong/main.cpp`). `config` có nhiều trường với giá trị mặc định, nên game
chỉ ghi những trường muốn đổi:

```cpp
const njin::config cfg{.title = "njin pong",
                         .width = 960.0f,
                         .height = 540.0f,
                         .target_fps = 60.0f,
                         .clear_bg_color = {0.06f, 0.07f, 0.1f, 1.0f},
                         .exit_key = njin::key_none,
                         .resizable = true,
                         .app_name = "njin pong"};
```

## 5. `std::initializer_list` và chuyển kiểu ngầm

Một hàm nhận `std::initializer_list<T>` cho phép gọi với danh sách trong ngoặc nhọn: `define("jump", {key_space, key_w, pad_a})`.
Trong ví dụ `[5]`, danh sách trộn **phím và nút tay cầm**: chúng là hai kiểu khác nhau, nhưng `Binding` có **hàm khởi tạo
nhận từng kiểu**, nên mỗi phần tử tự đổi sang `Binding`:

```cpp
struct Binding {
  int kind; // 0 = phím, 1 = nút tay cầm
  int code;
  Binding(Key k) : kind(0), code(k) {}
  Binding(PadButton b) : kind(1), code(b) {}
};

void define(const char *name, std::initializer_list<Binding> sources);
```

Đầu ra: `jump: key0 key1 pad0`, và `pause: pad1`.

**Trong njin**, đây chính là `njin::binding` và `njin::action_define` (`src/engine/api/njin_bindings.h`), nhờ đó game
khai báo phím trong một dòng:

```cpp
struct binding {
  input_source source;
  binding(key_code key) : source{input_source::key, (i32)key} {}
  binding(mouse_button button) : source{input_source::mouse, (i32)button} {}
  binding(gamepad_button button) : source{input_source::pad, (i32)button} {}
};

// src/games/platformer/main.cpp
g.jump = action_define(ctx, "jump", {key_space, key_w, key_up, pad_face_down});
g.move = axis_define(ctx, "move", {{key_left, key_right}, {key_a, key_d}}, {pad_axis_left_x});
```

Ở `axis_define`, danh sách đầu là `initializer_list<axis_keys>` với mỗi phần tử là một cặp `{âm, dương}`.

## 6. `constexpr`

`constexpr` nghĩa là "**tính được lúc biên dịch**". Hằng số `constexpr int max_voices = 8;` và hàm
`constexpr int square(int x)` dùng được ở chỗ cần hằng lúc biên dịch: kích thước mảng (`int table[square(3)]`),
`static_assert`.

```cpp
constexpr int square(int x) { return x * x; }
static_assert(square(4) == 16, "tinh luc bien dich");  // sai thì không biên dịch
int table[square(3)];                                  // 9 phần tử
```

Trong **header**, viết `inline constexpr`, không chỉ `constexpr`. Vì `inline` cho phép định nghĩa xuất hiện ở nhiều
file `.cpp` mà không đụng nhau lúc liên kết (cùng lý do như hàm trong header, xem @ref learn_errors). **Trong njin**:

```cpp
inline constexpr f32 pi = 3.14159265358979f;          // _math.h
inline constexpr u32 layer_all = 0xFFFFFFFFu;         // njin_collision.h
constexpr u32 layer_bit(i32 n) { return 1u << (u32)n; } // hàm constexpr: layer_bit(2) là hằng lúc biên dịch
```

Game viết `constexpr u32 layer_player = layer_bit(1);` để có hằng tên riêng mà không tốn gì lúc chạy.

## 7. `enum` và `enum class`

Hai loại enum khác nhau ở chỗ **tên nằm ở đâu** và **có tự đổi sang số không**:

| | `enum audio_bus { bus_sfx }` | `enum class ease { linear }` |
|---|---|---|
| Cách gọi | `bus_sfx` (tên nằm ngoài) | `ease::linear` (tên nằm trong `ease`) |
| Đổi sang `int` | Tự động: `int a = bus_sfx;` | Phải viết `static_cast<int>(e)` |

Thử trộn lẫn:

File `enum_class.cpp`:

```cpp
enum audio_bus { bus_master, bus_music, bus_sfx };
enum class ease { linear, in_quad };

int main() {
  int a = bus_sfx;
  int b = ease::linear;
  ease e = in_quad;
  return a + b + static_cast<int>(e);
}
```

```
enum_class.cpp:6:17: error: cannot convert 'ease' to 'int' in initialization
    6 |   int b = ease::linear;
      |           ~~~~~~^~~~~~
enum_class.cpp:7:12: error: 'in_quad' was not declared in this scope; did you mean 'ease::in_quad'?
```

**Trong njin có cả hai.** `enum audio_bus` (`njin_audio.h`) là enum thường, tên có tiền tố `bus_` (`bus_master`, `bus_music`,
`bus_sfx`...) và có phần tử cuối `audio_bus_count` để đếm số kênh; chỗ dùng: `audio_store` (`njin_audio_impl.h`) khai báo
`f32 bus_volume[audio_bus_count]` và tra âm lượng theo kênh, tức dùng enum như chỉ số mảng. `enum class ease`
(`_tween.h`: `ease::linear`, `ease::in_quad`, ...) là một tập lựa chọn, gọi bằng `ease::tên`.

## 8. Template: dùng, không viết

Bạn sẽ **gọi** template nhiều hơn viết ra. Điều cần hiểu là phần trong `<>`: một **tham số kiểu**, thứ mà trình biên
dịch dùng để tạo ra hàm cho đúng kiểu đó. Trong ví dụ `[8]` có một `Registry` thu nhỏ, mô phỏng cách gọi của EnTT:

```cpp
class Registry {
  std::map<std::type_index, std::map<int, std::any>> data_;
public:
  template <typename T> void emplace(int entity, T value) { data_[typeid(T)][entity] = std::move(value); }
  template <typename T> T &get(int entity) { return std::any_cast<T &>(data_.at(typeid(T)).at(entity)); }
};

reg.emplace(1, Health{3});          // T suy ra từ đối số: Health
reg.emplace<vec2>(1, {4.0f, 5.0f}); // T ghi rõ trong <>, vì {4, 5} không tự cho biết kiểu
reg.get<Health>(1).hp -= 1;         // get: không có đối số nào cho biết T, nên PHẢI ghi <Health>
```

Đầu ra: `hp=2 pos=4,5`. Từ đó hiểu được dòng nào cũng thấy trong code njin:

```cpp
reg.emplace<njin::transform>(e);        // gắn một transform vào entity e
reg.get<njin::transform>(e).pos = ...;  // lấy transform của e
reg.view<transform, ball>();            // duyệt mọi entity có cả hai
```

`<transform>` là "loại component nào". **EnTT thật đòi ghi `<T>` ở cả `emplace`**, không suy ra được từ đối số như
`Registry` thu nhỏ ở trên, vì `emplace<T>(e, args...)` chuyển `args` cho hàm khởi tạo của `T` (ví dụ
`reg.emplace<transform>(e, pos)`, nơi đối số là `pos` chứ không phải một `transform`). Bỏ `<T>` thì:

File `entt_emplace.cpp` (cần EnTT, biên dịch với `-isystem` trỏ tới thư mục `src` của EnTT):

```cpp
#include <entt/entity/registry.hpp>

struct Health { int hp; };

int main() {
  entt::registry reg;
  const entt::entity e = reg.create();
  reg.emplace(e, Health{3});
  return 0;
}
```

```
entt_emplace.cpp:8:14: error: no matching function for call to 'entt::basic_registry<>::emplace(const entt::entity&, Health)'
entt_emplace.cpp:8:14: note:   couldn't deduce template parameter 'Type'
```

`couldn't deduce template parameter` là câu chữ chuẩn của GCC khi thiếu `<T>`: thêm kiểu vào `<>`. Khi thấy lỗi với
chuỗi `[with T = ...]` dài, tìm dòng của bạn: bài @ref learn_errors hướng dẫn cách đọc.

## 9. `std::span`: nhìn vào một dãy, không sở hữu

`std::span<const int>` là **một cặp (con trỏ, độ dài)** nhìn vào một dãy có sẵn: mảng C, `std::vector`, hay một phần
của chúng, mà **không sao chép** dữ liệu. Một hàm nhận `span` dùng được với mọi nguồn:

```cpp
int sum(std::span<const int> values);
sum(array);                                    // mảng C
sum(vec);                                      // std::vector
sum(std::span<const int>(array).first(2));     // 2 phần tử đầu
```

Ba lời gọi ra `6`, `30`, `3`. `span` **không sở hữu**: nó chịu chung quy tắc tham chiếu treo ở @ref learn_cpp_types, dãy
gốc phải còn sống khi dùng. **Trong njin**, hàm đăng ký module có bản nhận `span`:

```cpp
void mod_register(context &ctx, std::span<const mod_desc> mods);
```

## Còn `[[nodiscard]]`

`[[nodiscard]]` gắn lên hàm để trình biên dịch **cảnh báo khi bỏ qua giá trị trả về**, phù hợp với hàm trả mã lỗi hay handle:

File `nodiscard.cpp`:

```cpp
[[nodiscard]] int try_load(const char *path) { return path != nullptr; }

int main() {
  try_load("hit.wav");
}
```

```
nodiscard.cpp:4:11: warning: ignoring return value of 'int try_load(const char*)', declared with attribute 'nodiscard' [-Wunused-result]
    4 |   try_load("hit.wav");
      |   ~~~~~~~~^~~~~~~~~~~
nodiscard.cpp:1:19: note: declared here
```

API của njin **không dùng** thuộc tính này (tìm trong `src` không thấy chỗ nào); nhưng nó rất hợp cho code của bạn.

## Tự kiểm tra

1. `auto x = names[0];` và `auto &x = names[0];` khác nhau ở đâu khi bạn sửa `x`?
2. `auto [a, b] = row;` báo `decomposes into 4 elements`. Cần sửa gì?
3. Lambda `[&count] { count++; }` được lưu vào một timer và chạy 5 giây sau, lúc `count` (biến cục bộ) đã hết sống. Vấn đề
   là gì và sửa thế nào?
4. Vì sao `sys_fnc` của njin là con trỏ hàm mà `timer_after` nhận `std::function`?
5. Trong `Registry` thu nhỏ của bài, vì sao `reg.get<Health>(1)` phải ghi `<Health>` còn `reg.emplace(1, Health{3})`
   thì bỏ được? Và với EnTT thật, `reg.emplace(e, Health{3})` có biên dịch được không?

## Bài tập

**Bài 1.** Viết hàm `describe(const Hit &h)` dùng structured binding in `target 7 takes 12.5`. Thêm một hàm `heaviest`
nhận `std::vector<Hit>` và trả về `Hit` có `damage` lớn nhất, duyệt bằng `for (const auto &[t, d] : hits)`.

**Bài 2.** Viết `void repeat(int times, std::function<void(int)> fn)` gọi `fn(i)` cho `i` từ 0 đến `times - 1`. Gọi nó với
một lambda chép `prefix` (một `std::string`) và in `prefix 0`, `prefix 1`, `prefix 2`.

**Bài 3.** Cho `struct Window { const char *title; int width; int height; bool vsync = true; bool fullscreen = false; };`.
Khởi tạo bằng khởi tạo chỉ định: `title` là `"demo"`, `width` 800, `height` 600, `fullscreen` là `true`, để `vsync` mặc định. In
tất cả trường.

## Đáp án

**Câu hỏi.**

1. `auto x` là **bản sao**: sửa `x` không đổi `names[0]`. `auto &x` là **tham chiếu**: sửa `x` là sửa `names[0]`.
2. Thêm tên cho đủ số phần tử (đây là 4 tên), hoặc bỏ bớt phần tử ở nguồn. Tên không cần dùng vẫn phải có (dùng
   `(void)tên;` để tránh cảnh báo).
3. `[&count]` giữ tham chiếu tới biến đã hết sống: tham chiếu treo, hành vi không xác định. Sửa: capture theo giá trị
   (`[count]`) hoặc để trạng thái ở nơi sống đủ lâu (trong `ctx`, trong một entity).
4. System không cần nhớ gì (trạng thái nằm trong `ctx`), nên con trỏ hàm đủ và đơn giản. Hẹn giờ cần nhớ ngữ cảnh lúc
   hẹn, nên cần một thứ chứa được lambda có capture: `std::function`.
5. Với `get`, không đối số nào mang kiểu component nên trình biên dịch không suy ra được `T`. Với `emplace(1, Health{3})` ở
   bản thu nhỏ, đối số `Health{3}` cho biết `T`. **EnTT thật thì không**: `emplace` nhận `args...` để dựng `T`, nên đối số
   không cho biết `T`, và bỏ `<T>` báo `couldn't deduce template parameter 'Type'` (thử ở trên).

**Bài 1.** Chạy in `target 7 takes 12.5` và `heaviest: target 3 takes 30.0`:

File `learn_cpp_modern_ex1.cpp`:

```cpp
#include <cstdio>
#include <vector>

struct Hit {
  int target;
  float damage;
};

void describe(const Hit &h) {
  const auto [target, damage] = h;
  std::printf("target %d takes %.1f\n", target, static_cast<double>(damage));
}

Hit heaviest(const std::vector<Hit> &hits) {
  Hit best = hits.front();
  for (const auto &[t, d] : hits)
    if (d > best.damage)
      best = Hit{t, d};
  return best;
}

int main() {
  describe(Hit{7, 12.5f});
  const std::vector<Hit> hits{{7, 12.5f}, {3, 30.0f}, {5, 8.0f}};
  const Hit top = heaviest(hits);
  std::printf("heaviest: ");
  describe(top);
}
```

**Bài 2.** Chạy in `hit 0`, `hit 1`, `hit 2`:

File `learn_cpp_modern_ex2.cpp`:

```cpp
#include <cstdio>
#include <functional>
#include <string>

void repeat(int times, std::function<void(int)> fn) {
  for (int i = 0; i < times; i++)
    fn(i);
}

int main() {
  const std::string prefix = "hit";
  repeat(3, [prefix](int i) { std::printf("%s %d\n", prefix.c_str(), i); });
}
```

**Bài 3.** Chạy in `demo 800x600 vsync=1 fullscreen=1`. Để ý `vsync` lấy giá trị mặc định `true` vì đã bỏ qua:

File `learn_cpp_modern_ex3.cpp`:

```cpp
#include <cstdio>

struct Window {
  const char *title;
  int width;
  int height;
  bool vsync = true;
  bool fullscreen = false;
};

int main() {
  const Window w{.title = "demo", .width = 800, .height = 600, .fullscreen = true};
  std::printf("%s %dx%d vsync=%d fullscreen=%d\n", w.title, w.width, w.height, w.vsync, w.fullscreen);
}
```

## Bước tiếp theo

- @ref learn_errors : đọc lỗi biên dịch và lỗi liên kết, gồm cả các lỗi template dài
- @ref learn_cmake_basics : viết file build thay vì gõ lệnh `g++` dài
