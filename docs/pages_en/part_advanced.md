# Part 7: Advanced {#part_advanced}

**Level: advanced.** You do not need these pages to have a working game. Read them once the game exists and you want it
prettier, faster, with more content, or you want to understand the engine inside. Each page stands alone, so read
them as you need.

**Scope:** @ref rendering, @ref shader_advanced and @ref post_processing are **2D**. @ref graphics_3d is
**3D**, its own page for the whole of `njin_3d.h` (perspective camera, primitives, SDF, models, shadowed
lighting, instancing). @ref procgen and @ref debug are **shared** between the two.

## Graphics

| Page | What you get |
|---|---|
| @subpage rendering | Textures, atlases, shaders, instancing (thousands of shapes with one draw call), editing images and shaders while the game runs |
| @subpage graphics_3d | Perspective camera, smooth SDF primitives, glTF models, shadowed lighting, 3D particles, instancing, ray picking, gizmos |
| @subpage world_3d | Terrain with collision, grass, rocks and trees scattered by rules, lakes and seas with waves, floating bodies, a sky by time of day, rain, snow, fog |
| @subpage post_3d | Wall corners that darken (SSAO), polished floors that reflect, bullet holes and paint stuck to any surface (decals), motion blur, light shafts, lens flare |
| @subpage spatial_batch | Draw only the part of tens of thousands of instances the camera sees, fine shapes up close and low-poly ones far away |
| @subpage lighting | Physically based (PBR) 2D lighting: point, spot and directional lights; soft shadows from occluders of any shape; normal maps, MRA materials, emission, tonemapping; per-pixel shadows after mattdesl's tutorial |
| @subpage shader_advanced | Shaders that read extra images (palettes, noise), take uniform arrays (lights), run over the whole frame, chain several passes together |
| @subpage post_processing | Full-screen effects: bloom, CRT, vignette, blur |

To write your own shaders, first study the three shader lessons in @ref learn (lessons 10 to 12).

## Content

| Page | What you get |
|---|---|
| @subpage procgen | Generate maps with noise, rules, corner rounding and Wave Function Collapse |

## Scripts

| Page | What you get |
|---|---|
| @subpage scripting | Write game rules and entity behaviour in Lua, edit files while the game runs, call functions both ways between C++ and Lua |

## Tools

| Page | What you get |
|---|---|
| @subpage debug | njin_inspector: view FPS, entities, colliders and the log in a separate window |

To understand the engine inside, see @ref architecture in @ref part_appendix.
