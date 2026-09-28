# Part 7: Advanced {#part_advanced}

**Level: advanced.** You do not need these pages to have a working game. Read them once the game exists and you want it
prettier, faster, with more content, or you want to understand the engine inside. Each page stands alone, so read
them as you need.

## Graphics

| Page | What you get |
|---|---|
| @subpage rendering | Textures, atlases, shaders, instancing (thousands of shapes with one draw call), editing images and shaders while the game runs |
| @subpage lighting | Physically based (PBR) 2D lighting: point, spot and directional lights; soft shadows from occluders of any shape; normal maps, MRA materials, emission, tonemapping; per-pixel shadows after mattdesl's tutorial |
| @subpage shader_advanced | Shaders that read extra images (palettes, noise), take uniform arrays (lights), run over the whole frame, chain several passes together |
| @subpage post_processing | Full-screen effects: bloom, CRT, vignette, blur |

To write your own shaders, first study the three shader lessons in @ref learn (lessons 10 to 12).

## Content

| Page | What you get |
|---|---|
| @subpage procgen | Generate maps with noise, rules, corner rounding and Wave Function Collapse |

## Tools

| Page | What you get |
|---|---|
| @subpage debug | njin_inspector: view FPS, entities, colliders and the log in a separate window |

To understand the engine inside, see @ref architecture in @ref part_appendix.
