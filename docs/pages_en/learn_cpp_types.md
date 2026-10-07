# Lesson 5: Values, references and ownership {#learn_cpp_types}

**What this lesson teaches**: copying or sharing, moving, RAII, who owns a resource and how long it lives; why
njin uses handles (ids) instead of pointers, and why every engine function takes a `context &`.

**What you need to know first**: references, `const`, `std::vector` and `std::string`, see @ref learn_cpp_from_c.

Most hard-to-find bugs in C++ come from two questions: **who owns this**, and **is it still alive**. This lesson answers
those two questions with examples that run, then shows how the njin engine answers them. Every example compiles with
`g++ -std=c++20 -Wall -Wextra -Wpedantic` (GCC 15.2) with no warnings, and the output below is the real output.

## One program with six ideas

@include learn_cpp_types.cpp

Output:

```
[1] values and references
a = 1,50  copy = 100,2
[2] copy and move
copy uses new memory:   yes
moved takes old memory: yes
a.size() after the move = 0
[3] RAII
 play(true):
  load hit.wav
  unload hit.wav
 play(false):
  load hit.wav
  load bgm.ogg
  playing
  unload bgm.ogg
  unload hit.wav
[4] ownership
  enemy 50 appears
  boss is null, owner has hp 50
  enemy 50 gone
  total hp = 7
[5] dangling references
capacity 3 -> 192
first element moved: yes
scores[index] = 10
[6] maybe nothing
optional: 2, empty
pointer:  20, nullptr
handle:   id 0 is invalid
```

## 1. Values and references

`vec2 copy = a;` **copies** `a` into a new variable: from then on the two variables are independent. `vec2 &alias = a;` **does not copy**: `alias`
is another name for `a` itself. Example `[1]` changes `copy.x` and `alias.y`: `a` only changes `y` (to 50), while `copy.x`
(set to 100) does not affect `a`.

The same rule applies to **function parameters**. `void f(vec2 v)` gets a copy (changing `v` does not affect the caller);
`void f(vec2 &v)` gets the variable itself (a change is a real change); `void f(const vec2 &v)` gets the variable itself but may only read it. Choose
by asking: does the function need to change the caller's variable? Yes: `&`. No, and the type is large: `const &`. No, and the type is small
like `int`, `float`, `vec2`: take it by value.

## 2. Copy and move

Copying a `std::vector` copies **every element** into new memory. With a vector of 1000 numbers that is still cheap; with a vector of 1 million
elements it is expensive. **Moving** is different: the new vector **takes over the memory** of the old one, copying nothing.

```cpp
std::vector<int> copy  = a;             // copy: new memory
std::vector<int> moved = std::move(a);  // takes a's memory
```

Example `[2]` checks this: `copy` uses different memory from `a` (`yes`), while `moved` uses **exactly the memory** that `a` used to
have (`yes`). `std::move` does not move anything by itself: it only says "you may take this variable's resources"; the taking is done by
the constructor.

After being moved from, `a` is in a valid but **unspecified** state: the run here shows `size() == 0`, but
do not rely on that value; treat `a` as no longer in use. You do not need to write move functions yourself; remember two things: (1) passing
and returning `std::vector`, `std::string` by value is usually cheap because they are moved, (2) calling `std::move` on a variable and then
using it again is a bug.

## 3. RAII: resources tied to a variable's lifetime

**RAII** (Resource Acquisition Is Initialization) means: acquire the resource in the **constructor**, release it in the
**destructor** (`~Name()`), which the compiler **always calls** when the variable goes out of scope, even when the function returns early. Example `[3]`:

```cpp
bool play(bool fail_early) {
  Sound hit("hit.wav");
  if (fail_early)
    return false;              // ~Sound still runs: "unload hit.wav"
  Sound bgm("bgm.ogg");
  std::puts("  playing");
  return true;
}                              // destroyed in reverse order: bgm, then hit
```

In the output: `play(true)` loads `hit.wav` and then unloads it right away, even though the function returns early. `play(false)` unloads `bgm.ogg` **before**
`hit.wav`, the reverse of the creation order. No `goto cleanup` as in C, and no way to forget a `free`.

`Sound` declares `Sound(const Sound &) = delete;`: **copying is forbidden**. The reason: if two objects held the same
resource, both would release it, which means releasing it twice. Trying to copy does not compile:

File `copyfail.cpp`:

```cpp
#include <cstdio>

struct Sound {
  Sound() { std::puts("load"); }
  ~Sound() { std::puts("unload"); }
  Sound(const Sound &) = delete;
  Sound &operator=(const Sound &) = delete;
};

int main() {
  Sound a;
  Sound b = a;
}
```

```
copyfail.cpp:12:13: error: use of deleted function 'Sound::Sound(const Sound&)'
   12 |   Sound b = a;
      |             ^
copyfail.cpp:6:3: note: declared here
    6 |   Sound(const Sound &) = delete;
      |   ^~~~~
```

**In njin**: `audio_store` (`src/engine/runtime/njin_audio_impl.h`) holds every loaded sound. It does exactly the above:
the destructor releases them, and copying is forbidden for that very reason:

```cpp
struct audio_store {
  std::vector<sound_slot> sounds;
  ...
  audio_store() = default;
  ~audio_store();
  // Copying would free the same audio buffers twice.
  audio_store(const audio_store &) = delete;
  audio_store &operator=(const audio_store &) = delete;
};
```

## 4. Ownership: `unique_ptr`, or better, a value

A raw pointer (`Enemy *`) does not say who must `delete` it. **`std::unique_ptr<T>`** says it clearly: exactly one owner, and when
that owner is destroyed, the object is destroyed too. It cannot be copied, only **transferred** with `std::move`:

```cpp
auto boss = std::make_unique<Enemy>(50);
std::unique_ptr<Enemy> owner = std::move(boss);  // boss becomes null, owner holds the Enemy
```

Output `[4]` shows `boss is null, owner has hp 50`, and the `Enemy` is destroyed **exactly once**, when `owner` leaves the block.
Copying is blocked:

File `uniquecopy.cpp`:

```cpp
#include <memory>

struct Enemy { int hp = 3; };

int main() {
  auto a = std::make_unique<Enemy>();
  auto b = a;
}
```

```
uniquecopy.cpp:7:12: error: use of deleted function 'std::unique_ptr<_Tp, _Dp>::unique_ptr(const std::unique_ptr<_Tp, _Dp>&) [with _Tp = Enemy; _Dp = std::default_delete<Enemy>]'
    7 |   auto b = a;
      |            ^
```

But most of the time **you do not need any pointer at all**. Three options, in the order to try them:

1. **A value**: `std::vector<Slime> wave(3);`. The vector owns its elements and destroys them when the vector is destroyed; you do not
   see a single pointer. This is the default.
2. **An id or handle** (section 7): when the thing being referred to may be deleted, or moved.
3. **`unique_ptr`**: when you need a single object that lives longer than the scope that created it, or whose type is only known at run time.

**In njin**, there is no `unique_ptr` in the engine's public headers (`src/engine/api`): entities are ids, resources
are handles, and data lives in a `std::vector` or in EnTT's registry.

## 5. Dangling references

A reference or pointer **dangles** when the thing it points to is no longer alive. Using it is undefined behavior: it may work
correctly a few times and then go wrong, or it may crash at once. The compiler catches some simple cases:

File `dangling_ref.cpp`:

```cpp
#include <cstdio>

const int &first_score() {
  int local = 7;
  return local;
}

int main() {
  std::printf("%d\n", first_score());
}
```

```
dangling_ref.cpp:5:10: warning: reference to local variable 'local' returned [-Wreturn-local-addr]
    5 |   return local;
      |          ^~~~~
dangling_ref.cpp:4:7: note: declared here
```

But many cases get **no warning at all**. A typical example: `const char *name = std::string("hero").c_str();`. The temporary
string is destroyed right at the end of the statement, and `name` dangles from the next line on. GCC 15.2 with `-Wall -Wextra -Wpedantic` stays silent when
compiling this line, so you have to remember the rule yourself: a pointer from `.c_str()` is only usable while the string is alive and unchanged.

**The real case you will meet most often in games: a vector reallocating.** A `std::vector` keeps its elements next to each other. When it is full, it
allocates a larger block of memory, copies the elements there, and **frees the old block**: every pointer and reference to an element
dangles. Example `[5]` (it does not read the old pointer, it only compares addresses):

```
capacity 3 -> 192
first element moved: yes
scores[index] = 10
```

The capacity jumps from 3 to 192 over several reallocations, and the first element **has moved**: an `int &` kept from before pushing
more elements would point into freed memory. The fix is to keep an **index** (or id) and fetch the element again each time you
need it: `scores[index]`. (The number 192 depends on the standard library; what does not change is that the capacity grows and the address changes.)

**This is exactly what the @ref ecs page warns about**: "Do not keep a reference to a component across frames: adding or removing components
can move the data. Fetch it from the registry again each frame." EnTT's registry is not a simple `std::vector`, so
we measured it on EnTT v4.0.0, the version njin uses, instead of guessing:

File `entt_move.cpp` (needs EnTT; compile with `-isystem` pointing to EnTT's `src` folder):

```cpp
#include <cstdint>
#include <cstdio>
#include <entt/entity/registry.hpp>

struct hp { int value = 3; };

int main() {
  entt::registry reg;
  const entt::entity first = reg.create();
  reg.emplace<hp>(first, hp{10});
  const auto a0 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(first));

  // 1. Add many components of the same type.
  for (int i = 0; i < 100000; i++)
    reg.emplace<hp>(reg.create(), hp{i});
  const auto a1 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(first));
  std::printf("after adding 100000 hp: first moved = %s\n", a0 != a1 ? "yes" : "no");

  // 2. Remove the component of another entity.
  const entt::entity second = reg.create();
  reg.emplace<hp>(second, hp{5});
  const entt::entity third = reg.create();
  reg.emplace<hp>(third, hp{6});
  const auto b0 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(third));
  reg.remove<hp>(second);
  const auto b1 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(third));
  std::printf("after removing another entity's hp: third moved = %s, value still %d\n",
              b0 != b1 ? "yes" : "no", reg.get<hp>(third).value);
}
```

```
after adding 100000 hp: first moved = no
after removing another entity's hp: third moved = yes, value still 6
```

A surprising result worth remembering: **adding** components of the same type does not move the old elements (unlike `std::vector`), but **removing**
an entity's component does move the component of *another* entity: EnTT fills the gap with the last element and then shrinks
the array. An `hp &` kept for `third` from before the `remove` call now points to a spot that no longer belongs to it. The value is still correct
(`value still 6`) when fetched again with `reg.get`. So the safe rule stays the same and does not depend on internal details: njin games
**store `entt::entity` (an id), not `transform &`**, and call `reg.get<transform>(e)` whenever they need it.

## 6. Three ways to say "maybe nothing"

A search function may find nothing. C++ has three ways to answer that, and all three appear in example `[6]`:

| Way | Example | When |
|---|---|---|
| Pointer, `nullptr` means "nothing" | `const int *find_ptr(...)` | The result is an element already in a container. EnTT's way: `try_get<T>(e)` |
| `std::optional<T>` | `std::optional<int> find_index(...)` | The result is a newly computed **value**. `has_value()`, `value_or(x)` |
| Handle with `id == 0` | `struct sound_handle { unsigned id = 0; }` | An identifier for a resource. njin's way |

The example `find_index` returns `std::optional<int>`: `2` when found, "empty" when not, with no special value like `-1` needed. None of the three
is better than the others; choose based on where it is used. njin uses the third kind for resources, read on.

## 7. Handles: a slot store and ids that are never reused

Resources such as sounds, textures and shaders live inside the engine. A game must not hold pointers to them (section 5: a pointer to a
vector element can dangle). Instead `sound_load` returns a **handle**: a `struct` that holds only a number `id`. In
`src/engine/api/_types.h`:

```cpp
struct sound_handle {
  u32 id = 0; ///< 0 means invalid.
};
```

`id == 0` means "nothing" (load failed, file missing). Inside, the engine keeps a `std::vector` of **slots**, and id N is slot
`N - 1`. The important rule, written at the very top of `njin_audio_impl.h`: **a slot is never reused**, so an old handle
can never accidentally point to a new resource. Here is a small store built exactly that way:

@include learn_cpp_handles.cpp

Output:

```
hit.id = 1, bgm.id = 2
coin.id = 3 (does not reuse hit's id)
hit still valid: no
bgm still valid: yes
coin volume = 1.0
bgm's slot moved: yes
bgm still usable through its handle: yes
```

Reading the result:

- After `sound_unload(hit)`, loading `coin` gets `id 3`, and does **not** take back `id 1`. The old handle `hit` looks up to `nullptr`, and
  functions given a bad handle (`sound_set_volume` with `hit` or with `{}`) **ignore it** instead of causing an error.
- After loading 100 more sounds, `bgm`'s slot **has moved** (the vector reallocated), so a pointer kept from before would dangle.
  But the handle `bgm` still works because it is a number, looked up again each time.

njin's lookup function is just like the small version above:

```cpp
inline sound_slot *sound_slot_of(audio_store &store, sound_handle handle) {
  if (handle.id == 0 || handle.id > store.sounds.size())
    return nullptr;
  sound_slot &slot = store.sounds[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}
```

This is why njin's API docs say "handle id 0 is invalid, and every function given an invalid handle does nothing",
and this is the cost: each use needs one lookup. In return, a game never has to think about the lifetime of resources
inside the engine.

The pointer that `sound_slot_of` returns is also used **right away**, not stored: that is the "fetch it again each time" rule from section 5.

## 8. Why njin passes `context &` instead of using global variables

The engine's state (window, resource stores, time, registry...) lives in one object, `context`, and every engine
function takes it. The header `njin_ctx.h` says:

```cpp
// Opaque: created with create (njin.h), only accessed through the
// functions below.
struct context;
```

and every game system is `void system(context &ctx)`. Compare the two ways of keeping state:

@include learn_cpp_context.cpp

Output:

```
global: frame = 2, 2 events
game:   frame = 3, last event = 30
replay: frame = 1, last event = 10
```

Global variables (`g_frame`) have only **one** set of state for the whole program. With `Ctx` you can create as many as you like, each
independent: `game` and `replay` run side by side without affecting each other. Passing a context also makes **dependencies visible right
in the function's signature** (`last_event(const Ctx &)` says it reads the context and does not change it), and makes testing easier, since you create a fresh `Ctx`
for each test.

This is a trade-off, not a law: for a small game, global variables are simpler. njin games also use global variables for state
that **belongs to the game** (`game_state g;` in `src/games/pong/pong.cpp`), while what the **engine** owns lives in
`context`, at the cost of writing `ctx` at the start of every function. Note also: `context` is declared but not defined in the public header
("opaque"). A game cannot create one itself or read its fields, only use the functions the engine provides, so the engine can change how it stores things
without breaking games.

## Self-check

1. How do `void f(vec2 v)`, `void f(vec2 &v)` and `void f(const vec2 &v)` differ when the function changes `v.x`?
2. After `auto b = std::move(a);` (with `a` a `std::vector`), what are you allowed to do with `a`?
3. Why does `Sound(const Sound &) = delete;` make sense for a class that holds a resource?
4. What is wrong here? `int &first = scores[0]; scores.push_back(5); first = 1;`
5. Why do njin handles use id 0 as "invalid", and why must slots not be reused?

## Exercises

**Exercise 1.** Guess the output, then run it to check:

```cpp
#include <cstdio>

struct vec2 { float x, y; };

void bump(vec2 v) { v.x += 1; }
void bump_ref(vec2 &v) { v.x += 1; }

int main() {
  vec2 a{0, 0};
  bump(a);
  bump_ref(a);
  bump_ref(a);
  std::printf("%.0f\n", a.x);
}
```

**Exercise 2.** Write a `struct Scope` that prints `enter <name>` in its constructor and `leave <name>` in its destructor. Create two nested `Scope`s
(`outer`, then `inner` in an inner `{ }` block) and guess the order of the printed lines.

**Exercise 3.** Write `unsigned find_id(const std::vector<std::string> &names, const std::string &wanted)` that returns an `id` the njin
way: the position plus 1 if found, **0** if not. Try it with `{"hero", "slime", "bat"}`, searching for `"bat"` and `"ghost"`.

## Answers

**Questions.**

1. `v` by value: it changes a copy, and the caller sees nothing. `vec2 &`: it changes the caller's variable itself. `const vec2 &`: it does not
   compile, because `const` forbids the change.
2. Treat `a` as abandoned. It is valid to destroy or assign again, but do not read its value, because its state is unspecified.
3. If it could be copied, two objects would hold the same resource and both would release it, that is, twice.
4. `first` is a reference to an element of the vector. `push_back` may make the vector reallocate; then `first` dangles, and `first = 1`
   writes into freed memory. Fix: keep the index `0`, write `scores[0] = 1`.
5. Id 0 lets `{}` (the default) mean "nothing" without needing any other special value. Slots are not reused so that an old handle
   never points by mistake to a resource loaded later.

**Exercise 1.** It prints `2`. `bump(a)` only changes a copy. The two `bump_ref` calls each add 1 to the real `a.x`.

**Exercise 2.** Destruction happens in the reverse order of creation. Here is one version (with an extra `between` line between the two destructions to show clearly
that `inner` is destroyed at the `}` of its block):

File `learn_cpp_types_ex2.cpp`:

```cpp
#include <cstdio>

struct Scope {
  const char *name;
  explicit Scope(const char *n) : name(n) { std::printf("enter %s\n", name); }
  ~Scope() { std::printf("leave %s\n", name); }
};

int main() {
  Scope outer("outer");
  {
    Scope inner("inner");
  }
  std::puts("between");
}
```

Running it prints `enter outer`, `enter inner`, `leave inner`, `between`, `leave outer`: `inner` is destroyed at the `}` of its block, while
`outer` is destroyed at the end of `main`.

**Exercise 3.** Running it prints `bat -> 3` and then `ghost -> 0`:

File `learn_cpp_types_ex3.cpp`:

```cpp
#include <cstdio>
#include <string>
#include <vector>

// Return an id the njin way: position + 1 if found, 0 if not (0 means invalid).
unsigned find_id(const std::vector<std::string> &names, const std::string &wanted) {
  for (std::size_t i = 0; i < names.size(); i++)
    if (names[i] == wanted)
      return static_cast<unsigned>(i + 1);
  return 0;
}

int main() {
  const std::vector<std::string> names{"hero", "slime", "bat"};
  std::printf("bat -> %u\n", find_id(names, "bat"));
  std::printf("ghost -> %u\n", find_id(names, "ghost"));
}
```

## Next steps

- @ref learn_cpp_modern : `auto`, lambdas, structured bindings, `constexpr`, the things that keep njin code short
- @ref learn_errors : reading compile errors and link errors
