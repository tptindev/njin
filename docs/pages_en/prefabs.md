# Prefabs and parent-child transforms {#prefabs}

## Prefabs

A prefab is a **template for creating entities**: a named builder function that attaches components to a freshly created entity.
Write the builder once, then spawn as many as you like, even by looking them up by name.

@include prefabs.cpp

njin::prefab_spawn() does the following in order:

1. creates the entity;
2. attaches njin::transform from the `at` parameter;
3. attaches njin::scene_owned pointing to the running scene (if there is one, and if `prefab_desc::scene_owned`
   has not been turned off), so leaving the scene removes the entity automatically;
4. calls the builder function.

To make one a little different (more health, another color), edit the returned entity:

```cpp
const entt::entity boss = njin::prefab_spawn(ctx, "goblin", {.pos = {500, 300}, .scale = 3});
njin::world(ctx).get<health>(boss).hp = 50;
```

| Function | What it does |
|---|---|
| njin::prefab_register() | Register a prefab. If the name already exists, returns the old one |
| njin::prefab_find() | Look up by name |
| njin::prefab_spawn() | Create an entity from a prefab (handle or name) |
| njin::prefab_spawn_child() | Create an entity from a prefab and attach it as a child of another entity |

## Parent-child transforms

Attach njin::child_of to an entity so it follows a parent entity: a weapon in a hand, wheels, the
shadow under the feet, a fire emitter on a rocket's tail.

```cpp
reg.emplace<njin::child_of>(gun, njin::child_of{.parent = player, .local = {.pos = {12, -4}}});
```

Every frame, in `phase_post_update`, the engine's hierarchy module **overwrites** the child's transform with
the parent's transform combined with `local` (see njin::transform_combine()):

| Child's | Is computed as |
|---|---|
| `pos` | `parent.pos + rotate(local.pos × parent.scale, parent.rot)` |
| `rot` | `parent.rot + local.rot` |
| `scale` | `parent.scale × local.scale` |

So:

- **Move the child through `local`**, not through its own transform (which would be overwritten).
- A parent can be a child of another entity; the engine computes from the root down.
- When the parent is destroyed, the child is destroyed with it. Set `destroy_with_parent = false` to let the child stay, detached
  at its last position.
- A system in `phase_update` that reads the child's transform sees the previous frame's value. If you need
  it exact right away, compute it yourself with njin::transform_combine().
- To attach an entity that is already standing in the world to a parent without making it jump: use
  njin::transform_relative() to compute `local`.

Flipping (`sprite.flip_x`) does not flip children automatically. When the character turns left, flip the sign of the weapon's
`local.pos.x`.
