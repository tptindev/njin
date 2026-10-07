# Lesson 11: SDF: drawing shapes with distance {#learn_shader_sdf}

**What this lesson teaches:** SDF (signed distance function) is how a shader **draws shapes with formulas**
instead of images: circles, boxes, rounded boxes, line segments, then combining, cutting out, outlining, shadows and glows. At the end
of the lesson is a health bar and a cooldown ring drawn entirely by a shader, without a single image file.

**What you need to know first:** @ref learn_shader_start : basic GLSL, uniforms, and the playground (`learn_shader_playground.c`). Every
example here runs in that playground, mode 1.

## The idea: ask "how far is this point from the shape?"

To draw a circle in a shader, the simplest question is "is this point inside or outside the shape?", answered true or
false. A true/false answer gives a **jagged** edge, and says nothing about the points near the edge. SDF asks something else:

> How far is it from this point to the nearest **edge of the shape**, and is the point **inside** or **outside**?

The answer is a **signed** number:

| Value of `d` | Meaning |
|---|---|
| `d < 0` | the point is **inside** the shape, `-d` is the distance to the edge |
| `d = 0` | the point is **exactly on** the edge |
| `d > 0` | the point is **outside** the shape, `d` is the distance to the edge |

The lesson @ref learn_shader_start already used this idea without naming it: `length(p)` is the distance from point `p` to the origin, and subtracting
the radius `r` gives the SDF of a circle:

```glsl
float sd_circle(vec2 p, float r) {
  return length(p) - r;
}
```

A number like this gives us more than "inside or outside": knowing `d` means knowing how to **soften the edge** (by `d`), **draw an outline** (`|d|`
small), **glow** (small positive `d`), **cast a shadow** (move the shape and reuse `d`), and **combine shapes** (combine the `d` values),
all from the same number.

## Seeing the distance field

The fastest way to understand SDF is to **draw `d` itself as a color**. The example below draws the SDF of a circle around the mouse (mode 1;
the circle's center follows the mouse):

@include learn_shader_sdf_field.fs

@image html learn_shader_sdf_field.png "Orange is inside (d < 0), blue is outside (d > 0). The white ring is d = 0, the edge of the circle itself. The concentric rings are places at the same distance."

Line by line:

- `float d = sd_circle(p - center, 0.2);` : `p - center` moves the shape to `center`. **Moving a shape = subtracting the position from the point being tested**;
  it sounds backward but it is right: instead of moving the shape to the right, we treat the tested point as moving to the left.
- `col *= 1.0 - exp(-6.0 * abs(d));` : near the edge (`|d|` small) it is dark, farther away it gets brighter. The function `exp(-k * x)` is a
  simple way to get a fast-falling curve; you will use it again for glows.
- `col *= 0.8 + 0.2 * cos(150.0 * d);` : the **contour lines**. `cos` of `d` repeats with distance, so each
  ring is a fixed distance from the edge. This is a familiar way to debug an SDF: if the rings are uneven or
  broken, your formula is wrong.
- `mix(col, vec3(1.0), 1.0 - smoothstep(0.0, 0.01, abs(d)))` : paints white **exactly where `d = 0`**. That is the circle.

@note When an SDF looks wrong, do not guess: show `d` as a color like above. The lesson @ref learn_shader_patterns has a section "Showing a
number as a color for debugging", the same idea.

## From distance to shape: `fill`

To fill a shape, turn `d` into a coverage from 0 to 1. The previous lesson used `smoothstep(0.28, 0.30, d)` with two fixed numbers, and
the edge only looks right at one exact size: scale up and the edge blurs, scale down and it gets jagged. The standard way uses `fwidth`:

```glsl
float fill(float d) {
  float aa = fwidth(d);                     // how much d changes when stepping to the next pixel
  return 1.0 - smoothstep(-aa, aa, d);      // 1 inside the shape, 0 outside, blurred over about one pixel at the edge
}
```

`fwidth(d)` tells how much `d` changes from this pixel to the next, that is "how long one pixel is in the coordinate
system of `d`". The edge is smooth over exactly one pixel whether the shape is big or small, and no numbers need hand-tuning.

@note `fwidth` (and the other derivative functions such as `dFdx`, `dFdy`) compare neighbouring pixels, so by the GLSL spec the result is **undefined**
when called inside an `if` branch where neighbouring pixels go different ways. Compute `aa` at the top of the function, outside any `if`,
as above. I have not tried this failure case, so this is advice based on the spec, not a measured result.

From now on, every shape is drawn with the same template: `col = mix(col, color, fill(d))`, and each new shape is drawn over the previous one.

## Basic shapes

@include learn_shader_sdf_shapes.fs

@image html learn_shader_sdf_shapes.png "Circle, box, rounded box, line segment with round ends. Every shape has a smooth edge."

Each shape is a function that takes **a point `p` (already moved to the origin)** and returns `d`:

- **Circle**: `length(p) - r`.
- **Box** `sd_box(p, b)`, where `b` is **half** the size. `abs(p) - b` folds the four quadrants into one, so only one
  corner needs to be considered. `q` negative on both axes means inside the box (the negative distance is `max(q.x, q.y)`); `q` positive means
  outside, and the distance to the box is the length of the positive part of `q` (`length(max(q, 0.0))`), exact even at the corners.
- **Rounded box**: a trick that works for **any** shape. Shrink the box by `r` on each side, then **subtract `r` from the distance**:
  `sd_box(p, b - r) - r`. Subtracting a number from `d` **inflates** the shape evenly in every direction by `r`, and sharp corners become circular
  arcs. So a rounded box is just a small box, inflated.
- **Line segment** `sd_segment`: find the nearest point **on the segment** (`h` clamped to 0 to 1, the projection of `p`
  onto the line), measure the distance to it, then subtract half the thickness. The round ends come for free.

A few transformations that work for any SDF:

| Want | Do | Example |
|---|---|---|
| Move the shape | `d = sd_x(p - position)` | `sd_circle(p - vec2(0.3, 0.0), 0.1)` |
| Inflate the shape by `r` | `d - r` | rounded box |
| Keep only an **outline** of thickness `w` | `abs(d) - w` | the hollow ring in the exercises and in the cooldown ring |
| Scale the shape up `s` times | divide `p` by `s`, then multiply `d` by `s` | multiply `d` back so it stays a true distance (by definition, not measured) |

## Combining shapes by combining distances

This is where SDF is strongest: combining two shapes is just combining two numbers.

| Operation | Formula | Meaning |
|---|---|---|
| Union | `min(a, b)` | inside `a` **or** inside `b` |
| Intersection | `max(a, b)` | inside `a` **and** inside `b` |
| Subtraction | `max(a, -b)` | inside `a`, **minus** what is inside `b` |
| Smooth union | `smin(a, b, k)` | like union, but the two shapes **stick** together like water drops |

@include learn_shader_sdf_combine.fs

@image html learn_shader_sdf_combine.png "Three different moments (one per row): union (yellow), intersection (blue), subtraction (green), smooth union (pink). The two circles move closer and then apart."

Smooth union is the hardest thing to do with images: as they get close, the two pink circles stick and **pinch at the waist** in the middle (second row in the image); closer
still, they become one blob. `k` is the "stickiness": a large `k` sticks from far away.

@note Mathematically, the `min` of two SDFs is still an exact SDF **outside**, while `max` (intersection, subtraction) and smooth union give a
number with the **right sign** (inside or outside is always right) but no longer right to the unit of distance. That is enough for drawing shapes; if you use `d` to
make a glow or a very soft shadow, the glow can be slightly distorted where the two shapes meet.

## Using it in a game: UI without images

Health bars, button borders, cooldown rings are all simple shapes, and SDF draws them **at any resolution, any size, without
images**. This example draws a health bar and a cooldown ring, 70% full (`mouse.x` is how full they are; try moving the mouse):

@include learn_shader_sdf_ui.fs

@image html learn_shader_sdf_ui.png "Health bar (70% full, with a soft red glow) and a cooldown ring going clockwise from the top."

**Health bar.** `frame` is the outer rounded box, `inner` the smaller box inside. The filled health part is the **intersection** of `inner`
with "left of the cut line": `max(inner, cut)`, where `cut = q.x - cut_line_position` is negative left of the cut line. Changing the amount of
health is just moving the cut line, and the shape keeps its rounded corners.

**Cooldown ring.** `abs(length(c) - 0.17) - 0.028` is a **thick ring** (the outline of a circle of radius 0.17). To
fill part of the ring by angle you need the point's angle: `atan(c.x, -c.y)` gives 0 at the top and grows clockwise; divide
by `2π` and take the fractional part (`fract`) to get 0 to 1. Points whose angle is less than `amount` have finished recovering.

### A bug I hit while writing this example

The first version of the health bar added the glow with
`col += color * 0.25 * exp(-40.0 * max(frame, 0.0))`. Measuring real pixels, the red health part came out `(255, 76, 92)` instead of
`(230, 64, 77)` as computed, and the inside of the bar (which should be dark) also lit up to `(70, 25, 36)`. The cause: **inside** the shape
`frame` is negative, `max(frame, 0.0)` is 0, and `exp(0)` is **1**, so the glow was added at full strength inside as well.
The glow should only be outside, so the correct version also multiplies by `(1.0 - fill(frame))`. The general lesson: **functions of `d` for glows
and shadows must be checked on the negative side of `d` too**, not only on the positive side you are looking at.

## In njin

njin uses exactly this kind of shader, and the example above runs unchanged:

@include learn_shader_sdf_njin.cpp

@image html learn_shader_sdf_njin.png "The same health bar shader, running in njin: an image stretched into a 400 x 225 frame, the shader making the shapes on it."

Points to remember (tried and run):

- The shader **does not use the image's colors**, only the coordinate `fragTexCoord` running 0 to 1 across the image being drawn. So any image
  stretched to the frame size you need will do; changing the frame size means changing the `scale` of njin::texture_draw_desc.
- `resolution` is the size of the **drawn frame**, not of the window, because it is used to keep circles from being squashed. Set it
  with njin::shader_set_vec2() **before** njin::shader_begin().
- Draw in `phase_post_render` for UI (it does not go through the camera), or in `phase_render` for objects in the world.
- You can see njin's built-in post-processing shader using this very idea: the vignette of njin::post_fx measures the distance from the center of the screen
  (`length(...)` in `src/engine/runtime/modules/post_fx.cpp`) and then applies `smoothstep`, which is the SDF of a point.
- njin **does not have** built-in SDF drawing functions yet, nor SDF font support (searched in the source code): for health bars, button borders and text,
  images are still the usual way; SDF suits best simple shapes that must look sharp at any size and need distance-based effects
  (outlines, glows, shadows).

## When to use it, and when not to

**Use it for**: simple geometric shapes (circles, boxes, rings, line segments), bar and ring style UI, shockwaves, glows,
attack ranges, anything that needs **a smooth edge at any size** or **parameters set by numbers** (health amount, progress).

**Do not use it for**: complex shapes such as characters, trees, hand-drawn lettering. Those already exist as images, and drawing them with formulas is just extra work. The cost
per pixel also grows with the **number of shapes** in the shader (each shape is one computation of `d` for every pixel of the frame), so do not cram
dozens of shapes into one full-screen shader. If you only need a few shapes, keep the drawn frame small (like the 400 x 225 above) so the cost is
the number of pixels in the frame, not of the whole screen.

## Self-check

1. What does `d = -0.05` at a point mean? And `d = 0.3`?
2. Why is a rounded box `sd_box(p, b - r) - r` and not `sd_box(p, b) - r`?
3. Why is `fwidth(d)` better than `smoothstep(0.28, 0.30, d)` with two fixed numbers?
4. Which formula do you use to draw the **outline** of a shape (only the border, hollow inside)?
5. How does smooth union differ from ordinary union when two shapes are close together?

## Exercises

1. **Shockwave**: a thin ring that grows from the middle and fades out, repeating (use `time`).
2. **Plus sign**: draw a plus sign in the middle by combining shapes.
3. **Drop shadow**: a light rounded box on a light background, with a soft shadow offset down and to the right.

## Answers

**Self-check**

1. `-0.05`: the point is **inside** the shape, `0.05` from the edge. `0.3`: the point is **outside** the shape, `0.3` from the edge.
2. `sd_box(p, b)` is already the full-size box; subtracting `r` inflates it into a box **bigger** by `r` on each side. To keep the size
   `b`, you must first shrink the box by `r` (`b - r`), then inflate it by `r`: the final shape still fits the `b` frame, only with round corners.
3. The two fixed numbers are **distances in the shape's units**, so the edge is only smooth at one size: scale up and the edge blurs
   wider, scale down and the edge gets jagged. `fwidth(d)` measures "how long one pixel is", so it adapts by itself.
4. `abs(d) - w`: close to 0 only near the edge, and `w` is half the outline thickness.
5. Ordinary union `min(a, b)` keeps both shapes intact and they only touch at a sharp corner. Smooth union **fills in** the space between the
   two shapes with a rounded neck, so they stick like water drops and, as they separate, pinch at the waist gradually.

**Exercise 1: shockwave**

@include learn_shader_sdf_answer_ring.fs

@image html learn_shader_sdf_ring.png "Two moments: the ring grows and fades."

The radius grows with `fract(time * 0.4)`, `abs(length(p) - radius) - 0.015` is a ring `0.03` thick, and its strength is multiplied by
`1 - life`. I measured on the screenshot: the bright ring peaks at radius 0.177 and then 0.335 (two moments), evenly round in all four directions.

**Exercise 2: plus sign**

@include learn_shader_sdf_answer_cross.fs

@image html learn_shader_sdf_cross.png "Union of a horizontal box and a vertical box."

Checked on the image: the center and the four arms are colored, the four corners are empty.

**Exercise 3: drop shadow**

@include learn_shader_sdf_answer_shadow.fs

@image html learn_shader_sdf_shadow.png "The shadow lies to the lower right, fading outward."

The shadow is **the same shape, moved**, but instead of a one-pixel edge it uses `smoothstep(-0.03, 0.06, d)`: a blur band many times wider. This
is exactly the benefit of knowing `d`: the edge can be as soft as you like. Measured on the image: the lower right side of the box is darker than the far background,
`(122, 133, 156)` against `(140, 153, 178)`, and the opposite side (upper left) is as bright as the background.

Every example and answer in this lesson was run in the playground, and the screenshots were compared with hand-computed values at
fixed points (the center color of each shape, the cut corner of the rounded box, the smooth edge, the filled part of the health bar and the ring). The example that runs in njin is
`learn_shader_sdf_njin.cpp`, using the very same shader `learn_shader_sdf_ui.fs`. Tried on only one graphics card (Intel Iris Xe).

## Next steps

@ref learn_shader_patterns : the common shader patterns (post-processing, white flash, outline, dissolve, pixel art) and how they map
to njin, including the built-in `sprite_dissolve`.
