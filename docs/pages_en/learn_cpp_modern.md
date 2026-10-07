# Lesson 6: Modern C++ in njin code {#learn_cpp_modern}

**What this lesson teaches**: the C++11 to C++20 syntax that keeps njin code short: `auto`, structured bindings, lambdas and
`std::function`, designated initializers, `std::initializer_list`, `constexpr`, `enum class`, templates (to **use**), `std::span`.

**What you need to know first**: references, `const`, `std::vector` (@ref learn_cpp_from_c), and the idea of values versus references
(@ref learn_cpp_types).

Each section has an example that runs and a piece of njin code that uses exactly that syntax. Everything compiles with
`g++ -std=c++20 -Wall -Wextra -Wpedantic` (GCC 15.2) with no warnings; the error messages are real output.

## One program with nine sections

@include learn_cpp_modern.cpp

Output:

```
[1] auto and range-for
count=3 ratio=2.5 names[0]=hero? copy=hero!
  hero?
  slime
  bat
[2] structured binding
target=7 damage=12.5
  hero has 10 hp
  slime has 3 hp
entity=1 pos.x=5 life=2
[3] lambda and std::function
by_value(1)=6 by_ref(1)=101
  frame 2: respawn
  frame 3: lives=3
other.frame = 101
[4] designated initializers
game 960x540 fps=60 resizable=yes
[5] initializer_list
jump: key0 key1 pad0
pause: pad1
[6] constexpr
max_voices=8 table=9 elements, pi=3.14
[7] enum and enum class
bus_sfx=2 ease::in_quad=1
[8] template
hp=2 pos=4,5
[9] span
sum(array)=6 sum(vector)=30 sum(first 2 elements)=3
```

## 1. `auto` and the range-for loop

`auto` lets the compiler **deduce the type** from the initial value: `auto count = names.size();` is `std::size_t`,
`auto ratio = 2.5f;` is `float`. It does not make the code looser: the type is still fixed at compile time; you just do not have to write
it out. Use it when the type is long or obvious; keep the type explicit when the type is information the reader needs.

**Pitfall: `auto` drops references.**

```cpp
auto copy = names[0];     // std::string: a COPY
auto &alias = names[0];   // std::string &: the same string
copy += "!";
alias += "?";             // names[0] becomes "hero?", copy is "hero!"
```

Output `[1]` confirms it: `names[0]=hero? copy=hero!`. To keep a reference, write `auto &`; for a read-only reference, write
`const auto &`.

A **range-for** loop walks a container directly: `for (const auto &n : names)`. With `&`, each iteration does not copy
the string; with `const`, it cannot be changed.

## 2. Structured binding

`auto [a, b] = value;` **splits** an object into several names. It works with `struct`, `std::pair`, `std::tuple` and
arrays. You see it most when looping over a `std::map`, or over the result of a `view` in EnTT:

```cpp
const auto [target, damage] = hit;              // hit is struct Hit { int target; float damage; }
for (const auto &[name, value] : hp)            // hp is std::map<std::string, int>
  std::printf("%s has %d hp\n", name.c_str(), value);
```

**In njin**, this is how you loop over entities (`src/games/pong/pong.cpp`):

```cpp
for (auto [e, tr, b] : reg.view<transform, ball>().each()) {
  tr.pos = field * 0.5f;
  ...
}
```

Each iteration is one row: the entity, then the requested components as **references**. Example `[2]` imitates this with
`std::tuple<int, vec2 &, int &>`: `p` and `l` are references, so `p.x += 5` changes the real `pos` (`pos.x=5 life=2`).

**Pitfall: the number of names must equal the number of elements.** `view<A, B, C>` gives **the entity plus three components**, that is four elements, so
you need `auto [e, a, b, c]`. With `std::tuple<int, vec2 &, int &>` (three elements) and only two names written:

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

The `note:` message tells you clearly what the third element is. This is the most common error when writing an `each()` loop for the first time; see also
@ref learn_errors. If you do not use the name `e`, write `(void)e;` to avoid the unused-variable warning.

## 3. Lambdas and `std::function`

A **lambda** is a nameless function written right where it is used: `[bonus](int x) { return x + bonus; }`. The part inside `[ ]` is the **capture**:
the outside variables the lambda may use.

| Written | Meaning |
|---|---|
| `[]` | Captures nothing |
| `[bonus]` | Copies `bonus` into the lambda (by value) |
| `[&bonus]` | Keeps a reference to `bonus` |
| `[=]`, `[&]` | Copies, or references, every outside variable the lambda uses |

The difference shows in output `[3]`: after `bonus = 100`, the lambda `[bonus]` (copied at creation, keeps 5) gives `by_value(1)=6`,
and the lambda `[&bonus]` (sees the new value) gives `by_ref(1)=101`.

**Pitfall: capturing by reference and then the lambda outlives the variable.** `[&x]` points to `x`; if the lambda is called after `x` is no longer
alive, that is a dangling reference (@ref learn_cpp_types). For a lambda that is stored to run later (timers, callbacks), **capture
by value** (`[lives]`).

**`std::function<void(Ctx &)>`** is a type that can hold **anything callable** with that signature: a plain function, a lambda, a lambda
with captures. The function `after` in the example imitates `njin::timer_after`:

```cpp
after(ctx, 2, [](Ctx &c) { std::printf("frame %d: respawn\n", c.frame); });
after(ctx, 3, [lives](Ctx &c) { std::printf("frame %d: lives=%d\n", c.frame, lives); });
```

After 4 calls to `tick`, the output shows `frame 2: respawn` and `frame 3: lives=3`, exactly at the scheduled frames, and the second lambda
still remembers `lives`. **In njin**, the real signature (`src/engine/api/njin_timer.h`):

```cpp
timer_handle timer_after(context &ctx, f32 seconds, std::function<void(context &)> fn,
                         const timer_desc &desc = {});
// use:
njin::timer_after(ctx, 0.4f, [](njin::context &c) { njin::scene_fade(c, next); });
```

**Function pointers versus lambdas.** njin has **two** kinds of callables, because there are two different needs:

- A **system** (`sys_fnc`) is a plain function pointer with no state (`using sys_fnc = void (*)(context &ctx);`
  in `_mod.h`). The game's state lives in `ctx` and the registry, not in the function.
- A **timer** is a `std::function`: it needs to remember the context at the time it was set (`lives`, the entity to respawn).

A lambda **without captures** converts to a function pointer (that is how the platformer writes `.setup = [](context &c) { ... }` for
`mod_desc`). A lambda **with captures** does not, because it carries data that a function pointer cannot hold:

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

Line 7 (the lambda without captures) compiles; only line 8 (with the capture `[bonus]`) is rejected.

## 4. Designated initializers

C++20 lets you write **field names** when initializing a `struct`: `Cfg cfg{.title = "game", .width = 960.0f, ...};`. Fields
you leave out get their default values (`fps` = 60, `resizable` = `false`), and the code explains itself, with no commas to count.

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

The rule: **the fields must be written in their declaration order** (C does not require it, C++ does):

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

It only applies to simple `struct`s (aggregates): ones with no constructor you wrote yourself.

**In njin**, this is how a game starts (`src/games/pong/main.cpp`). `config` has many fields with default values, so the game
only writes the fields it wants to change:

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

## 5. `std::initializer_list` and implicit conversion

A function that takes a `std::initializer_list<T>` can be called with a list in braces: `define("jump", {key_space, key_w, pad_a})`.
In example `[5]`, the list mixes **keys and gamepad buttons**: they are two different types, but `Binding` has **a constructor
for each type**, so each element converts to `Binding` on its own:

```cpp
struct Binding {
  int kind; // 0 = key, 1 = gamepad button
  int code;
  Binding(Key k) : kind(0), code(k) {}
  Binding(PadButton b) : kind(1), code(b) {}
};

void define(const char *name, std::initializer_list<Binding> sources);
```

Output: `jump: key0 key1 pad0`, and `pause: pad1`.

**In njin**, this is exactly `njin::binding` and `njin::action_define` (`src/engine/api/njin_bindings.h`), which let a game
declare its keys in one line:

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

In `axis_define`, the first list is an `initializer_list<axis_keys>` where each element is a pair `{negative, positive}`.

## 6. `constexpr`

`constexpr` means "**can be computed at compile time**". The constant `constexpr int max_voices = 8;` and the function
`constexpr int square(int x)` can be used where a compile-time constant is needed: array sizes (`int table[square(3)]`),
`static_assert`.

```cpp
constexpr int square(int x) { return x * x; }
static_assert(square(4) == 16, "computed at compile time");  // if false, it does not compile
int table[square(3)];                                  // 9 elements
```

In a **header**, write `inline constexpr`, not just `constexpr`. That is because `inline` lets the definition appear in several
`.cpp` files without clashing at link time (the same reason as for functions in headers, see @ref learn_errors). **In njin**:

```cpp
inline constexpr f32 pi = 3.14159265358979f;          // _math.h
inline constexpr u32 layer_all = 0xFFFFFFFFu;         // njin_collision.h
constexpr u32 layer_bit(i32 n) { return 1u << (u32)n; } // constexpr function: layer_bit(2) is a compile-time constant
```

A game writes `constexpr u32 layer_player = layer_bit(1);` to get its own named constant at no run-time cost.

## 7. `enum` and `enum class`

The two kinds of enum differ in **where the names live** and **whether they convert to a number on their own**:

| | `enum audio_bus { bus_sfx }` | `enum class ease { linear }` |
|---|---|---|
| How you name it | `bus_sfx` (the name lives outside) | `ease::linear` (the name lives inside `ease`) |
| Converting to `int` | Automatic: `int a = bus_sfx;` | You must write `static_cast<int>(e)` |

Try mixing them up:

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

**njin has both.** `enum audio_bus` (`njin_audio.h`) is a plain enum whose names have the prefix `bus_` (`bus_master`, `bus_music`,
`bus_sfx`...) and a last element `audio_bus_count` to count the buses; where it is used: `audio_store` (`njin_audio_impl.h`) declares
`f32 bus_volume[audio_bus_count]` and looks up the volume by bus, that is, it uses the enum as an array index. `enum class ease`
(`_tween.h`: `ease::linear`, `ease::in_quad`, ...) is a set of choices, named with `ease::name`.

## 8. Templates: use them, do not write them

You will **call** templates far more often than you write them. What you need to understand is the part inside `<>`: a **type parameter**, which the
compiler uses to generate the function for exactly that type. Example `[8]` has a small `Registry` that imitates the way EnTT is called:

```cpp
class Registry {
  std::map<std::type_index, std::map<int, std::any>> data_;
public:
  template <typename T> void emplace(int entity, T value) { data_[typeid(T)][entity] = std::move(value); }
  template <typename T> T &get(int entity) { return std::any_cast<T &>(data_.at(typeid(T)).at(entity)); }
};

reg.emplace(1, Health{3});          // T deduced from the argument: Health
reg.emplace<vec2>(1, {4.0f, 5.0f}); // T written out in <>, because {4, 5} does not tell its own type
reg.get<Health>(1).hp -= 1;         // get: no argument tells T, so you MUST write <Health>
```

Output: `hp=2 pos=4,5`. From there you can understand the lines you see all over njin code:

```cpp
reg.emplace<njin::transform>(e);        // attach a transform to entity e
reg.get<njin::transform>(e).pos = ...;  // get e's transform
reg.view<transform, ball>();            // loop over every entity that has both
```

`<transform>` means "which kind of component". **Real EnTT requires `<T>` on `emplace` too**; it cannot deduce it from the argument like
the small `Registry` above, because `emplace<T>(e, args...)` passes `args` to `T`'s constructor (for example
`reg.emplace<transform>(e, pos)`, where the argument is `pos`, not a `transform`). Leave out `<T>` and you get:

File `entt_emplace.cpp` (needs EnTT; compile with `-isystem` pointing to EnTT's `src` folder):

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

`couldn't deduce template parameter` is GCC's standard wording when `<T>` is missing: add the type in `<>`. When you see an error with
a long `[with T = ...]` string, look for your own line: the lesson @ref learn_errors shows how to read it.

## 9. `std::span`: a view into a sequence, without owning it

`std::span<const int>` is **a pair (pointer, length)** that looks into an existing sequence: a C array, a `std::vector`, or part
of one, **without copying** the data. A function that takes a `span` works with any source:

```cpp
int sum(std::span<const int> values);
sum(array);                                    // a C array
sum(vec);                                      // std::vector
sum(std::span<const int>(array).first(2));     // the first 2 elements
```

The three calls give `6`, `30`, `3`. A `span` **does not own** anything: it is subject to the same dangling-reference rule as in @ref learn_cpp_types; the original
sequence must still be alive when it is used. **In njin**, the module registration function has a version that takes a `span`:

```cpp
void mod_register(context &ctx, std::span<const mod_desc> mods);
```

## What about `[[nodiscard]]`

`[[nodiscard]]` is put on a function so the compiler **warns when the return value is ignored**, a good fit for functions that return error codes or handles:

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

njin's API **does not use** this attribute (a search in `src` finds no place); but it suits your own code very well.

## Self-check

1. How do `auto x = names[0];` and `auto &x = names[0];` differ when you change `x`?
2. `auto [a, b] = row;` reports `decomposes into 4 elements`. What needs fixing?
3. The lambda `[&count] { count++; }` is stored in a timer and runs 5 seconds later, when `count` (a local variable) is no longer alive. What is the
   problem and how do you fix it?
4. Why is njin's `sys_fnc` a function pointer while `timer_after` takes a `std::function`?
5. In the lesson's small `Registry`, why must `reg.get<Health>(1)` write `<Health>` while `reg.emplace(1, Health{3})`
   can leave it out? And with real EnTT, does `reg.emplace(e, Health{3})` compile?

## Exercises

**Exercise 1.** Write a function `describe(const Hit &h)` that uses a structured binding to print `target 7 takes 12.5`. Add a function `heaviest`
that takes a `std::vector<Hit>` and returns the `Hit` with the largest `damage`, looping with `for (const auto &[t, d] : hits)`.

**Exercise 2.** Write `void repeat(int times, std::function<void(int)> fn)` that calls `fn(i)` for `i` from 0 to `times - 1`. Call it with
a lambda that copies `prefix` (a `std::string`) and prints `prefix 0`, `prefix 1`, `prefix 2`.

**Exercise 3.** Given `struct Window { const char *title; int width; int height; bool vsync = true; bool fullscreen = false; };`.
Initialize it with designated initializers: `title` is `"demo"`, `width` 800, `height` 600, `fullscreen` is `true`, and leave `vsync` at its default. Print
all the fields.

## Answers

**Questions.**

1. `auto x` is a **copy**: changing `x` does not change `names[0]`. `auto &x` is a **reference**: changing `x` changes `names[0]`.
2. Add names until there are enough for every element (here, 4 names), or remove elements from the source. A name you do not need must still be there (use
   `(void)name;` to avoid the warning).
3. `[&count]` keeps a reference to a variable that is no longer alive: a dangling reference, undefined behavior. Fix: capture by value
   (`[count]`) or keep the state somewhere that lives long enough (in `ctx`, in an entity).
4. A system does not need to remember anything (the state lives in `ctx`), so a function pointer is enough and simple. A timer needs to remember the context at the time
   it was set, so it needs something that can hold a lambda with captures: `std::function`.
5. With `get`, no argument carries the component type, so the compiler cannot deduce `T`. With `emplace(1, Health{3})` in
   the small version, the argument `Health{3}` tells `T`. **Real EnTT does not**: `emplace` takes `args...` to construct `T`, so the arguments
   do not tell `T`, and leaving out `<T>` reports `couldn't deduce template parameter 'Type'` (tried above).

**Exercise 1.** Running it prints `target 7 takes 12.5` and `heaviest: target 3 takes 30.0`:

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

**Exercise 2.** Running it prints `hit 0`, `hit 1`, `hit 2`:

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

**Exercise 3.** Running it prints `demo 800x600 vsync=1 fullscreen=1`. Note that `vsync` takes its default value `true` because it was left out:

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

## Next steps

- @ref learn_errors : reading compile errors and link errors, including long template errors
- @ref learn_cmake_basics : writing a build file instead of typing long `g++` commands
