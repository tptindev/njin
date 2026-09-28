# Entities, components and events {#ecs}

njin uses **EnTT** for its ECS. EnTT is part of the public API: you use `entt::registry` and `entt::dispatcher`
directly, and the engine does not wrap them.

@note EnTT has its own very complete documentation at
https://github.com/skypjack/entt/wiki. This page only covers what you need when using it in njin.

## All you need to remember

You can make a game with njin without knowing ECS. Treat `entt::entity` as **an identifier number**, not
an object: it has no functions and holds no data. The data lives in components, which you fetch through the
registry using that identifier. The ten operations below are enough for most games:

| You want | Write |
|---|---|
| Get the registry | `entt::registry &reg = njin::world(ctx);` |
| Create an entity | `const entt::entity e = reg.create();` |
| Attach a component | `reg.emplace<hp>(e, hp{.value = 3});` |
| Read or change a component (sure it exists) | `reg.get<hp>(e).value -= 1;` |
| Read if it exists | `if (hp *h = reg.try_get<hp>(e)) h->value += 10;` |
| Does the entity have this component | `reg.all_of<enemy_tag>(e)` |
| Destroy an entity | `reg.destroy(e);` |
| Is this entity still alive | `reg.valid(e)` |
| "No entity yet" | `entt::null` |
| Find the one entity with a tag (the player) | `reg.view<player_tag>().front()` |

Store the **`entt::entity`**, not a pointer or reference to a component. Before using an entity you stored a while ago
(did the monster die? was the bullet destroyed?), ask `reg.valid(e)`.

## Three concepts

| Concept | What it is | Example |
|---|---|---|
| **Entity** | An identifier that holds no data | `entt::entity` |
| **Component** | A data struct attached to an entity | njin::transform, `velocity` |
| **System** | A function that processes entities with certain components | see @ref modules_systems |

## Getting the registry

njin::world() returns the registry that holds every entity of the game:

```cpp
entt::registry &registry = njin::world(ctx);
```

## Common operations

```cpp
// Create an entity and attach components
const entt::entity e = registry.create();
registry.emplace<njin::transform>(e);

// Query: every entity that has all of these components
auto view = registry.view<njin::transform, const velocity>();
for (auto [entity, tr, vel] : view.each()) {
  tr.pos.x += vel.value.x;   // tr is a reference, you can modify it
}

// Remove a component, or destroy the whole entity
registry.remove<velocity>(e);
registry.destroy(e);
```

`const` before the component type in `view<...>` means you only read it.

There is another way to write it, shorter when you do not need the entity: a function that takes the components directly.

```cpp
registry.view<njin::transform, velocity>().each([&](njin::transform &tr, velocity &vel) {
  tr.pos += vel.value;
});
```

When you need the entity (to destroy it, to read another component), use the `for` loop above, and remember the first name in `[...]`
is the entity.

## Built-in components

The engine understands three components out of the box:

| Component | Meaning |
|---|---|
| njin::transform | Position, rotation, scale |
| njin::camera_2d | Camera configuration (offset, zoom) |
| njin::camera_on | A tag that marks the camera in use |

See @ref camera for how to use the camera.

## Your own components

Any struct is a component. No registration needed:

@include movement.cpp

A component with no data (like `struct player_tag {}`) is used to **mark** entities.
See `player_tag` in the camera example in @ref camera.

## Events

When two systems need to talk to each other without calling each other directly, use events through
njin::events(), which is an `entt::dispatcher`:

@include events.cpp

Points to remember:

- An event is any struct.
- Register a receiver with `sink<Event>().connect<&function>()` (usually in `setup`).
- `enqueue` **queues** the event. The receiver is called right after `phase_post_update` of that frame.
- To call it immediately, use `trigger` instead of `enqueue`.

## Common errors

**`only 3 names provided for structured binding ... decomposes into 4 elements`**
: `view<A, B, C>().each()` gives **the entity plus three components**, which is four elements. Write
  `for (auto [e, a, b, c] : view.each())`, or use the form of the function that takes components (no entity).
  If you do not use `e`, write `(void)e;` to avoid the unused variable warning.

**`invalid use of void expression` when calling `get` or `try_get` on a tag**
: An empty component (`struct player_tag {};`) has no data to return. Ask with `reg.all_of<player_tag>(e)`
  and attach it with `reg.emplace<player_tag>(e)`.

**`Assertion failed: ... Set does not contain entity` at runtime (Debug build)**
: `reg.get<X>(e)` on an entity that has no `X`. `get` requires it to exist; when you are not sure, use
  `try_get` or `all_of`. The Release build does not check and the behavior is undefined, so fix it right away when
  you hit it in Debug.

**Destroying monsters while iterating over them**
: Collect them into a `std::vector<entt::entity>` and destroy them after the loop ends.

Need to create many identical entities (monsters, bullets, items)? Do not repeat the chain of `emplace` calls: write a builder function once with njin::prefab_register(), see @ref prefabs.

Want to see these operations in a program you can run: @ref first_jump and @ref first_walk.

## Notes

- Do not hold references to components across frames: removing a component (or destroying an entity) can
  move another entity's data. Fetch it from the registry again every frame. Adding a component, on the other hand,
  does not move existing ones (measured with EnTT v4.0.0).
- Destroying entities while iterating a `view` needs care, see the EnTT documentation on this.
