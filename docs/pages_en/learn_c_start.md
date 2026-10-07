# Lesson 1: Your first C program {#learn_c_start}

**What this lesson teaches:** writing, compiling and running a C program; data types, `printf`, `if`, `for`, `while` and functions.

**What you need to know first:** how to open a terminal, and a compiler installed (see @ref setup). No programming knowledge needed.

This is the first lesson in the "foundations" group: C, C++, CMake and shaders, read in order before you start on
njin (the table of contents is at @ref learn). If you already know a topic, skip its lesson. Every example runs with just a
compiler, no njin needed. The examples below were compiled with GCC 15.2 on Windows (w64devkit) using
`gcc -std=c17 -Wall -Wextra`, with no warnings.

## Why learn C to make games with njin?

njin is written in C++, but it stands on **raylib**, a pure C library. C++ contains nearly all of C, so
everything in this lesson still applies unchanged when you write a game:

- File paths and strings are still C style: `const char *path`, for example `texture_load(ctx, "assets/tiles.png")`.
- Logging uses `printf` format strings: `NJIN_INFO("player spawned at %.1f, %.1f", x, y)`.
- Number types such as `int32_t` and `float` come from C. njin only gives them short names (`i32`, `f32`).

## The first program

@include learn_c_hello.c

Save it as `hello.c`, then in the terminal:

```
gcc -std=c17 -Wall -Wextra hello.c -o hello
./hello
```

```
Hello, C!
```

(On Windows, GCC makes `hello.exe`; run it with `hello` or `.\hello`.) Piece by piece:

| Part | Meaning |
|---|---|
| `#include <stdio.h>` | brings in the declaration of `printf` from the standard library; without this line the compiler does not know what `printf` is |
| `int main(void)` | the starting point: the program runs from here; `void` means "takes no parameters" |
| `printf("...\n")` | prints to the screen; `\n` is a line break |
| `return 0;` | tells the operating system "success"; anything other than 0 means "there was an error" |

Three `gcc` flags worth remembering: `-std=c17` picks the language version, `-Wall -Wextra` turn on warnings. **Always turn
on warnings**: a great many C bugs are reported there, before the program runs wrong.

## Data types and `printf`

@include learn_c_types.c

```
lives=3 speed=110.500000 speed(2 digits)=110.50
grade=A code=65 alive=1
score=1250 red=255 hex=ff
7 / 2 = 3, 7 / 2.0 = 3.5
255 + 1 in uint8_t = 0
sizeof: char=1 int=4 float=4 double=8 int64_t=8
```

Every variable has a **type**, which decides how many bytes it takes and how its bits are read:

| Type | Used for | `printf` specifier |
|---|---|---|
| `int` | signed integers | `%d` |
| `unsigned int`, `uint8_t`... | unsigned integers | `%u` (hex: `%x`) |
| `float`, `double` | real numbers | `%f` (`%.2f`: two decimal places) |
| `char` | one character (really a small number: `'A'` is 65) | `%c` (or `%d` to see the number) |
| `bool` | true/false, needs `<stdbool.h>` | `%d` (0 or 1) |
| `size_t` (the result of `sizeof`) | sizes, indices | `%zu` |

A few things the output above shows:

- `7 / 2` is `3`: dividing two integers drops the fractional part. To get `3.5`, one of the two must be a real number (`7 / 2.0`).
- `uint8_t` only holds 0..255, so `255 + 1` wraps back to `0`. For **unsigned** numbers, wrapping around is behavior C guarantees. For **signed** numbers, overflow is undefined behavior: do not rely on it.
- The size of `int` is not fixed by the language (usually 4 bytes). If you need an exact number of bits, use the types in `<stdint.h>`.

### Fixed-size types

`<stdint.h>` gives `int8_t`, `int16_t`, `int32_t`, `int64_t` (signed) and `uint8_t`... `uint64_t` (unsigned). Games often
use them and give them short names:

```c
typedef int32_t i32;
typedef float f32;
typedef uint8_t u8;
```

That is exactly what njin does in `src/engine/api/_types.h`, with the equivalent C++ syntax:

```cpp
using i32 = std::int32_t;
using u8 = std::uint8_t;
using f32 = float;
```

When you read `i32` in njin code, read `int32_t`.

### A wrong specifier is a real bug

`printf` does not know the types of its arguments: it trusts you. Give it the wrong specifier and the program prints garbage or crashes. Luckily the
compiler can check, with `-Wall`:

```c
int lives = 3;
printf("lives=%s\n", lives);   // %s needs a string, not an int
printf("speed=%d\n", 110.5);   // %d needs an int, not a double
```

```
warning: format '%s' expects argument of type 'char *', but argument 2 has type 'int' [-Wformat=]
warning: format '%d' expects argument of type 'int', but argument 2 has type 'double' [-Wformat=]
```

This only works for `printf` and its relatives, because the compiler knows about them specifically. For a function you write yourself, you
have to tell it with a GCC/Clang attribute:

```c
__attribute__((format(printf, 1, 2))) static void log_info(const char *fmt, ...);
```

`format(printf, 1, 2)` means "parameter 1 is a printf-style format string, and the arguments to check start at position 2".
Calling `log_info("texture missing: %s", 42)` then gets exactly the same `-Wformat=` warning as above, and if you ignore the warning and run it,
the program **crashes** (tried: segmentation fault), because `%s` goes reading memory at address 42.

In njin, `src/engine/api/njin_log.h` does exactly this:

```cpp
#define NJIN_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
...
void log_write(log_level level, const char *file, i32 line, const char *fmt,
               ...) NJIN_PRINTF(4, 5);
```

The format string of `log_write` is parameter 4, and the arguments after it start at 5. Thanks to that, `NJIN_INFO("... %d", x)` with
the wrong type is also warned about at compile time. (On MinGW, that is GCC on Windows, njin uses `__MINGW_PRINTF_FORMAT` instead of `printf`; that is why there is
an `#if` branch at the top of the file: the next lessons cover `#if`.)

## Conditions, loops and functions

@include learn_c_flow.c

```
frame 0: y=0.0083 vy=0.500
frame 1: y=0.0250 vy=1.000
frame 2: y=0.0500 vy=1.500
frame 3: y=0.0833 vy=2.000
frame 4: y=0.1250 vy=2.500
hit the ground after 35 frames, y=5.00
```

The program simulates a falling object, exactly the way a game updates a position every frame:

- **`if`** picks a branch. `clampf` uses it to keep a value inside the range `[lo, hi]`.
- **`for (init; condition; step)`** loops when the number of iterations is known up front. `frame` only exists inside the loop.
- **`while (condition)`** loops until the condition is false. If the condition is never false, the program runs forever.
  There is also `do { ... } while (condition);`, which runs the body at least once.
- **A function** has a return type, a name and a parameter list. `static` here means "only this file uses this function"; the lesson
  @ref learn_c_project explains it.
- **`const`** is a promise "this does not change": `const float dt` cannot be assigned again. Use it for everything that does not need to change.

The number `1.0f / 60.0f` is the length of one frame at 60 FPS. `vy += gravity * dt` is "velocity grows by acceleration times time",
`y += vy * dt` is "position grows by velocity times time". You will see these exact two lines in every
game, including in njin's `platformer_body` (multiplied by `delta(ctx)` instead of a constant `dt`).

## Self-check

1. What does `printf("%d\n", 7 / 2);` print, and why?
2. What is the difference between `int` and `int32_t`? When should you use the second one?
3. `uint8_t x = 250; x = x + 10;` gives `x` what value?
4. Why do `-Wall -Wextra` matter when learning C?
5. How do a `for` loop and a `while` loop differ?

## Exercises

1. Write a function `float lerp(float a, float b, float t)` that returns `a + (b - a) * t`, then print its value for `t` = 0, 0.25, 0.5, 0.75, 1 between 10 and 20.
2. An object bounces up with `vy = -10` (negative is up, `y = 0` is the ground) and acceleration 30. Count how many frames at 60 FPS it takes to fall back to the ground (`y >= 0`). Use `do ... while`.
3. Write a function `bool is_even(int n)` and print the even numbers from 1 to 10.

## Answers

**Self-check.**

1. It prints `3`. Both operands are `int`, so the division is integer division and drops the fractional part.
2. `int` has a size that depends on the compiler (usually 32 bits); `int32_t` is always exactly 32 bits, signed. Use `int32_t` when the size matters: data saved to a file, sent over a network, or exchanged with a library.
3. `4`. `x + 10` is 260, but `uint8_t` only holds 0..255, so what is left is `260 - 256 = 4`.
4. Because C trusts the programmer: many bugs (a wrong `printf` specifier, an unassigned variable, a mistaken comparison) are only caught as warnings, not as compile errors.
5. `for` is compact when the number of iterations is known up front (init, condition and step sit on one line); `while` is for looping until a condition you cannot count, for example "until it hits the ground".

**Exercise 1**

```c
#include <stdio.h>

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

int main(void) {
  for (int i = 0; i <= 4; i++) {
    float t = i / 4.0f;
    printf("t=%.2f -> %.1f\n", t, lerp(10.0f, 20.0f, t));
  }
  return 0;
}
```

```
t=0.00 -> 10.0
t=0.25 -> 12.5
t=0.50 -> 15.0
t=0.75 -> 17.5
t=1.00 -> 20.0
```

**Exercise 2**

```c
#include <stdio.h>

int main(void) {
  const float gravity = 30.0f, dt = 1.0f / 60.0f;
  float y = 0.0f;
  float vy = -10.0f;
  int frames = 0;
  do {
    vy += gravity * dt;
    y += vy * dt;
    frames++;
  } while (y < 0.0f);
  printf("back on the ground after %d frames\n", frames);
  return 0;
}
```

```
back on the ground after 39 frames
```

Use `do ... while` because the body must run at least once: at the start `y = 0`, so the condition `y < 0` is false right away and a
plain `while` loop would not run even once.

**Exercise 3**

```c
#include <stdbool.h>
#include <stdio.h>

static bool is_even(int n) { return n % 2 == 0; }

int main(void) {
  for (int n = 1; n <= 10; n++)
    if (is_even(n))
      printf("%d ", n);
  printf("\n");
  return 0;
}
```

```
2 4 6 8 10
```

## Next step

@ref learn_c_memory : memory, pointers, arrays and strings, the hardest and most important part of C.
