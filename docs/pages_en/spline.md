# Curves (splines) {#spline}

This page moves things along a smooth curve: a camera running on a rail in a cutscene, a moving platform, a
patrolling monster, a dragon's flight path, a curving bullet, a race track. Everything is declared in
`njin_spline.h`, and works in 2D (njin::spline2d) as well as 3D (njin::spline3d) with the same function names.

Read first: @ref math (vec2, vec3).

## Two kinds of curve

| Kind | How the points are placed | Suits |
|---|---|---|
| `spline_catmull_rom` (default) | The curve goes **through** every point | Rails, patrol routes: dot the points and you are done |
| `spline_bezier` | Points 0, 3, 6... lie on the curve; the two points between each pair are handles pulling it | Curves whose shape needs care, like a drawing program's pen tool |

Catmull-Rom uses the **centripetal** form by default (`alpha = 0.5`): when the points are unevenly spaced the
curve makes no knots or loops. `closed = true` joins the last point back to the first into a loop.

```cpp
njin::spline3d rail;
rail.points = {{12, 4, 0}, {6, 6, 10}, {-8, 5, 9}, {-12, 3, -2}};
rail.closed = true;
njin::spline_bake(rail); // after every change to the points
```

## Moving by parameter or by distance

spline_point() reads the curve by a parameter `t`: the whole part is the segment, the fraction the place in it;
for Catmull-Rom, `t = 2` is exactly point 2. But moving evenly in `t` is **not even in distance**: long segments
go fast, short ones slow.

To move evenly (a camera that does not jerk, a platform that is not fast then slow), move by distance:
spline_bake() builds a length table, then spline_point_at() and spline_tangent_at() take a distance from the start
of the curve. spline_length() is the length of the whole curve.

| Function | Answers |
|---|---|
| spline_point(), spline_tangent() | Point and direction at parameter `t` |
| spline_point_at(), spline_tangent_at() | Point and direction at a distance along the curve |
| spline_t_at() | Parameter `t` at a distance |
| spline_length() | Length of the whole curve |
| spline_nearest() | The place on the curve nearest a point, and the distance along to it: snapping onto a rail, how far the player has come along a race track |
| spline_draw_debug() | Draws the curve and its control points with gizmos |

## Moving along the curve every frame

njin::spline_follower holds the state of something on the move: how far it has come, its speed, and what to do at
the end (`spline_stop`, `spline_loop`, `spline_ping_pong`). Call spline_follow() every frame:

```cpp
njin::spline_follower mover{.speed = 4.0f, .end = njin::spline_loop};
// every frame:
const njin::vec3 pos = njin::spline_follow(rail, mover, njin::delta(ctx));
const njin::vec3 dir = njin::spline_tangent_at(rail, mover.distance);
```

With `spline_stop`, `mover.finished` becomes true at the end. With `spline_ping_pong`, `speed` changes sign at each
turn. To go backwards set a negative `speed`.

## Full example

The camera goes round a closed rail looking along it; a platform goes back and forth along a Bezier curve; Tab
shows and hides the curves.

@include spline.cpp

## Limits

- The direction (tangent) is only a vector: there is no roll angle along the curve. A camera on a rail keeps its
  own `up`.
- The length table must be built again (spline_bake()) after each change to the points; without it the functions
  by distance build a temporary table at each call, correct but slow.
