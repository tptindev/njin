# Lesson 13: Game patterns {#learn_game_patterns}

**What this lesson teaches:** six ideas that nearly every game uses, written in plain C++ so you can see that each one is really just a few
dozen lines: the loop with a fixed physics step, ECS, state machines, events, timers, and actions instead of keys.
**What you need to know first:** reading and running a C++ program with `struct`, `std::vector` and lambdas (see @ref learn_cpp_from_c
and @ref learn_cpp_modern).

Each section has a short program that actually runs, followed by an **In njin** part that shows where that idea lives in the engine. When you
start with njin, you will meet each of these again.

## 1. The loop and the fixed physics step

A game is a loop: read input, update the world, draw, then repeat. The question is how much time the world moves forward on each
update. The simplest answer is the length of the last frame (`dt`). That answer has a bug that is hard to spot:
**the result depends on FPS**. The program below makes a character jump, at four FPS levels, in two ways.

@include learn_gp_loop.cpp

Run (`g++ -std=c++20 -Wall -Wextra -Wpedantic`):

```
   FPS  step = frame      fixed step
    20  37.50             42.50
    30  40.00             42.50
    60  42.50             42.50
   144  43.96             42.50
```

The same jump: in the left column it reaches anywhere from 37.5 to 44 pixels depending on the machine (at 240 FPS it is 44.38). The reason: each step is an approximation, and the longer
the step, the bigger the error. The right column is always 42.5: physics only runs in **fixed steps** (`1/60` of a second), and what changes is the number of steps
per frame. The `while` loop collects time in `accumulator` and then runs enough steps. `max_steps` caps a
frame that is too long (for example, one stopped at a breakpoint) so the game does not have to run hundreds of steps in a row.

@note When frames are faster than 60 FPS, several frames in a row have no physics step at all, so the position on screen stands still and then
jumps. The fix is **interpolation**: draw between the previous and the next position, using the leftover fraction of a tick `alpha = accumulator / fixed_dt`.
That is exercise 2 at the end.

**In njin:**
- `njin::phase_fixed_update` runs exactly this `while` loop, 60 times a second by default (`config::fixed_hz`), at most 8 ticks
  per frame (`src/engine/runtime/njin.cpp`). Inside that phase, `njin::delta()` always equals one fixed tick.
- `njin::fixed_alpha()` is the `alpha` above. See @ref game_loop and @ref time.

## 2. Entity, component, system

Thinking in inheritance ("an Enemy is a Character is an Object") breaks down quickly as a game grows: where does a trap that has health but
does not move belong? ECS turns it around: an **entity** is only an ID number, a **component** is data
attached to it, and a **system** is a function that walks the entities that have exactly the components it needs. To give a thing health, you
attach `health` to it; you do not write a new class.

@include learn_gp_ecs.cpp

Run:

```
entity 1: (5.0, 0.0) has health
entity 2: (4.0, 4.0)
entity 3: (5.5, 0.0) has health [enemy]
```

The rock (entity 2) has a position but no `velocity`, so `move_system` skips it. Nobody has to write `if (is_rock)`.
`enemy_tag` holds no data at all: it is only there to **mark** the entity.

@note To keep it short, this program keeps the component stores in a `static` variable, so two `registry` objects would share the same stores. A
real ECS keeps the stores inside the registry, and stores components next to each other in memory so it can walk them fast.

**In njin:** njin uses the **EnTT** library, and `njin::world(ctx)` returns an `entt::registry`. It is used the same way:
`registry.create()`, `registry.emplace<T>(e, ...)`, `registry.view<A, B>().each(...)`. See @ref ecs.

## 3. State machines

Is the character standing still, running or jumping? Use an `enum class` and **a single place** that changes the state, so the
"on enter" and "on exit" work for each state has somewhere to live (play a sound, change the animation, reset a counter).

@include learn_gp_states.cpp

Run:

```
  idle -> run (after 0.25 seconds)
  [0.25] start running
  [0.75] timer inside a timer: jump in 0.25 seconds
  run -> jump (after 0.75 seconds)
  [1.00] jump!
  jump -> run (after 0.25 seconds)
```

Do not scatter `state = state::run;` all over the code: every change goes through `change()`, so the log (the lines with arrows) and every effect
that goes with a change all sit in one place.

**In njin:** the whole game is also a state machine: menu, level and game over are **scenes**
(`njin::scene_register()`, `njin::scene_set()`, see @ref scenes). The animation switching between idle, run and jump is a
state graph (`njin::anim_graph_create()`, see @ref animation).

## 4. Timers

"Respawn after 2 seconds", "spawn an enemy every 3 seconds". Do not keep a counter variable of your own for each of these. One list
of timers (the bottom half of the program above) handles them all. There are two easy mistakes, and the program avoids both:

- a called function can **create a new timer** (the "timer inside a timer" line), so you must collect the due functions into a separate list
  first, and only then call them;
- counting time by adding up floating-point numbers drifts (`0.05` added many times does not come out as exactly `0.25`), so the example uses `dt = 1/16`.

**In njin:** `njin::timer_after()` and `njin::timer_every()` do exactly this. The functions run in `phase_update`, and they may
also do work that adds or destroys entities. See @ref screen_timers.

## 5. Events

When an enemy dies, you need to add score, play a sound and drop an item. If the enemy's code calls each of those, the enemy has to know about all of them.
With events, the enemy only **announces** "I died"; whoever cares signs up to listen.

@include learn_gp_events.cpp

Run:

```
before flush: score = 0
  sound: enemy 1 died
  sound: enemy 2 died
after flush:  score = 350
press W:       jump = yes
press left arrow: jump = no
after rebinding: jump = yes
```

The score is `0` before `flush()` and `350` after it: events are **queued** and then sent at a fixed point in the frame. Thanks to that,
calling `enqueue` in the middle of walking the enemy list does not break that list.

**In njin:** `njin::events()` is an `entt::dispatcher`. `enqueue` queues an event, and queued events are sent right after
`phase_post_update`; `trigger` sends one immediately. See @ref ecs.

## 6. Actions instead of keys

The second half of the program above. Game code asks `pressed("jump")`, not `Space key`. The table that ties "jump" to keys
lives in one place: adding the W key, adding a gamepad, or letting the player rebind keys all just edit that table, and not one line of logic
changes.

**In njin:** `njin::action_define(ctx, "jump", {njin::key_space, njin::key_w, njin::pad_face_down})`, then
`njin::action_pressed()`. See @ref input.

## One more pattern: handles instead of pointers

In many places in njin you will see a `struct` that holds only an `id` number, where `id == 0` means "invalid"
(`njin::timer_handle`, `njin::texture_handle`, `njin::sound_handle`...). The engine keeps the real resource inside, and the game
holds only the number. There are no dangling pointers, and once a resource is destroyed, the old number just becomes "invalid" instead of crashing
the program. Lesson @ref learn_cpp_types builds a store like that.

## Self-check

1. Why does physics use a fixed step but drawing does not?
2. What is `max_steps` in the fixed loop for? What happens if you remove it and the game freezes for half a second?
3. In ECS, what do you do to make a thing both move and have health? And with inheritance?
4. Why is `enqueue` safer than calling the listeners directly while walking the enemy list?
5. Why does counting time by adding `0.05f` many times drift easily?

## Exercises

1. In `learn_gp_loop.cpp`, add FPS `240` to the list. Does the "fixed step" column change? Why?
2. Add **interpolation** to the fixed loop: save `prev_y` before each step, and each frame compute the draw position
   `prev_y + (y - prev_y) * alpha`. Count, at 144 FPS, in how many frames `y` changes and in how many frames the draw position changes.
3. Add to the mini ECS `registry` a function `each<A>(f)` that walks the entities that have component `A`. Then use it to count the entities that have
   `health` and their total health.

## Answers

**Self-check**

1. Physics must give the same result at every FPS and must not let things pass through walls when FPS drops, so it has to follow a fixed step.
   Drawing only has to be on time and smooth: draw every frame, with interpolation if needed.
2. It caps the number of physics steps in one frame. Without it, after the game freezes for half a second (30 steps piled up), the next
   frame has to run all 30 steps, which takes longer, so the frame after that piles up even more: the game gets slower and slower.
3. ECS: attach both `velocity` and `health` to the entity. Inheritance: you need a class that inherits from both (multiple inheritance) or a new class just
   for that combination, and the number of combinations grows very fast.
4. `enqueue` only writes to the queue, so neither the enemy list nor the listener list changes while you are walking them. Sending the
   event happens later, at a fixed point in the frame.
5. `0.05` cannot be represented exactly in binary floating point, so a tiny error adds up over many additions. Use a value that
   divides exactly (like `1/16`), count with integers, or compare with a tolerance.

**Exercises**

1. The "fixed step" column does not change, it stays `42.50`, while the "step = frame" column becomes `44.38`. The number of physics steps depends only on the time that has passed (`floor(t / (1/60))`), not on
   how that time is split into frames. FPS only changes how many steps run in each frame.
2. The full program:

   @include learn_gp_alpha.cpp

   It prints `frames: 72, y changed in 28 frames, draw_y changed in 69 frames, draw_y always between prev and y: yes`. The physics position `y`
   changes in only 28 of the 72 frames (the other frames have no step), so it looks jerky. The draw position changes in almost every frame
   and always lies between two physics positions. That is the price of interpolation: the draw position lags one step behind.
3. Add to `struct registry`:

   ```cpp
   template <class A, class F> void each(F f) {
     for (const entity e : alive)
       if (has<A>(e))
         f(e, get<A>(e));
   }
   ```

   Use: `reg.each<health>([&](entity, health &h) { count++; total += h.hp; });`. With three entities (two with health 5 and 2, one without)
   it prints `2 entities have health, 7 health in total`.

Every answer above was compiled and run with `g++ -std=c++20 -Wall -Wextra -Wpedantic`.

## Next step

You have gone through all the foundations. From here:

- @ref setup : set up your environment
- @ref getting_started : your first njin program
- @ref first_jump and @ref first_walk : a character that runs, in 50 lines
