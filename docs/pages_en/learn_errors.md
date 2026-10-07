# Lesson 7: Reading compile errors and link errors {#learn_errors}

**What this lesson teaches**: reading GCC's error messages (compile, link, run time) so you find the wrong line yourself
instead of guessing, and a debugging workflow made of `-Wall`, `printf` and `gdb`.

**What you need to know first**: you have written and compiled a small C++ program, see @ref learn_cpp_from_c.

Every message in this lesson is **real output** from GCC 15.2 (w64devkit, Windows), run with
`g++ -std=c++20 -Wall -Wextra -Wpedantic`. Only the length has been trimmed (cuts are marked `...`). On another operating
system or GCC version the wording may differ slightly (the path of `ld`, temporary file names), but you read it the same way.

## Three stages, three kinds of error

One `g++ main.cpp -o game` command does three jobs in a row, and errors at each stage look different:

| Stage | What it does | What the error looks like |
|---|---|---|
| **Compile** | Turns each `.cpp` file into an object file `.o` | `file.cpp:5:3: error: ...` |
| **Link** | Joins the `.o` files and libraries into an executable | `undefined reference to ...`, `multiple definition of ...` |
| **Run** | Runs the program | The program dies: `Segmentation fault`, `Assertion failed` |

Knowing which stage you are at is half of fixing the error: a link error cannot be fixed by rereading a line of code,
and a compile error has nothing to do with a file missing from the build. Split the two stages to see them:

```
g++ -std=c++20 -c main.cpp     (compile only, produces main.o)
g++ main.o -o game             (link only)
```

## Reading a GCC message

A program missing a semicolon:

File `syntax.cpp`:

```cpp
#include <cstdio>

int main() {
  int lives = 3
  std::printf("%d", lives);
  return 0;
}
```

```
syntax.cpp: In function 'int main()':
syntax.cpp:5:3: error: expected ',' or ';' before 'std'
    5 |   std::printf("%d", lives);
      |   ^~~
syntax.cpp:4:7: warning: unused variable 'lives' [-Wunused-variable]
    4 |   int lives = 3
      |       ^~~~~
```

Read it part by part:

- `syntax.cpp:5:3:` is the **file, line, column**. Line 5, column 3.
- `error:` or `warning:`: an error produces no executable; a warning does, but it is often a real bug waiting to happen.
- Then comes the message, then a **quote of the code line** with a `^~~~` marker pointing at the spot.
- `[-Wunused-variable]` is the name of the flag that produced the warning. It is the keyword to look up.
- `note:` is extra information attached to the error above it (for example "declared here").

Notice the most important thing in this example: the `;` is missing on **line 4**, but the error is reported on **line 5**.
The compiler only knows the statement ended wrongly when it reads `std` on the next line. So the rule is:

> If you cannot find a syntax error on the reported line, look at **the line just before it**.

## Five rules for reading errors

**1. Read the first error.** One error can drag dozens of errors after it, because the compiler has misunderstood the
code from that point on. Fix the first error and compile again; do not fix error 30 before you have looked at error 1.

**2. Cascading errors.** This is a `struct` missing its `}` on line 3:

File `cascade.cpp`:

```cpp
#include <vector>

struct Enemy {
  int hp;
  Enemy(int h) : hp(h) {}

Enemy make_boss() { return Enemy(50); }

int main() {
  std::vector<Enemy> wave;
  wave.push_back(make_boss());
  return wave.size();
}
```

```
cascade.cpp:13:2: error: expected '}' at end of input
   13 | }
      |  ^
cascade.cpp:3:14: note: to match this '{'
    3 | struct Enemy {
      |              ^
cascade.cpp:13:2: error: expected unqualified-id at end of input
```

The error is reported at the **end of the file** (line 13), not where the brace is missing. Only the `note:` line points
at the real cause: the `{` on line 3 has no matching `}`. So `note:` matters as much as `error:`.

**3. Do not ignore warnings.** This function forgets to `return` when `lives` is 0:

File `noreturn.cpp`:

```cpp
#include <cstdio>

int lives_left(int lives) {
  if (lives > 0)
    return lives - 1;
}

int main() {
  std::printf("%d", lives_left(0));
}
```

```
noreturn.cpp:6:1: warning: control reaches end of non-void function [-Wreturn-type]
    6 | }
      | ^
```

It compiles. But when it runs, **the program dies** right away on this machine:

```
Illegal instruction
```

Running off the end of a function without a `return` is undefined behavior: each compiler and each optimization level
gives a different result. So always compile with `-Wall -Wextra` and treat warnings as errors.

**4. "In file included from".** When the error is inside a header, the message shows the whole include chain:

File `bad.h`:

```cpp
#pragma once
inline int twice(int x) { return x * 2 }
```

```
In file included from inc.cpp:1:
bad.h: In function 'int twice(int)':
bad.h:2:39: error: expected ';' before '}' token
    2 | inline int twice(int x) { return x * 2 }
      |                                       ^~
      |                                       ;
```

The first line only says this header was pulled in from `inc.cpp` line 1. The real error is at `bad.h:2`, and GCC even
suggests the `;` you need to add.

**5. Template errors are very long: find your own line.** Putting a string into a `std::vector<int>`:

File `tmpl.cpp`:

```cpp
#include <vector>

int main() {
  std::vector<int> scores;
  scores.push_back("ten");
}
```

```
tmpl.cpp:5:19: error: no matching function for call to 'push_back(const char [4])'
    5 |   scores.push_back("ten");
      |   ~~~~~~~~~~~~~~~~^~~~~~~~
tmpl.cpp:5:19: note: there are 2 candidates
In file included from C:/Dev/raylib/w64devkit/lib/gcc/x86_64-w64-mingw32/15.2.0/include/c++/vector:68,
                 from tmpl.cpp:1:
...stl_vector.h:1416:7: note: candidate 1: 'constexpr void std::vector<_Tp, _Alloc>::push_back(const value_type&) ...
...
tmpl.cpp:5:20: error: invalid conversion from 'const char*' to 'std::vector<int>::value_type' {aka 'int'} [-fpermissive]
```

Most of the text is inside the standard library (`stl_vector.h`). All you need is the first `error:` line and **the lines
that are in your own file** (`tmpl.cpp:5`). The wording is enough to understand it: "there is no `push_back` that takes a
`const char[4]`, and a string cannot be converted to an `int`".

## Common compile errors

**Undeclared.** You use a name the compiler has not seen yet: you forgot an `#include`, forgot `std::`, or made a typo:

File `undeclared.cpp`:

```cpp
#include <cstdio>

int main() {
  int lives = 3;
  cout << lives;
  scoreboard = 10;
  return 0;
}
```

```
undeclared.cpp:5:3: error: 'cout' was not declared in this scope
    5 |   cout << lives;
      |   ^~~~
undeclared.cpp:6:3: error: 'scoreboard' was not declared in this scope
```

The fix: `cout` needs `#include <iostream>` and `std::cout`; `scoreboard` was never declared (a typo in the name?).
With njin, this error usually means a missing `#include <njin.h>` or a missing `njin::` in front of the name.

**Wrong type and wrong number of arguments.**

File `mismatch.cpp`:

```cpp
#include <string>

struct vec2 { float x, y; };

void move(vec2 &pos, float dx, float dy) { pos.x += dx; pos.y += dy; }

int main() {
  int lives = "three";
  vec2 p{0, 0};
  move(p, 1.0f);
  return lives;
}
```

```
mismatch.cpp:8:15: error: invalid conversion from 'const char*' to 'int' [-fpermissive]
    8 |   int lives = "three";
      |               ^~~~~~~
mismatch.cpp:10:7: error: too few arguments to function 'void move(vec2&, float, float)'
   10 |   move(p, 1.0f);
      |   ~~~~^~~~~~~~~
mismatch.cpp:5:6: note: declared here
    5 | void move(vec2 &pos, float dx, float dy) { pos.x += dx; pos.y += dy; }
```

The second error has `note: declared here`: it points you to where the function is declared so you can compare it with
the call. This is a general pattern: **`error:` says where the call is wrong, `note:` says what it is being compared
with.**

**Violating `const`.**

File `constviol.cpp`:

```cpp
struct vec2 { float x, y; };

void reset(const vec2 &pos) {
  pos.x = 0;
}

int main() {
  vec2 p{1, 2};
  reset(p);
}
```

```
constviol.cpp:4:9: error: assignment of member 'vec2::x' in read-only object
    4 |   pos.x = 0;
      |   ~~~~~~^~~
```

The function promises "read only" (`const vec2 &`) and then modifies it. Fix the function to match what you intend: if it
needs to modify, drop the `const`.

## Two common errors when using njin

Both of these errors really came up while writing njin's tutorials.

**"only 3 names provided for structured binding".** Iterating `view<A, B, C>().each()` gives you **the entity plus three
components**, that is four elements, so `auto [e, a, b]` is one name short. Reproduced with a plain `std::tuple`:

File `sb.cpp`:

```cpp
#include <tuple>

int main() {
  std::tuple<int, float, float, int> row{1, 2.0f, 3.0f, 4};
  auto [entity, pos, vel] = row;
  return 0;
}
```

```
sb.cpp:5:8: error: only 3 names provided for structured binding
    5 |   auto [entity, pos, vel] = row;
      |        ^~~~~~~~~~~~~~~~~~
sb.cpp:5:8: note: while 'std::tuple<int, float, float, int>' decomposes into 4 elements
```

The fix: give enough names (remember the first one is the entity), or use the lambda form `each([](A &a, B &b, C &c) {...})`.
See @ref ecs.

**"invalid use of void expression" inside the library, not in your code.** `get` or `try_get` on an empty component (a
tag, `struct player_tag {};`) has nothing to return. This is real output, compiled with EnTT:

File `entt_tag.cpp` (needs EnTT, compile with `-isystem` pointing at EnTT's `src` folder):

```cpp
#include <entt/entity/registry.hpp>

struct player_tag {};

int main() {
  entt::registry reg;
  const entt::entity e = reg.create();
  reg.emplace<player_tag>(e);
  return reg.try_get<player_tag>(e) == nullptr;
}
```

```
In file included from entt_tag.cpp:1:
.../entt/entity/registry.hpp: In instantiation of 'auto entt::basic_registry<...>::try_get(entity_type) const ...
.../entt/entity/registry.hpp:911:83:   required from 'auto entt::basic_registry<...>::try_get(entity_type) [with Type = {player_tag}
  911 |             return (const_cast<Type *>(stl::as_const(*this).template try_get<Type>(entt)), ...);
entt_tag.cpp:9:33:   required from here
    9 |   return reg.try_get<player_tag>(e) == nullptr;
      |          ~~~~~~~~~~~~~~~~~~~~~~~^~~
.../entt/entity/registry.hpp:901:80: error: invalid use of void expression
  901 |             return (cpool && cpool->contains(entt)) ? stl::addressof(cpool->get(entt)) : nullptr;
```

The line with `error:` is in `registry.hpp`, EnTT's code. Do not fix it there. Look for the **`required from here`** line
and your own `.cpp`: `entt_tag.cpp:9:33`, exactly where `try_get` is called on a tag. The fix: ask `reg.all_of<player_tag>(e)`.

The message `invalid use of void expression` itself appears when you pass a `void` expression as a value:

File `void_arg.cpp`:

```cpp
#include <utility>

void tick() {}

int main() {
  auto p = std::make_pair(1, tick());
  return p.first;
}
```

```
void_arg.cpp:6:34: error: invalid use of void expression
    6 |   auto p = std::make_pair(1, tick());
      |                              ~~~~^~
```

A function that returns `void` has no value to assign (`int r = tick();` reports `void value not ignored as it ought to be`).

## Errors in C

C also uses GCC, so you read the errors the same way. One difference worth knowing: calling an undeclared function. With
`gcc -std=c17`:

File `implicit.c`:

```c
int main(void) {
  printf("hello");
  return 0;
}
```

```
implicit.c:2:3: error: implicit declaration of function 'printf' [-Wimplicit-function-declaration]
    2 |   printf("hello");
      |   ^~~~~~
implicit.c:1:1: note: include '<stdio.h>' or provide a declaration of 'printf'
  +++ |+#include <stdio.h>
    1 | int main(void) {
```

GCC even shows the `#include <stdio.h>` line you need to add. (Older GCC versions only warn instead of failing; GCC 15.2
reports an error.)

## Link errors

Everything compiled, yet `g++` still reports an error, and the message has no line number: that is a link error.

**`undefined reference`**: the program calls a function that has **no definition** in any of the files passed to the
linker. A declaration (`int score(int lives);`) is not enough; there must be a definition somewhere.

File `undef_main.cpp`:

```cpp
#include <cstdio>

int score(int lives);

int main() {
  std::printf("%d", score(3));
}
```

```
...ld.exe: ...undef_main.cpp:(.text+0x13): undefined reference to `score(int)'
collect2.exe: error: ld returned 1 exit status
```

`ld` is the linker program. The name in quotes is the missing function, **with its parameter types**: `score(int)`. Three
common causes:

1. The definition was never written, or the `.cpp` file that holds it **was not added to the build** (with CMake: it is
   not in `add_executable`, see @ref learn_cmake_basics).
2. The definition exists but has a **different signature** (parameter types, `const`, namespace): the name `score(int)` must
   match exactly.
3. A **library** is missing: the function lives in a library that was not linked.

Case 3 really happens with njin. This is the docs' `minimal_main.cpp`, with the right include path but without linking
the engine:

```
...minimal_main.cpp:(.text+0xb0): undefined reference to `njin::create(njin::config const&)'
...minimal_main.cpp:(.text+0xc0): undefined reference to `njin::run(njin::context&)'
...minimal_main.cpp:(.text+0xcc): undefined reference to `njin::destroy(njin::context*)'
collect2.exe: error: ld returned 1 exit status
```

The `njin::` prefix shows those functions belong to the engine: you need `target_link_libraries(... njin::rt)` in CMake. If
the include path is missing too, the error happens earlier, at the compile stage:

```
minimal_main.cpp:1:10: fatal error: njin.h: No such file or directory
    1 | #include <njin.h>
      |          ^~~~~~~~
compilation terminated.
```

**`multiple definition`**: **two** files define the same function. This happens most often when someone writes a function
body in a header:

File `util.h`:

```cpp
#pragma once
int square(int x) { return x * x; }
```

File `a.cpp`:

```cpp
#include "util.h"
int from_a() { return square(2); }
```

File `b.cpp`:

```cpp
#include "util.h"
int from_b() { return square(3); }
int from_a();
int main() { return from_a() + from_b(); }
```

```
...b.cpp:(.text+0x0): multiple definition of `square(int)'; ...a.cpp:(.text+0x0): first defined here
collect2.exe: error: ld returned 1 exit status
```

`#pragma once` only prevents including twice **within one `.cpp` file**. Each `.cpp` still gets its own copy of `square`,
and then the linker sees two copies. Fix it with `inline` (for small functions in a header) or move the function body into
one `.cpp` file. With `inline`, the program above links and returns 13 (4 + 9). That is why njin writes `inline constexpr` for constants in headers.

## Runtime errors

The program has already started running when it dies. The message has no line number.

**Accessing a null pointer (segmentation fault).**

File `crash.cpp`:

```cpp
#include <cstdio>

struct Enemy { int hp = 3; };

Enemy *find_enemy(int id) {
  return id == 1 ? new Enemy : nullptr;
}

int main() {
  Enemy *e = find_enemy(2);
  std::printf("hp = %d", e->hp);
  return 0;
}
```

Run in Git Bash, there is just one line `Segmentation fault` and exit code 139. In PowerShell, the same program gives
exit code `-1073741819` (that is `0xC0000005`, Windows' memory access error). The exit code does not help you find out
**where**: that is the job of `gdb` (next section).

**A failed `assert`.** `assert(condition)` stops the program if the condition is false, and tells you which condition on
which line:

File `asrt.cpp`:

```cpp
#include <cassert>
#include <vector>

int main() {
  std::vector<int> scores{10, 20};
  int index = 2;
  assert(index < static_cast<int>(scores.size()) && "index out of range");
  return scores[index];
}
```

```
Assertion failed: index < static_cast<int>(scores.size()) && "index out of range", file asrt.cpp, line 7
```

The `&& "message"` trick adds words to the message. `assert` only exists in debug builds: compile with `-DNDEBUG`
(njin's release build does this) and it disappears. Libraries use it to catch programming mistakes. This is the real message
when calling `registry.get<X>(e)` on an entity that has no `X` (see @ref ecs):

```
Assertion failed: ((contains(entt)) && ("Set does not contain entity")), file .../sparse_set.hpp, line 721
```

When you see `Assertion failed` from a library, read the condition and the name of the function that leads to it, then find
the call in your own code.

## Debugging workflow

1. **Compile with `-g -Wall -Wextra`**. `-g` gives `gdb` function names and line numbers; `-Wall -Wextra` turns on warnings
   (see rule 3).
2. **Read the first error** and the line just before it.
3. **Shrink it**: cut the program down to the fewest lines that still cause the error. Many bugs reveal themselves as you cut.
4. **Print**: `std::printf("x = %d\n", x);` before the suspicious line. Crude but effective.
5. **Use `gdb`** when the program dies and you do not know where.

`gdb` (included in w64devkit) runs the program and stops exactly where it dies. The following program calls two layers of
functions before accessing a null pointer:

File `deep.cpp`:

```cpp
#include <cstdio>

struct Enemy { int hp = 3; };

int hit_points(const Enemy *e) { return e->hp; }
int report(const Enemy *e) { return hit_points(e); }

int main() {
  Enemy *target = nullptr;
  std::printf("hp = %d", report(target));
  return 0;
}
```

Compile with `g++ -std=c++20 -g deep.cpp -o deep.exe`, then:

```
gdb -batch -ex run -ex bt -ex "print e" -ex "up 2" -ex "info locals" ./deep.exe
```

```
Thread 1 received signal SIGSEGV, Segmentation fault.
0x00007ff626c3149c in hit_points (e=0x0) at deep.cpp:5
5	int hit_points(const Enemy *e) { return e->hp; }
#0  0x00007ff626c3149c in hit_points (e=0x0) at deep.cpp:5
#1  0x00007ff626c314b8 in report (e=0x0) at deep.cpp:6
#2  0x00007ff626c314df in main () at deep.cpp:10
$1 = (const Enemy *) 0x0
#2  0x00007ff626c314df in main () at deep.cpp:10
10	  std::printf("hp = %d", report(target));
target = 0x0
```

Addresses like `0x00007ff626c3149c` change on every run, so ignore them. You only need to read **function names, line
numbers and variable values**.

| Command | What it does |
|---|---|
| `run` | Runs the program |
| `bt` | Prints the **call chain** (backtrace) where it died: `#0` is where it died, `#2` is `main` |
| `print e` | Prints a variable's value. Here `e` is a null pointer (`0x0`) |
| `up 2` | Moves up two levels in the call chain, to see `main`'s variables |
| `info locals` | Prints every local variable of the level you are looking at: `target = 0x0` |
| `break report` | Stops before entering the function `report` (then `next` to step line by line) |

Reading the result: the program died in `hit_points` line 5 because `e` is null; it was called from `report`, which was called
from `main` line 10, where `target` was null from the start. The real bug is in `main`, not where it died.

**Memory checkers (sanitizers).** GCC and Clang have `-fsanitize=address,undefined`, which catches memory errors and
undefined behavior the moment they happen. On the w64devkit GCC used for this lesson, that flag **does not work**:

```
ld.exe: cannot find -lasan
```

Linux builds of GCC usually have it, but it has not been tried in this lesson. On Windows with w64devkit, use `-Wall`,
`assert` and `gdb`.

## Self-check

1. A syntax error is reported on line 12, but line 12 looks right. Where should you look?
2. There are 30 `error:` lines. Which do you read first? Why?
3. Is `undefined reference to 'score(int)'` a compile error or a link error? Name two possible causes.
4. When does `multiple definition of 'square(int)'` happen, and how do you fix it?
5. The program prints `Segmentation fault`. Which command tells you the line that caused it, and which flag do you need to
   compile with so that command can print line numbers?

## Exercises

**Exercise 1: three compile errors.** The following program has three errors. Compile it, read the messages, fix each error:

File `ex1.cpp`:

```cpp
#include <cstdio>

struct Player {
  int lives = 3;
  float speed = 90.0f
};

int main() {
  Player p;
  int coins = "five";
  cout << p.lives << coins;
  return 0;
}
```

**Exercise 2: a link error.** These two files each compile on their own, but `g++ main2.cpp score2.cpp` reports
`undefined reference`. Find out why (hint: compare `total_score(...)` in the message with the definition) and fix it:

File `main2.cpp`:

```cpp
#include <cstdio>

int total_score(const int *values, int count);

int main() {
  int scores[] = {10, 20, 30};
  std::printf("%d", total_score(scores, 3));
}
```

File `score2.cpp`:

```cpp
unsigned total_score(const int *values, unsigned count) {
  unsigned sum = 0;
  for (unsigned i = 0; i < count; i++)
    sum += values[i];
  return sum;
}
```

**Exercise 3: using `gdb`.** This program dies. Compile with `-g`, use `gdb -batch -ex run -ex bt` to find the line and the
null variable, then fix it so it prints "no enemies" when the wave is empty:

File `ex3.cpp`:

```cpp
#include <cstdio>
#include <vector>

struct Enemy {
  int hp = 3;
  float x = 0;
};

Enemy *nearest(std::vector<Enemy> &wave, float x) {
  Enemy *best = nullptr;
  for (Enemy &e : wave)
    if (best == nullptr || e.x - x < best->x - x)
      best = &e;
  return best;
}

int main() {
  std::vector<Enemy> wave;
  Enemy *target = nearest(wave, 10.0f);
  std::printf("target hp = %d\n", target->hp);
}
```

## Answers

**Questions.**

1. On **the line just before it** (usually a missing `;` or `)`), because the compiler only notices the error when it reads
   the next line. If it is `expected '}' at end of input`, read the `note: to match this '{'` line.
2. The first one: the later errors are often consequences of it.
3. A link error. The function has no definition (or the file that holds it was not added to the build), or the definition
   has a different signature (parameter types, `const`, namespace), or the function lives in a library that was not linked.
4. When two `.cpp` files both contain a definition of the same function, usually because the function body is in a header.
   Fix: add `inline`, or move the function body into one `.cpp` file and leave only the declaration in the header.
5. `gdb -batch -ex run -ex bt ./your_program`. You need `-g` when compiling.

**Exercise 1.** GCC reports three errors: `expected ';' at end of member declaration` on line 5, `invalid conversion from
'const char*' to 'int'` on line 10, `'cout' was not declared in this scope` on line 11. Here is the fixed version, which prints `3 5`:

File `ex1_fixed.cpp`:

```cpp
#include <cstdio>

struct Player {
  int lives = 3;
  float speed = 90.0f;
};

int main() {
  Player p;
  int coins = 5;
  std::printf("%d %d\n", p.lives, coins);
  return 0;
}
```

**Exercise 2.** The message is ``undefined reference to `total_score(int const*, int)'``, but `score2.cpp` defines
`total_score(const int *, unsigned)`. The parameter types differ (`int` and `unsigned`), so they are two different functions.
Make both sides match. The fixed `score2.cpp` (`main2.cpp` stays the same) prints `60`:

File `score2_fixed.cpp`:

```cpp
int total_score(const int *values, int count) {
  int sum = 0;
  for (int i = 0; i < count; i++)
    sum += values[i];
  return sum;
}
```

**Exercise 3.** `gdb` reports `main () at ex3.cpp:20`, the line `std::printf("target hp = %d\n", target->hp);`. `nearest`
returns `nullptr` when `wave` is empty, and `main` does not check. The fixed version prints `no enemies`:

File `ex3_fixed.cpp`:

```cpp
#include <cstdio>
#include <vector>

struct Enemy {
  int hp = 3;
  float x = 0;
};

Enemy *nearest(std::vector<Enemy> &wave, float x) {
  Enemy *best = nullptr;
  for (Enemy &e : wave)
    if (best == nullptr || e.x - x < best->x - x)
      best = &e;
  return best;
}

int main() {
  std::vector<Enemy> wave;
  Enemy *target = nearest(wave, 10.0f);
  if (target == nullptr)
    std::printf("no enemies\n");
  else
    std::printf("target hp = %d\n", target->hp);
}
```

## Next steps

- @ref learn_cmake_basics : write a build file to compile and link many files without typing long commands
- @ref learn_cpp_modern : if you have not read it yet, syntax such as `auto [a, b]` and lambdas shows up a lot in the errors above
