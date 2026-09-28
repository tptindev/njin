# Foundations: learn before you make games {#learn}

To make games with njin you need to be able to read C++, know how CMake builds a program, and understand what a shader is. This group of 13 lessons teaches
exactly those parts, **from zero**, in a single thread. You do not have to pick a "level": you start at the first lesson, and
skip any lesson you already know.

Everything here is distilled from njin itself: the examples take their shape from the engine's code (handles, `njin_ctx &`, the repo's CMake,
the post-processing shaders), plus the game patterns every game runs into. **No lesson needs njin**: all you need is a compiler, and
CMake for the last part. Install them by following @ref setup.

## How to study

- Each lesson opens with "What this lesson teaches" and "What you need to know first", and ends with **Self-check** (questions), **Exercises** and
  **Answers**. Do the exercises before you look at the answers.
- **Every example has been compiled and actually run**, and the output printed in a lesson is the real output. Where a page could not try something
  (a compiler, an operating system), it says so.
- Retype and modify the examples instead of just reading them. A bug you cause and fix yourself teaches more than an example that runs right.

## The 13 lessons

**C: the foundation of everything** (raylib is written in C, and njin's API still keeps a C shape in `const char *` and `printf`)

| Lesson | Content |
|---|---|
| @subpage learn_c_start | Write, compile and run a C program: data types, `printf`, loops, functions |
| @subpage learn_c_memory | Pointers, arrays, `struct`, C strings, stack and heap, the five classic memory bugs and how to catch them |
| @subpage learn_c_project | Multiple files, headers, the preprocessor, the four stages from code to executable, the `undefined reference` error |

**C++: the parts njin uses every day**

| Lesson | Content |
|---|---|
| @subpage learn_cpp_from_c | References, `const`, namespaces, `std::string`, `std::vector`, `struct` with functions, overloading, default arguments |
| @subpage learn_cpp_types | Values and references, copy and move, RAII, ownership, and why njin uses handles and `njin_ctx &` |
| @subpage learn_cpp_modern | `auto`, structured bindings, lambdas, designated initializers, `initializer_list`, `constexpr`, `enum class`, templates to **use** |
| @subpage learn_errors | Reading compile errors, link errors and runtime errors, and the debugging workflow with `-Wall`, `printf`, `gdb` |

**CMake: building and managing a project**

| Lesson | Content |
|---|---|
| @subpage learn_cmake_basics | `CMakeLists.txt` for programs and libraries, `PRIVATE`/`PUBLIC`/`INTERFACE`, build types, the cache |
| @subpage learn_cmake_projects | `FetchContent`, presets, multiple folders, copying assets, and a raylib window built with CMake |

**Shaders: programs that run on the GPU**

| Lesson | Content |
|---|---|
| @subpage learn_shader_start | What a shader is, just enough GLSL, a raylib "playground" to write and edit fragment shaders |
| @subpage learn_shader_sdf | SDF: drawing shapes with distance. Circles, boxes, combining shapes, glows, shadows, health bars with no image |
| @subpage learn_shader_patterns | Dark vignette, CRT stripes, blur, white flash, palette swap, dissolve, outline, sharp pixel art, ripples |

**Game patterns**

| Lesson | Content |
|---|---|
| @subpage learn_game_patterns | Fixed physics step, ECS, state machines, timers, events, actions instead of keys |

## What you can skip

Not everyone needs to read all of it. If you already know:

- how to write a multi-file C program and read `undefined reference`: start from @ref learn_cpp_from_c;
- basic C++ (classes, `std::vector`, smart pointers): start from @ref learn_cpp_modern, and read @ref learn_cpp_types to
  understand njin's handles;
- how to make a `CMakeLists.txt` with several targets: read @ref learn_cmake_projects for `FetchContent` and presets;
- how to write a fragment shader: read @ref learn_shader_sdf if you do not know SDF yet, then @ref learn_shader_patterns and its "In njin" section;
- ECS and the game loop: @ref learn_game_patterns is short, and has examples that run.

## What was tried

Every page says this itself, but in short: every example was run on **Windows with GCC 15.2**. The memory-error detectors
(AddressSanitizer) ran in Ubuntu (WSL) because the Windows GCC here does not have them. Shaders ran for real on an
Intel Iris Xe card; shader compile error messages are written by the driver, so the wording will differ on other cards. MSVC, Clang and macOS were
not tried.

## After these lessons

You have enough background to start njin:

1. @ref setup : set up your environment
2. @ref getting_started : your first njin program
3. @ref first_jump (platformer) or @ref first_walk (top-down) : a character that moves, in 50 lines
4. @ref cheatsheet : look up "to do X, which function do I use"
