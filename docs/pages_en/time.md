# Time, fixed update, timers and tweens {#time}

## The kinds of time

| Function | Returns | Affected by speed and pause |
|---|---|---|
| njin::delta() | The frame's time (seconds) | Yes |
| njin::delta_real() | The frame's real time | No |
| njin::elapsed() | Time since the game opened | No |
| njin::fixed_delta() | The length of one fixed tick | Never changes |

Most code uses njin::delta(). Use njin::delta_real() for things that must keep running while the game
is stopped: the pause menu, UI effects.

## The machine's clock

The times above only count while the game runs. Anything counted in **real time**, even while the
game is closed (a building that takes 8 hours, a mine producing gold while the player is away, daily
rewards), uses the machine's clock:

| Function | Returns |
|---|---|
| njin::time_unix() | Seconds since 1 January 1970 UTC |
| njin::time_date() | That time split into a njin::date_time: year, month, day, hour, minute, second, weekday |
| njin::time_from_date() | The reverse; a field out of range carries into the larger one (`day + 1` of a month's last day is the 1st of the next) |

`local = true` (the default) is the machine's time zone: a new day starts at the player's midnight.
`false` is UTC: every machine changes day together.

@code
// When saving the game.
save["saved_at"] = njin::time_unix();

// When opening it again: gold mined while away, at most 8 hours.
const njin::i64 away = njin::time_unix() - saved_at;
if (away > 0)
  gold += std::min<njin::i64>(away, 8 * 3600) * gold_per_second;

// The daily reward resets at the coming midnight.
const njin::date_time today = njin::time_date(njin::time_unix());
const njin::i64 reset = njin::time_from_date({.year = today.year, .month = today.month, .day = today.day + 1});
@endcode

Players can set the machine's clock. An offline game usually accepts that; at least ignore time that
went backwards since the last save (`away < 0`), so no gold is taken away.

## Speed and pause

- njin::time_set_scale(): 1 is normal, 0.5 is half speed. It affects delta(), fixed
  update and sprite animation. Use it for slow motion.
- njin::time_set_paused(): njin::delta() goes to 0 and fixed update stops. **Every other phase still
  runs**, so input can still be read and menus can still be drawn.

## Fixed update

`phase_fixed_update` runs at a **fixed tick**, 60 times per second by default
(`config::fixed_hz`). Each frame it runs 0, 1 or a few times depending on FPS, so that the total number of ticks
matches real time.

```mermaid
flowchart LR
  A["accumulator += delta()"]:::engine --> B{"accumulator >= tick?"}:::decide
  B -- yes --> C["run phase_fixed_update<br/>accumulator -= tick"]:::fixed --> B
  B -- no --> D["phase_update"]:::update
```

In this phase, njin::delta() returns **exactly one tick**. Put **physics** here: the result is the same
whether the machine runs at 30 or 144 FPS, and collisions do not tunnel through when FPS drops.

If a frame is very long (for example stopped at a breakpoint), the remainder beyond 8 ticks is dropped, so the game
does not have to run hundreds of ticks at once. The 8 can be set with `config::fixed_max_steps`: on a weak machine,
2-4 avoids the "long frame, many ticks, even longer frame" spiral (the game slows down a little when FPS drops
instead of stuttering harder and harder), see @ref quality.

njin::fixed_alpha() tells you the leftover part of a tick (0..1). Use it to interpolate the position when drawing, for
smooth motion when FPS is higher than the physics tick:

```cpp
const njin::vec2 draw_pos = njin::lerp(prev_pos, pos, njin::fixed_alpha(ctx));
```

njin::time_set_fixed_rate() makes `phase_fixed_update` run a little faster or slower than real time (1.02 is
2% faster) without changing the length of one tick. Networked games use it to keep two machines on the same
tick; netcode sets it automatically on the client (@ref netcode).

## Timers and tweens

@include time_tween.cpp

njin::timer counts down and then reports. `tick(dt)` returns `true` exactly once when time is up (or every
loop if `repeat`). `progress()` gives progress 0..1.

njin::tween smoothly moves a value (`f32`, `vec2` or `rgba`) from `from` to `to` over
`duration` seconds, along an njin::ease curve. `tick(dt)` returns the current value.

| Curve | Feel |
|---|---|
| `linear` | Even, mechanical |
| `out_quad`, `out_cubic` | Slows down on arrival: the most natural for UI |
| `in_out_sine` | Soft at both ends: camera moves |
| `out_back` | Overshoots then returns: a panel popping out |
| `out_bounce` | Bounces like a dropped ball |
| `out_elastic` | Wobbles like a spring |

Timers and tweens are plain structs, not components. You decide where to keep them: in a
component, in a module's variables.
