# Lesson 4: What C++ adds to C {#learn_cpp_from_c}

**What this lesson teaches**: the things C++ adds to C that njin code uses every day: references (`&`), `const`,
namespaces, `std::string`, `std::vector`, `struct` with member functions, function overloading, default arguments, `nullptr`.

**What you need to know first**: basic C: variables, functions, `struct`, pointers, multiple files with headers (see @ref learn_c_memory and
@ref learn_c_project).

Every example compiles with `g++ -std=c++20 -Wall -Wextra -Wpedantic` (GCC 15.2) and has actually been run. The output below is the
real output.

## One program that has it all

Read this program first; the sections after it explain it part by part. Each part prints a line
`[number]` so you know where it is:

@include learn_cpp_from_c.cpp

When it runs it prints:

```
[1] references
a.x = 111
[2] const
length_squared = 25
[3] namespace
3 Menu
lives = 3
[4] struct
scale = 1.0, at_origin = false
[5] overloading and default arguments
show(int) = 7
show(float) = 7.5
show(const char *) = seven
spawn(zoom=1.0, pos=0,0)
spawn(zoom=3.0, pos=0,0)
spawn(zoom=2.0, pos=10,20)
[6] nullptr
hit = 20, miss is nullptr
[7] string and vector
hero_1 (6 characters)
slime (5 characters)
bat (3 characters)
helper = 42
```

## 1. References

In C, for a function to change the caller's variable, you pass a **pointer**: `move_by_pointer(&a, 10)` (take the address) and
inside the function write `pos->x`. C++ adds **references**: `vec2 &pos`. A reference is **another name for the same variable**:

```cpp
void move_by_ref(vec2 &pos, float dx) { pos.x += dx; }  // call: move_by_ref(a, 100.0f)
```

No `&` when calling, no `->` inside the function. In the example, `a.x` starts at 1, gets 10 added through the pointer and 100 through
the reference, giving 111.

Two differences from pointers worth remembering:

- A reference **must always have an object**. There is no "null reference", and you cannot declare one without initializing it:

File `refnull.cpp`:

```cpp
int main() {
  int lives = 3;
  int &alias;
  int &r = lives;
  r = 5;
  return lives;
}
```

```
refnull.cpp:3:8: error: 'alias' declared as reference but not initialized
    3 |   int &alias;
      |        ^~~~~
```

- A reference **cannot be switched to another object**. Writing `r = b` does **not** make `r` refer to `b`: it copies the value of `b`
  into the variable that `r` is another name for.

File `reseat.cpp`:

```cpp
#include <cstdio>

int main() {
  int a = 1, b = 2;
  int &r = a;
  r = b;          // does NOT switch r to b: copies b into a
  r = 10;
  std::printf("a=%d b=%d\n", a, b);
}
```

```
a=10 b=2
```

**In njin**: the signature `void run(context &ctx)` says "I need a context, and it must exist". The create function returns
a **pointer**, because at that point there is no context yet, and the destroy function takes a pointer that is allowed to be null:

```cpp
njin::context *ctx = njin::create(cfg);  // a pointer
njin::run(*ctx);                          // *ctx: from the pointer to "the object itself", to make a reference
njin::destroy(ctx);                       // takes a pointer; nullptr is ignored
```

An easy rule to remember: **a reference when it "must exist"**, **a pointer when it "may not exist"**.

## 2. `const`

`const` is a promise "I will not change this", and the compiler checks the promise. Three common places:

```cpp
float length_squared(const vec2 &v);    // parameter: the function promises to only read v
const vec2 b{3.0f, 4.0f};               // variable: nobody can change b
bool at_origin() const { ... }          // member function: promises not to change the object
```

`const vec2 &` is the parameter type you will see most: **passed without a copy** (like a reference) but **not allowed
to change**. For a large struct, it is cheaper than passing by value.

Break the promise and the compiler reports an error right away; see the real message in @ref learn_errors (section "Violating `const`").

**In njin**, `const` tells you whether a function changes the engine or not, straight from the signature, without reading the body:

```cpp
f32 time_scale(const context &ctx);              // only asks
void time_set_scale(context &ctx, f32 scale);    // will change something
```

## 3. Namespaces

A namespace is a **family** of names: the same name can exist in two namespaces without clashing. C has none, so C libraries
must use a prefix (`SDL_Init`, `rl_...`). C++ writes `group::name`:

```cpp
namespace game {
int lives = 3;
namespace ui {
const char *title() { return "Menu"; }
}
}
// use: game::lives, game::ui::title()
```

`using namespace game;` lets you drop the prefix. Put it inside a `{ }` block and it only applies inside that block (as in the example in
section `[3]`).

**In njin**, everything in the engine is in `njin::`. A game writes `using namespace njin;` at the **top of the `.cpp` file, inside an
anonymous namespace** (section 8), so it does not have to type `njin::` before every name. Example from `src/games/pong/pong.cpp`:

```cpp
namespace pong {
namespace {
using namespace njin;
...
```

Do not write `using namespace` in a **header**: every file that `#include`s that header gets it too, and names may
clash where you least expect.

## 4. `struct` with default values and member functions

A `struct` in C++ can do more than hold data: it can have a **default value** for each field and **member
functions**.

```cpp
struct transform {
  vec2 pos{};             // {} = every field is 0
  float rot = 0.0f;
  float scale = 1.0f;

  void translate(float dx, float dy) { pos.x += dx; pos.y += dy; }
  bool at_origin() const { return pos.x == 0.0f && pos.y == 0.0f; }
};
```

`transform t;` creates an object with every field already set correctly, no init function needed. `t.translate(5, 0)` calls the function
on `t` itself, and `pos` inside the function is `t.pos`. The function `at_origin` has `const` after the parentheses: it promises not to change `t`, so it
can be called even on a `const` object.

**In njin**, this is exactly `njin::transform` (`src/engine/api/_comps.h`), a basic component, almost identical to the code above
(only without the member functions):

```cpp
struct transform {
  vec2 pos{};       ///< Position in the world.
  f32 rot = 0.0f;   ///< Rotation in degrees, clockwise.
  f32 scale = 1.0f; ///< Scale. 1 is the original size.
};
```

`njin::platformer_body` is a `struct` of the same kind with dozens of fields that have default values (`run_speed = 110.0f`,
`gravity = 1000.0f`...). That is why `reg.emplace<platformer_body>(player)` gives you a character that can jump right away, with nothing to fill in.

## 5. Function overloading and default arguments

C++ allows **several functions with the same name** if their parameter types differ (overloading). The compiler picks one based on the arguments:

```cpp
void show(int v);          // show(7)
void show(float v);        // show(7.5f)
void show(const char *v);  // show("seven")
```

If it cannot pick a single best one, it reports an error. `7.5` is a `double`, which converts to `int` or `float` equally well, so:

File `ambig2.cpp`:

```cpp
#include <cstdio>

void show(int v) { std::printf("int %d\n", v); }
void show(float v) { std::printf("float %.1f\n", v); }

int main() {
  show(7.5);
}
```

```
ambig2.cpp:7:7: error: call of overloaded 'show(double)' is ambiguous
    7 |   show(7.5);
      |   ~~~~^~~~~~
ambig2.cpp:7:7: note: there are 2 candidates
ambig2.cpp:3:6: note: candidate 1: 'void show(int)'
ambig2.cpp:4:6: note: candidate 2: 'void show(float)'
```

Fix it by giving the argument the right type: `show(7.5f)`.

**Default arguments** let you leave out the last arguments: `void spawn(float zoom = 1.0f, vec2 pos = {})` can be called as
`spawn()`, `spawn(3.0f)` or `spawn(2.0f, {10, 20})` (see the three `spawn(...)` lines in the output). The default value is written
**only once**, where the function is declared (in the header), and not repeated in the definition:

File `defarg2.cpp`:

```cpp
void spawn(float zoom = 1.0f);

void spawn(float zoom = 1.0f) { (void)zoom; }

int main() { spawn(); }
```

```
defarg2.cpp:3:6: error: default argument given for parameter 1 of 'void spawn(float)' [-fpermissive]
    3 | void spawn(float zoom = 1.0f) { (void)zoom; }
      |      ^~~~~
defarg2.cpp:1:6: note: previous specification in 'void spawn(float)' here
```

**In njin**, `camera_spawn` uses two default arguments, so `camera_spawn(ctx)`, `camera_spawn(ctx, 3.0f)` and
`camera_spawn(ctx, 3.0f, {100, 50})` are all valid:

```cpp
entt::entity camera_spawn(context &ctx, f32 zoom = 1.0f, vec2 pos = {});
```

Overloading is used in njin too: `mod_register` has three versions, taking one module, a list of modules, or a
`std::span` of modules; `render_texture_begin` has two versions, with or without a clear color. But when two jobs really are different,
the engine uses different names: `sound_play_once`, `sound_play_loop`, `sound_play_at`.

## 6. `bool` and `nullptr`

C++ has a built-in `bool` type (`true`, `false`), no `<stdbool.h>` needed. And it has `nullptr`, a null pointer **with a type**, instead of `0`
or `NULL`. Example `[6]` uses it in the most common way: a search function returns **a pointer to the element if found, or
`nullptr` if not**:

```cpp
const int *find_value(const std::vector<int> &values, int wanted);
// ...
const int *miss = find_value(scores, 99);
if (miss == nullptr) { /* not there */ }
```

This is how njin, and EnTT which njin uses, say "may not exist": `registry.try_get<hp>(e)` returns `hp *`, null if the entity
does not have that component. The other way is the handles' "id 0 means invalid", see @ref learn_cpp_types.

## 7. `std::string` and `std::vector`

These are the two **standard library** types you will use most, in place of C's character arrays and hand-allocated arrays. They
manage their own memory: you do not `malloc`/`free`.

```cpp
std::string name = "hero";
name += "_1";                                   // append to the string
std::vector<std::string> names{name, "slime"};  // initializer list
names.push_back("bat");                         // add to the end, grows on its own
for (const std::string &n : names)              // loop, without copying each string
  std::printf("%s (%zu characters)\n", n.c_str(), n.size());
```

Two things to know:

- **`.c_str()`** turns a `std::string` into a `const char *` for C-style functions. Most njin functions that take a path take
  `const char *path`. The Pong example keeps the save file path in a `std::string` and passes `.c_str()`:

  ```cpp
  g.save_file = save_path(ctx, "best_rally.txt");            // save_path returns std::string
  file_read(g.save_file.c_str(), text);                       // file_read takes const char *
  ```

  The pointer `.c_str()` returns is only valid while the string is alive and unchanged: do not keep it for long, see
  @ref learn_cpp_types.
- The loop `for (const std::string &n : names)` uses a **`const` reference**: it does not copy each string, and does not
  change it. Drop the `&` and every iteration copies the whole string.

`std::vector` is njin's main data type when it needs a "list": for example `std::vector<sys_fnc> after{}` in
`njin::sys_desc` (the list of systems that must run first).

## 8. Headers, `#pragma once` and anonymous namespaces

Headers in C++ work like in C: declarations in `.h`, definitions in `.cpp`. The difference is how double inclusion is prevented: instead of

```c
#ifndef NAME_H
#define NAME_H
...
#endif
```

almost every njin header starts with one line:

```cpp
#pragma once
```

When you define a function in a header, add `inline`, or two `.cpp` files that include it will clash at link time
(see @ref learn_errors, section `multiple definition`).

An **anonymous namespace** solves a similar problem for functions used **only in one file**. Two files both have a function `helper`:

File `one.cpp`:

```cpp
int helper() { return 1; }
int from_one() { return helper() * 10; }
```

File `two.cpp`:

```cpp
int helper() { return 2; }
int from_two() { return helper() * 10; }
```

File `main.cpp`:

```cpp
#include <cstdio>

int from_one();
int from_two();

int main() {
  std::printf("%d %d\n", from_one(), from_two());
}
```

Compile with `g++ one.cpp two.cpp main.cpp` and the linker reports an error:

```
...two.cpp:(.text+0x0): multiple definition of `helper()'; ...one.cpp:...
collect2.exe: error: ld returned 1 exit status
```

Put `helper` inside `namespace { ... }` in each file, changing nothing else:

File `one2.cpp`:

```cpp
namespace {
int helper() { return 1; }
} // namespace

int from_one() { return helper() * 10; }
```

File `two2.cpp`:

```cpp
namespace {
int helper() { return 2; }
} // namespace

int from_two() { return helper() * 10; }
```

Now `g++ one2.cpp two2.cpp main.cpp` runs and prints:

```
10 20
```

Everything in an anonymous namespace is **visible only in the file that contains it**, and each file has its own copy. (The C way is `static` before the function,
which also works in C++; C++ prefers anonymous namespaces.) If a function in there is never called, GCC warns
`defined but not used`, one more benefit.

**In njin**, game files use it everywhere. `src/games/pong/pong.cpp` wraps almost the whole file (from line 18 to line
398) in `namespace { ... }`: the state variable `game_state g`, and helper functions such as `make_blip` and `serve`. And
`src/games/platformer/play.cpp` wraps `build_player` and the other entity-building functions. That way those names do not clash
between files and do not leak out of the file that holds them.

## Self-check

1. What is the difference between `void f(vec2 *p)` and `void f(vec2 &p)` when calling and when used in the function body?
2. Why is `const vec2 &v` a more common parameter type than `vec2 v` for a large struct?
3. What does `int &r = a; r = b;` do: does `r` now refer to `b`, or does it copy `b` into `a`?
4. In njin, looking at the signatures `f32 delta(const context &ctx)` and `void time_set_scale(context &ctx, f32 scale)`, what can you conclude
   from `const` alone?
5. Why should you not write `using namespace njin;` in a header?

## Exercises

**Exercise 1.** Write `void heal(int &hp, int amount, int max_hp)` that adds health but never above `max_hp`, and
`bool is_dead(const int &hp)`. `hp` starts at 8: call `heal(hp, 5, 10)` and print the result.

**Exercise 2.** Write `const std::string *find_name(const std::vector<std::string> &names, const std::string &wanted)`
that returns a pointer to the name found, or `nullptr`. Try it with `{"hero", "slime", "bat"}`, searching for `"slime"` and `"ghost"`.

**Exercise 3.** The three files `one.cpp`, `two.cpp`, `main.cpp` in section 8 cause the error `multiple definition of 'helper()'`. Fix it **without
renaming the function** `helper`.

## Answers

**Questions.**

1. Pointer: you call `f(&a)` and use `p->x`, and it can be null. Reference: you call `f(a)` and use `p.x`, and there is always an object.
2. Passing by reference does not copy the whole struct, and `const` promises not to change it, so it is safe.
3. It copies `b` into `a`. A reference cannot switch objects after it is initialized (see the `a=10 b=2` example).
4. `delta` only **reads** engine state; `time_set_scale` **changes** it.
5. A header is included by many other files, so `using namespace` would apply to all of them too, and names could clash
   in unrelated places.

**Exercise 1.** Running it prints `hp = 10, dead = no` and then `dead = yes`:

File `learn_cpp_from_c_ex1.cpp`:

```cpp
#include <cstdio>

// Heal without going over max_hp. hp is a reference: the function changes the caller's variable directly.
void heal(int &hp, int amount, int max_hp) {
  hp += amount;
  if (hp > max_hp)
    hp = max_hp;
}

// Only reads hp, so const.
bool is_dead(const int &hp) { return hp <= 0; }

int main() {
  int hp = 8;
  heal(hp, 5, 10);
  std::printf("hp = %d, dead = %s\n", hp, is_dead(hp) ? "yes" : "no");
  int gone = 0;
  std::printf("dead = %s\n", is_dead(gone) ? "yes" : "no");
}
```

**Exercise 2.** Running it prints `hit: slime` and then `miss: (none)`:

File `learn_cpp_from_c_ex2.cpp`:

```cpp
#include <cstdio>
#include <string>
#include <vector>

// Return a pointer to the element found, or nullptr.
const std::string *find_name(const std::vector<std::string> &names, const std::string &wanted) {
  for (const std::string &n : names)
    if (n == wanted)
      return &n;
  return nullptr;
}

int main() {
  const std::vector<std::string> names{"hero", "slime", "bat"};
  const std::string *hit = find_name(names, "slime");
  const std::string *miss = find_name(names, "ghost");
  std::printf("hit: %s\n", hit ? hit->c_str() : "(none)");
  std::printf("miss: %s\n", miss ? miss->c_str() : "(none)");
}
```

**Exercise 3.** Put `helper` in an anonymous namespace in both `one.cpp` and `two.cpp`: these are exactly the two files `one2.cpp` and
`two2.cpp` in section 8, which print `10 20`. Another way, also correct: add `static` before `int helper()` in each file.

## Next steps

- @ref learn_cpp_types : values and references, copy and move, RAII, pointers and handles
- @ref learn_errors : reading compile errors and link errors
