# Lesson 2: Memory, pointers and strings {#learn_c_memory}

**What this lesson teaches:** pointers, arrays, `struct`, C strings, the stack and the heap (`malloc`/`free`), and the five classic memory bugs, with how to catch them.

**What you need to know first:** @ref learn_c_start (data types, `printf`, functions, loops).

This is the hardest part of C, and also the part that explains why njin's code is written the way it is: handles instead of
pointers, `const char *` for paths, checked format strings. The best way to learn it is to run each example. Every example
below was compiled with `gcc -std=c17 -Wall -Wextra` (GCC 15.2, Windows), and **all eight "healthy" examples also ran clean
under AddressSanitizer and UBSan** (GCC 15.2 in WSL Ubuntu, see the section "Catching bugs with sanitizers").

## Memory is a row of cells with addresses

Every variable lives somewhere in memory, and that place has an **address** (a number). A **pointer** is a variable that holds an
address. Two operators go together: `&x` is "the address of `x`", `*p` is "go to the address `p` holds".

@include learn_c_pointers.c

```
lives=5, *p=5, p==&lives: 1
speed=150.0
sizeof(scores)=20, elements=5
distance between scores[1] and scores[0] = 4 bytes
scores[2]=30, *(scores+2)=30
sum=150
nothing is NULL, do not use *nothing
```

What to take away:

- `*p = 5` writes into `lives` itself. This is how a function **changes the caller's variable**: `scale(&speed, 1.5f)` passes the address, and the function writes through that address. If you passed plain `speed`, the function would only get a copy.
- An array is elements sitting **right next to each other**: `scores[1]` is exactly 4 bytes from `scores[0]`, which is `sizeof(int)`. `scores[i]` is exactly `*(scores + i)`, and pointer arithmetic counts in elements, not bytes.
- `NULL` means "points nowhere". Reading or writing through a `NULL` pointer is a bug. Always check before use if a pointer can be `NULL`.
- `const int *values` means "this function only reads the elements". Writing to them is a compile error.

### Arrays "decay" into pointers

Pass an array to a function and the function only gets **the address of the first element**, and no longer knows how long the array is:

```c
static size_t bad_count(const int values[]) { return sizeof(values) / sizeof(values[0]); }
...
int scores[5] = {10, 20, 30, 40, 50};
printf("in main    : %zu elements\n", sizeof(scores) / sizeof(scores[0]));
printf("in function: %zu elements\n", bad_count(scores));
```

```
decay.c:4:60: warning: 'sizeof' on array function parameter 'values' will return size of 'const int *' [-Wsizeof-array-argument]
in main    : 5 elements
in function: 2 elements
```

Inside the function, `sizeof(values)` is 8 (the size of a pointer on a 64-bit machine), so the result is wrong (2). The warning
`-Wsizeof-array-argument` catches it. The rule: **always pass the length along with the array** (`sum(scores, 5)`).

## `struct`: grouping data

@include learn_c_struct.c

```
after boost_copy: jump_speed=350
after boost:      jump_speed=450
jump height=101.2, sizeof(body_config)=12 bytes
```

- **Pass by value** (`boost_copy(body)`): the function gets a copy. Changing the copy does not touch the caller's `body`, so `jump_speed` stays 350.
- **Pass a pointer** (`boost(&body)`): the function changes the original. `body->jump_speed` is shorthand for `(*body).jump_speed`.
- **`const body_config *`**: pass a pointer to avoid copying the whole struct, while still promising not to change it.
- **Designated initializers** (`.run_speed = 110.0f`): every field is named, and any field you leave out is 0. `#define BODY_DEFAULT {...}` gives a set of default values, then you adjust single fields: `body.jump_speed = 350.0f;`.

This is exactly how `njin::platformer_body` is used. In C++ the default values sit right inside the struct
(`f32 run_speed = 110.0f;`, `f32 jump_speed = 300.0f;`, `f32 gravity = 1000.0f;` in `njin_body.h`), and a game writes:

```cpp
njin::platformer_body body{};
body.jump_speed = 350.0f;
```

(C++ is covered in the lesson @ref learn_cpp_from_c.)

## C strings

C has no "string" type. A string is **an array of `char` that ends with a zero byte** (`'\0'`).

@include learn_c_strings.c

```
strlen(name)=6, sizeof(name)=16
bytes of name: 83 112 114 111 117 116 0 0
line="Sprout is Rung " (needs 23 chars, buffer 16)
truncated!
a == b: 0, strcmp(a, b) == 0: 1
```

The compiler even warns about that `snprintf` line, right at compile time:

```
warning: '%s' directive output truncated writing 13 bytes into a region of size between 0 and 12 [-Wformat-truncation=]
```

Remember:

| Thing | Meaning |
|---|---|
| `char name[16] = "Sprout"` | a 16-byte array of your own, you can change it. The string "Sprout" takes 7 bytes (6 characters and `'\0'`); the rest is 0 |
| `const char *title = "..."` | a pointer to a constant string that already exists in the program. You can read it, **you may not change it** |
| `strlen(name)` | counts characters up to `'\0'` (6), unlike `sizeof(name)` (16, the size of the array) |
| `snprintf(buf, size, fmt, ...)` | prints into `buf` but never writes past `size`, always adds `'\0'`, and returns the length it **needed**: if that is `>= size`, the output was cut |
| `a == b` on strings | compares **addresses**, almost never what you mean. Compare contents with `strcmp(a, b) == 0` |

Avoid `strcpy` and `sprintf` (they have no limit: a string longer than the buffer writes past its end). Use `snprintf`
with `sizeof(buf)`.

## The stack and the heap

- **Stack**: local variables (`int lives`, `char name[16]`). Allocated automatically when a function starts, and **gone automatically when the function returns**. Fast, but small and short-lived.
- **Heap**: memory you ask for yourself with `malloc` and give back yourself with `free`. It lives until you give it back. Use it when the size is only known at run time, or when the data must outlive the function that created it.

@include learn_c_heap.c

```
grown to 4 elements
grown to 8 elements
10 20 30 40 50 
```

Rules for the heap:

1. **Check whether `malloc` returned `NULL`**: running out of memory really happens.
2. **Every `malloc` has exactly one `free`** (`realloc` asks for a bigger block again and keeps the contents; the old pointer is no longer used).
3. After `free`, set the pointer to `NULL`, so a mistaken use crashes at once instead of going quietly wrong.
4. Assign the result of `realloc` to a **temporary** variable (`bigger`) first: if it fails it returns `NULL`, and the old `values` still needs to be `free`d.

## The five classic memory bugs

What all five have in common: the program breaks the rules, and C **does not stop** to tell you. The behavior at that point is called
**undefined** (undefined behavior): it may run correctly, print garbage, crash, or go wrong only on someone else's machine. Below, each
bug has a small program in `docs/examples/`. The results written underneath are **real results**: "plain" is GCC 15.2
on Windows without a sanitizer, "sanitizer" is GCC 15.2 in WSL Ubuntu with `-fsanitize=address`.

### 1. Writing past the end of an array

@include learn_c_bug_oob.c

A 4-element array has indices 0..3, but the loop writes up to `scores[4]`.

- **Plain**: prints `done`, exits normally, no warning at all. The bug is still there; it just did not break anything visible this time.
- **Sanitizer**:

```
ERROR: AddressSanitizer: heap-buffer-overflow on address 0x75a7cf0e0020 ...
WRITE of size 4 at 0x75a7cf0e0020 thread T0
    #0 ... in main learn_c_bug_oob.c:10
0x75a7cf0e0020 is located 0 bytes after 16-byte region [0x75a7cf0e0010,0x75a7cf0e0020)
allocated by thread T0 here:
    #1 ... in main learn_c_bug_oob.c:6
```

The report is exact: a 4-byte write at line 10, right after the 16-byte block asked for at line 6.

### 2. Use-after-free

@include learn_c_bug_uaf.c

- **Plain**: GCC warns `pointer 'lives' used after 'free' [-Wuse-after-free]`, the program still runs and prints a garbage number (my try gave `lives=-73919904`; the number changes from run to run).
- **Sanitizer**: `heap-use-after-free`, with three places: where it was read (line 11), where it was `free`d (line 10), where it was `malloc`ed (line 6).

### 3. A dangling pointer to a local variable

@include learn_c_bug_dangling.c

`label` sits on the stack of `remember_label` and dies when the function ends; `saved` still holds the old address.

- **Plain** (Windows): GCC warns `storing the address of local variable 'label' in 'saved' [-Wdangling-pointer=]`, and the program prints **an empty line** instead of `Level 3`.
- **The same program, GCC in WSL, no sanitizer**: prints `Level 3` and exits with code 0. **The program "runs correctly" while still being wrong.** This is the most dangerous case: it breaks on another machine, or when you add one line of code.
- **Sanitizer** (needs `ASAN_OPTIONS=detect_stack_use_after_return=1` on this GCC version):

```
ERROR: AddressSanitizer: stack-use-after-return on address 0x74ad99ad0020 ...
READ of size 8 ...
    #1 ... in main learn_c_bug_dangling.c:14
Address 0x74ad99ad0020 is located in stack of thread T0 at offset 32 in frame
    #0 ... in remember_label learn_c_bug_dangling.c:6
  This frame has 1 object(s):
    [32, 64) 'label' (line 7) <== Memory access at offset 32 is inside this variable
```

If you write `return label;` directly in a function that returns `const char *`, GCC catches it right away with
`-Wreturn-local-addr` and even **changes the return value to `NULL`**: my program then printed nothing at all. The
right way: return a constant string, ask for memory with `malloc` (and have the caller `free` it), or let the caller pass in a buffer.

### 4. Leaks

@include learn_c_bug_leak.c

- **Plain**: prints `done`, nobody notices. A leak only shows up when the program runs for a long time: the game slowly loses RAM, then slows down or crashes after a few hours.
- **Sanitizer** (LeakSanitizer, which comes with ASan and reports at exit):

```
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 48 byte(s) in 3 object(s) allocated from:
    #1 ... in spawn_particle learn_c_bug_leak.c:6
SUMMARY: AddressSanitizer: 48 byte(s) leaked in 3 allocation(s).
```

Three calls, 16 bytes each (`4 * sizeof(float)`), 48 in total.

### 5. Uninitialized variables

@include learn_c_bug_uninit.c

A local variable in C **is not automatically 0**: it holds garbage left over in memory. Real results:

| How it was run | Result |
|---|---|
| Windows, `-O0` | `bonus=32758` (garbage) |
| Windows, `-O1` and `-O2` | `bonus=100`: a "nice" result that should be impossible (`argc` is 1) |
| WSL, `-fsanitize=address,undefined` | `bonus=32765` (garbage), no error reported |

The `100` result can be explained in theory: using an unassigned variable is undefined behavior, so the optimizer is allowed to treat
the assigning branch as always taken (I did not read the assembly to confirm this). A result that changes with the optimization level is enough to show the program
can no longer be trusted. GCC 15.2 gives no warning in any of the cases above, and ASan/UBSan do not detect this bug. The only
sure prevention: **always assign a value when you declare** (`int bonus = 0;`). (There are other tools for this bug, such as
Clang's MemorySanitizer or Valgrind; I have not tried them here.)

## Catching bugs with sanitizers

AddressSanitizer (ASan) inserts checks into the program at compile time and reports memory bugs right at the line that causes them;
the program runs slower and uses more RAM. UBSan catches some other undefined behavior (signed overflow, division by zero...). Use them when
debugging, not for release builds:

```
gcc -std=c17 -Wall -Wextra -g -fsanitize=address,undefined myprogram.c -o myprogram
```

`-g` makes the report include file names and line numbers. **Not every machine has it:** the w64devkit GCC (Windows) I use
does **not** have ASan (linking fails with `cannot find -lasan`). Every report above was produced by GCC in WSL Ubuntu. On Linux,
GCC and Clang have it built in; on Windows use WSL, or MSVC (which has `/fsanitize=address`; I have not tried it).

## In njin

All the rules above show up in the design of njin's API:

- **Paths are `const char *`**, read-only: `texture_load(context &ctx, const char *path)`, `shader_load(ctx, const char *vspath, const char *fspath)`. `const` means the function only reads the string, it does not change it.
- **Structs full of default values**: `platformer_body`, `topdown_body`, `collider`... declare their defaults right in the struct; a game only adjusts a few fields.
- **Handles instead of pointers.** `njin` does not return pointers to resources; it returns handles: a struct that only holds an `id` number.

```cpp
struct shader_handle {
  u32 id = 0; ///< 0 means invalid.
};
```

The note in `src/engine/runtime/njin_audio_impl.h` explains why:

```
// handle id 0 is "invalid", id N maps to slots[N - 1], and slots are never
// reused, so a stale handle can never alias a newer resource.
```

A pointer to a resource can dangle (the resource was freed but you still hold it) or point to the wrong place (the internal array moves when it
grows). A handle cannot: it is looked up again on every use, and an old handle gives back "nothing" instead of garbage. Here is that pattern, in C:

@include learn_c_handle.c

```
a.id=1 b.id=2
after destroying a: sprite_get(a) is NULL
c.id=3, a is still NULL
b at (30, 40)
handle 0: NULL, no crash
```

Note four things, all following njin's principles: `id == 0` is invalid and `sprite_get` rejects it; a destroyed handle is ignored, so destroying
twice is harmless; when creation fails (out of room, like `shader_load` meeting a missing file) it returns handle 0 instead of crashing; and `c` **does not take
over** the slot of `a`, so the old handle `a` never points by mistake to the new sprite.

## Self-check

1. What does `int *p = &x; *p = 7;` do to `x`?
2. Why does `sizeof` on a function's array parameter give the wrong result, and how do you fix it?
3. What is the difference between `char name[16] = "Sprout"` and `const char *title = "Sprout"`?
4. `snprintf` returns `23` with a 16-byte buffer. What does that mean?
5. A program runs correctly, does not crash, and has no warnings. Does that mean it has no memory bugs?

## Exercises

1. Write a function `void swap_int(int *a, int *b)` that swaps two numbers, and call it in `main`.
2. Write `copy_name(char *dst, size_t dst_size, const char *src)` that never writes more than `dst_size` bytes and always ends with `'\0'`. Try it with 6-byte and 16-byte buffers and the source `"Sprout"`.
3. Write a list of integers that grows by itself: `struct int_list { int *items; int count; int capacity; }` with `list_push` (doubling `capacity` when full) and `list_free`. Push 10 squares into it and print them.

## Answers

**Self-check.**

1. It assigns `7` to `x`: `*p` is `x` itself.
2. An array passed to a function decays into a pointer to its first element, so `sizeof` gives the size of a pointer. Pass the array's length as a separate parameter.
3. The first is a 16-byte array of your own: you can change it (`name[0] = 'X'`). The second is a pointer to a constant string of the program: changing it is undefined behavior.
4. It needed 23 characters (not counting `'\0'`), the buffer only has 16 bytes, so the string was **cut**. You must check `>= sizeof(buf)`.
5. No. All five bugs above can "run correctly" (a heap overflow, a dangling pointer printing `Level 3`, a leak...). Only sanitizers, warnings and reading the code find them.

**Exercise 1**

```c
#include <stdio.h>

static void swap_int(int *a, int *b) {
  int tmp = *a;
  *a = *b;
  *b = tmp;
}

int main(void) {
  int x = 1, y = 2;
  swap_int(&x, &y);
  printf("x=%d y=%d\n", x, y);
  return 0;
}
```

```
x=2 y=1
```

**Exercise 2**

```c
#include <stdio.h>
#include <string.h>

static void copy_name(char *dst, size_t dst_size, const char *src) {
  snprintf(dst, dst_size, "%s", src);
}

int main(void) {
  char small[6];
  copy_name(small, sizeof(small), "Sprout");
  printf("small=\"%s\" (strlen=%zu)\n", small, strlen(small));
  char big[16];
  copy_name(big, sizeof(big), "Sprout");
  printf("big=\"%s\"\n", big);
  return 0;
}
```

```
small="Sprou" (strlen=5)
big="Sprout"
```

A 6-byte buffer holds 5 characters and `'\0'`, so "Sprout" is cut to "Sprou": `snprintf` cuts safely instead of writing past the end.

**Exercise 3**

```c
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  int *items;
  int count;
  int capacity;
} int_list;

static int list_push(int_list *list, int value) {
  if (list->count == list->capacity) {
    int new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
    int *bigger = realloc(list->items, (size_t)new_capacity * sizeof(int));
    if (bigger == NULL)
      return 0; // the old list is still intact
    list->items = bigger;
    list->capacity = new_capacity;
  }
  list->items[list->count++] = value;
  return 1;
}

static void list_free(int_list *list) {
  free(list->items);
  *list = (int_list){0};
}

int main(void) {
  int_list list = {0};
  for (int i = 1; i <= 10; i++)
    list_push(&list, i * i);
  for (int i = 0; i < list.count; i++)
    printf("%d ", list.items[i]);
  printf("\n(count=%d, capacity=%d)\n", list.count, list.capacity);
  list_free(&list);
  return 0;
}
```

```
1 4 9 16 25 36 49 64 81 100 
(count=10, capacity=16)
```

These three answers also ran clean under `-fsanitize=address,undefined`.

## Next step

@ref learn_c_project : splitting a program into several files, and understanding why the "undefined reference" error happens.
