# Math, collision, random numbers {#math}

These parts are header-only and do not depend on the engine: you can use them outside a system too.

@include math_collide.cpp

## vec2

njin::vec2 supports the full set of operations:

```cpp
njin::vec2 a{3, 4}, b{1, 2};
a + b;  a - b;  a * 2.0f;  2.0f * a;  a / 2.0f;  -a;
a * b;  // multiply component by component
a += b; a *= 0.5f;
a == b;
```

| Function | Returns |
|---|---|
| njin::dot(), njin::cross() | Dot product, 2D cross product |
| njin::length(), njin::length_sq(), njin::distance() | Length, squared length, distance |
| njin::normalize() | A vector of length 1 (`{0,0}` is left as it is) |
| njin::rotate(), njin::from_angle(), njin::angle_of() | Rotate, vector from an angle, angle of a vector |
| njin::lerp(), njin::clamp(), njin::move_toward() | Interpolate, limit, move toward a target |

**Angles** are in degrees, **clockwise on screen** (because the y axis points down):
0 degrees is to the right, 90 degrees is down. It is the same convention as njin::transform::rot.

njin::rect (top-left corner + size) and njin::circle are the two basic shapes.
njin::rect_from_center() and njin::rect_center() convert back and forth with the center.

## Collision

**Overlap tests** (true/false): njin::point_in_rect(), njin::point_in_circle(),
njin::rects_overlap(), njin::circles_overlap(), njin::circle_rect_overlap().

**Collision with push-out** returns njin::contact: njin::collide_rects(), njin::collide_circles(),
njin::collide_circle_rect(). When `hit` is true, moving the first object by `normal * depth`
leaves the two just touching at the edge:

```cpp
const njin::contact hit = njin::collide_rects(player, wall);
if (hit.hit)
  player.pos += hit.normal * hit.depth;
```

njin::reflect() reflects a velocity across a normal: a ball bouncing off a wall.

@note Merely touching at the edge does **not** count as overlapping. That way an object that was just pushed out is not
considered to still be colliding on the next frame.

## Random numbers

njin::random() returns the engine's shared generator, seeded from the time when the
game opened:

```cpp
njin::rng &r = njin::random(ctx);
r.range(0.0f, 1.0f);   // a float in [0, 1)
r.range(1, 6);         // a die: 1 to 6, 6 included
r.chance(0.25f);       // true with a probability of 25%
r.direction();         // a vector of length 1, in any direction
r.point_in(area);      // any point inside a rectangle
```

njin::rng is **reproducible**: the same seed gives the same sequence. Make your own generator when you need
an independent sequence, for example generating a map from a code number:

```cpp
njin::rng level_rng(seed);
```
