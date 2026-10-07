# Lesson 3: Multiple files, the preprocessor and linking {#learn_c_project}

**What this lesson teaches:** splitting a program into several files (headers and sources), the preprocessor, the four stages from code to executable, and reading the `undefined reference` error.

**What you need to know first:** @ref learn_c_memory (pointers, strings) and how to run `gcc` from a terminal.

A real program does not live in one file. njin has hundreds of `.h` and `.cpp` files, and the reason it can be split up like that is
what this lesson covers. Every example and error below was actually run with GCC 15.2 on Windows (w64devkit),
`gcc -std=c17 -Wall -Wextra`; where Linux differs the lesson says so, and that is the result of a run in WSL Ubuntu
(GCC 15.2).

## Three files, one program

Example: a small vector module. `learn_c_vec.h` **declares** what the module provides:

@include learn_c_vec.h

`learn_c_vec.c` **defines** (implements) them:

@include learn_c_vec.c

`learn_c_vecmain.c` uses the module:

@include learn_c_vecmain.c

Compile each file into an **object file** (`.o`), then link:

```
gcc -std=c17 -Wall -Wextra -c learn_c_vecmain.c -o main.o
gcc -std=c17 -Wall -Wextra -c learn_c_vec.c -o vec.o
gcc main.o vec.o -o app
./app
```

```
sum=(3, 4) length=5.0
function calls: 2
```

One command does all three: `gcc learn_c_vecmain.c learn_c_vec.c -o app`. (On Linux add `-lm` at the end; see the errors section.)

### Declarations and definitions

| | Example | Meaning | Where it goes |
|---|---|---|---|
| **Declaration** | `float vec2_length(vec2 v);` | "there is a function with this name and this type" | a header, `#include`d by many files |
| **Definition** | `float vec2_length(vec2 v) { ... }` | the function body itself, and memory for a variable | **exactly one** `.c` file |

The rule: a thing may be **declared many times**, but **defined exactly once** in the whole program.
Headers hold declarations, so every file can share them; `.c` files hold definitions.

## `#include` and include guards

`#include "learn_c_vec.h"` **copies the contents** of that file, word for word, in place of this line, before compiling. Double quotes look
first in the folder of the current file, `<stdio.h>` looks in the system folders. You add search folders with `-I`;
that is how an njin game writes `#include <njin.h>`: CMake adds the folder `src/engine/api` to the search path.

Because it is only copying, a header can be copied **twice** into one file (`a.h` and `b.h` both include `base.h`, then `main.c` includes
both). Declaring twice is fine, but a second `typedef struct {...} point;` is an error. For real:

```
base.h:3:3: error: conflicting types for 'point'; have 'struct <anonymous>'
```

The fix is an **include guard**, wrapped around the whole header:

```c
#ifndef LEARN_C_VEC_H   // if this name has never been defined...
#define LEARN_C_VEC_H   // ...define it and keep the contents
...
#endif                  // the second time, #ifndef is false and the whole block is skipped
```

Or a single line `#pragma once` at the top of the file, shorter and understood by every common compiler (but not part of the C standard).
njin uses `#pragma once` at the top of every header (all 40 headers in `src/engine/api` and 42 headers in `src/engine/runtime`). Both ways were tried on the error above, and both compile with no error.

## `static` and `extern`

- **`static` on a function or a global variable**: "only this file sees it". `square` in `learn_c_vec.c` cannot be called from another file, and does not clash with a `square` function in another file. Internal functions should be `static`.
- **`extern`**: "this variable is real, but it is defined in another file". `extern int vec2_calls;` in the header declares it; `int vec2_calls = 0;` in the `.c` defines it.

Why not just write `int vec2_calls = 0;` in the header? Because every `.c` file that includes it would define its own copy, and linking
would fail (see the errors section below). njin has exactly this pattern in the platformer sample game: `src/games/platformer/game.h` has
`extern game_state g;`, while `main.cpp` has `game_state g;`, the one and only definition.

## The preprocessor: editing code before compiling

Every line that starts with `#` is a directive for the **preprocessor**, which runs before the compiler and works on **text**, without understanding C.

@include learn_c_platform.c

The output on Windows, then with `-DDEBUG_LOG` added to the compile command:

```
platform: Windows
MAX_SPRITES=8
SQUARE_BAD(1 + 2) = 5, SQUARE_OK(1 + 2) = 9
```

```
platform: Windows
[log] running
MAX_SPRITES=8
SQUARE_BAD(1 + 2) = 5, SQUARE_OK(1 + 2) = 9
```

The same file run on Linux (WSL) gives `platform: Linux`, and the other lines are identical.

- **`#define NAME value`**: replaces text. `MAX_SPRITES` becomes `8`.
- **`#if defined(...)` / `#ifdef`**: keeps or drops a whole block of code. The compiler predefines `_WIN32`, `__linux__`, `__APPLE__` depending on the operating system, so one source file can pick code per platform. `-DDEBUG_LOG` turns the real `LOG` on; without it `LOG(...)` becomes `((void)0)`, which costs nothing.
- **Macros only replace text.** `SQUARE_BAD(1 + 2)` turns into `1 + 2 * 1 + 2`, which is 5, not 9. Always wrap the parameters and the whole expression in parentheses like `SQUARE_OK`, or use a `static` function instead of a macro.

njin uses this very pattern. In `src/engine/runtime/njin_file.cpp`, the folder for user data differs per operating system:

```cpp
fs::path user_data_dir() {
#if defined(_WIN32)
  ...APPDATA...
#elif defined(__APPLE__)
  ...~/Library/Application Support...
#else
  ...~/.local/share...
#endif
```

`njin_log.h` also uses `#if defined(__MINGW_PRINTF_FORMAT)` to pick the right `printf` attribute (lesson @ref learn_c_start).

## The four stages from code to executable

`gcc` does four jobs in a row. You can stop in the middle to see each result:

| Stage | Command | Result | What it does |
|---|---|---|---|
| 1. Preprocess | `gcc -E learn_c_vecmain.c -o main.i` | `main.i`, plain C | expands `#include`, replaces `#define`, picks `#if` |
| 2. Compile | `gcc -S learn_c_vecmain.c -o main.s` | `main.s`, assembly | translates C into CPU instructions |
| 3. Assemble | `gcc -c learn_c_vecmain.c -o main.o` | `main.o`, machine code | packs the instructions into an object file |
| 4. Link | `gcc main.o vec.o -o app` | `app`, the program | joins the `.o` files and libraries into one |

See it for yourself:

- **Preprocess**: `main.i` is **1059 lines** long on my machine (mostly the headers of `stdio.h`), and in it `#include "learn_c_vec.h"` has turned into the header's own contents:

```
vec2 vec2_add(vec2 a, vec2 b);
float vec2_length(vec2 v);
extern int vec2_calls;
```

- **Compile**: in `main.s`, the call to `vec2_add` is just `call vec2_add`, a name, with no address yet:

```
32:	call	vec2_add
36:	call	vec2_length
```

- **Assemble**: `nm` lists the **symbols** (names) an object file has or needs. Shortened:

```
$ nm main.o      (only the vec2 lines kept)
                 U vec2_add
                 U vec2_calls
                 U vec2_length

$ nm vec.o        (only the symbols kept, section lines such as .text dropped)
                 U sqrtf
0000000000000000 t square
0000000000000014 T vec2_add
0000000000000000 B vec2_calls
000000000000007e T vec2_length
```

Read it like this: `U` (undefined) is "I need this, someone give it to me"; `T` is a function that is defined and **exported** to other files;
`B` is a global variable (data); **lowercase `t` is `static`**: `square` only exists internally. `main.o` does not yet know where `vec2_add`
is, only that it needs one.

- **Link**: the linker matches every `U` in one file with a `T`/`B` in another file, and reports an error if some `U` has no provider.

## Reading errors

Compile errors (`error: ...` from `gcc`) happen in stages 1 to 3 and give the **file and line**. Link errors come from `ld` in stage 4 and only give the **symbol name**. Here are four errors I caused and actually ran.

### `undefined reference to ...`: a forgotten `.c` file

```
$ gcc main.o -o app
ld.exe: main.o:learn_c_vecmain.c:(.text+0x45): undefined reference to `vec2_add'
ld.exe: main.o:learn_c_vecmain.c:(.text+0x55): undefined reference to `vec2_length'
ld.exe: main.o:...: undefined reference to `vec2_calls'
collect2.exe: error: ld returned 1 exit status
```

Exactly the three `U` lines above: `main.o` needs three symbols and no `.o` provides them. The fix: add `vec.o` (or `learn_c_vec.c`)
to the link command. Read `undefined reference to X` as: "I was promised X exists (by a declaration), but I cannot find its definition".

**Three common causes of this same error:** (1) a `.c` file left out of the build command; (2) the function is `static`, so it is not exported. Try declaring `float square(float v);` in `usesq.c` and linking it with `vec.o`, which gives: ``undefined reference to `square'``. (3) a forgotten library: see right below.

### `undefined reference to sqrtf` only on Linux: missing `-lm`

On Windows/MinGW, `sqrtf` is just there. On Linux (GCC 15.2, WSL), the same one-line command gives:

```
$ gcc -std=c17 -Wall -Wextra learn_c_vecmain.c learn_c_vec.c -o vec
learn_c_vec.c:(.text+0xda): undefined reference to `sqrtf'
collect2: error: ld returned 1 exit status
```

`sqrtf` lives in the math library `libm`, which you must ask for with `-lm` **at the end of the command**. Add `-lm` and it runs correctly. A detail worth remembering: with `-O2`
you do **not** need `-lm` (the optimizer replaces `sqrtf` with a single CPU instruction, without calling the library). Link errors depend on the compile
flags too, not only on the code. njin also has library linking that depends on the operating system: in `src/engine/runtime/CMakeLists.txt`, `ws2_32` is only linked on
Windows and `${CMAKE_DL_LIBS}` (the dynamic loading library) only on Linux. The lesson @ref learn_cmake_basics shows how to declare libraries in the right place.

### `multiple definition of ...`: a definition in a header

Change `extern int vec2_calls;` in the header to `int vec2_calls = 0;`, then include the header in two `.c` files:

```
ld.exe: bad_vec.o:bad_vec.c:(.bss+0x0): multiple definition of `vec2_calls'; bad_main.o:bad_main.c:(.bss+0x0): first defined here
```

Both object files define `vec2_calls`, and the linker does not know which one to pick. Define it in exactly **one** place (a `.c` file); the header only has `extern`.

### `unknown type name` and `implicit declaration`: a forgotten `#include`

Remove the line `#include "learn_c_vec.h"` from `learn_c_vecmain.c`:

```
nodecl.c:4:3: error: unknown type name 'vec2'
nodecl.c:6:14: error: implicit declaration of function 'vec2_add' [-Wimplicit-function-declaration]
```

This is a **compile** error (stage 2, with a file and a line): the compiler has no declaration of `vec2` and `vec2_add`. With GCC 15.2 and `-std=c17`, calling an undeclared function is
an error, not just a warning. To tell them apart: if there is a line number, fix the `#include` or the spelling; if there is only a symbol name, fix the link command or the definition.

## When there are hundreds of files

Typing `gcc -c` by hand for three files is fine. With hundreds of files you want to: recompile only the files that changed, find headers in the right place,
link the right libraries for each operating system, and have one single command everybody uses. That job is called a **build system**,
and njin's is CMake: @ref learn_cmake_basics.

## Self-check

1. What is the difference between a declaration and a definition? Which one can be repeated many times?
2. Why does every header need an include guard or `#pragma once`?
3. `nm` prints `U foo` for an object file. What does that tell you?
4. Which errors come from the compiler and which from the linker? How do you tell them apart?
5. Why should you not write `int counter = 0;` in a header?

## Exercises

1. Move a function `float clampf(float value, float lo, float hi)` out into `clamp.h` and `clamp.c`, and call it from `main1.c`. Compile all three with one command.
2. Given `add.c` containing `int add(int a, int b)`, and `main2.c` that declares `int add(int, int);` and calls it: compile **only** `main2.c`, read the error, and fix it.
3. `base.h` has `typedef struct { float x, y; } point;`, `a.h` and `b.h` both include it, and `main3.c` includes both `a.h` and `b.h`. Cause the error, then fix it in two ways.

## Answers

**Self-check.**

1. A declaration says "there is this thing, with this type" and can be repeated as often as you like; a definition is the function body or the variable itself, gets memory, and there must be **exactly one** in the whole program.
2. `#include` copies word for word, so a header can be copied twice into one file (through two different headers). Defining a type twice is an error; the guard makes the second copy empty.
3. That file needs the symbol `foo` but does not define it. When linking, some other file must provide it, or you get `undefined reference to foo`.
4. Compile errors: from `gcc` (`cc1`), with a file name and a line number. Link errors: from `ld`, ending with `collect2: error: ld returned 1 exit status`, with only a symbol name.
5. Every `.c` file that includes that header would define its own `counter`, and linking would report `multiple definition`. Use `extern int counter;` in the header and one single definition in a `.c` file.

**Exercise 1**

```c
// clamp.h
#ifndef CLAMP_H
#define CLAMP_H

float clampf(float value, float lo, float hi);

#endif
```

```c
// clamp.c
#include "clamp.h"

float clampf(float value, float lo, float hi) {
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return value;
}
```

```c
// main1.c
#include "clamp.h"
#include <stdio.h>

int main(void) {
  printf("%.1f %.1f %.1f\n", clampf(-3.0f, 0.0f, 10.0f), clampf(4.5f, 0.0f, 10.0f), clampf(99.0f, 0.0f, 10.0f));
  return 0;
}
```

```
$ gcc -std=c17 -Wall -Wextra main1.c clamp.c -o ex1
$ ./ex1
0.0 4.5 10.0
```

**Exercise 2**

```
$ gcc -std=c17 -Wall -Wextra main2.c -o ex2
ld.exe: ...main2.c:(.text+0x18): undefined reference to `add'
collect2.exe: error: ld returned 1 exit status
```

`main2.c` only has a declaration of `add`, and nobody defines it. The fix: put `add.c` in the command:

```
$ gcc -std=c17 -Wall -Wextra main2.c add.c -o ex2
$ ./ex2
5
```

**Exercise 3**

The real error without a guard:

```
base.h:3:3: error: conflicting types for 'point'; have 'struct <anonymous>'
```

Way 1: wrap `base.h` in an include guard.

```c
#ifndef BASE_H
#define BASE_H
typedef struct {
  float x, y;
} point;
#endif
```

Way 2: just put `#pragma once` on the first line of `base.h`. Both ways compiled `main3.c` with no error.

## Next step

@ref learn_cpp_from_c : from C to C++: references, `namespace`, `std::string`, `std::vector`, and what replaces the hand-made patterns of these three lessons.
