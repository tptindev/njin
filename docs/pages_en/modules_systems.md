# Modules, systems and phases {#modules_systems}

Game logic in njin is divided into three concepts:

| Concept | What it is | Type |
|---|---|---|
| **System** | A function that runs at a certain moment in the frame | njin::sys_fnc |
| **Phase** | A stage of the frame that systems are attached to | njin::sys_phase |
| **Module** | A named group of systems, registered together | njin::mod_desc |

## System

A system is a plain function that takes the engine's context and returns nothing:

```cpp
void move(njin::njin_ctx &ctx);
```

It has no state of its own. Data lives in entity components (see
@ref ecs) or in the file's variables.

## Phase

Every frame calls the phases in a fixed order. Details are in @ref game_loop.

| Phase | When it runs | Used for |
|---|---|---|
| njin::phase_startup | Once, before the first frame | Creating entities, loading resources, binding keys |
| njin::phase_pre_update | Every frame | Preparing before the update |
| njin::phase_fixed_update | At a fixed rate, 0 or more times per frame | Physics, collision (see @ref time) |
| njin::phase_update | Every frame | Main logic |
| njin::phase_post_update | Every frame | Work to do after everything has updated, for example the camera following the player |
| njin::phase_pre_render | Every frame | Preparing to draw |
| njin::phase_render | Every frame | Drawing in world space |
| njin::phase_post_render | Every frame | Drawing in screen space (UI) |
| njin::phase_shutdown | Once, after the window closes | Cleanup |

## Module

A module is where you declare systems. It consists of a name and a `setup` function:

@include hello_module.cpp

How it works:

1. njin_mod_register() calls `setup` exactly once.
2. Inside `setup`, you call ecs_register() for each system to attach it to a phase.
3. From then on, the system runs whenever its phase runs.

The rules:

- njin_mod_register() must be called **before** njin_run().
- Module names must be **unique**. Registering the same name twice is ignored and logs a warning.
- ecs_register() is only valid **inside `setup`**. Calling it elsewhere is ignored and logs a warning.
- The engine's core modules (camera, audio, sprite) are already registered by njin_create().

### Registering several modules at once

Instead of calling njin_mod_register() for each module, pass a whole list:

```cpp
njin::njin_mod_register(*ctx, {input_module(), physics_module(), ui_module()});
```

A list built at run time (`std::vector<njin::mod_desc>`, `std::array`) works too:

```cpp
std::vector<njin::mod_desc> mods{input_module(), physics_module()};
if (debug)
  mods.push_back(debug_overlay_module());
njin::njin_mod_register(*ctx, mods);
```

The result is identical to calling each module in turn: the earlier module runs first within the same phase,
and a failing module (for example a duplicate name) is skipped on its own, while the others are still registered.

## Run order

There are two levels:

1. **Between modules**: they run in registration order. The module registered first has its systems
   run first, within the same phase.
2. **Within a module, within a phase**: by the constraints you declare with
   njin::sys_desc.

If you declare nothing, systems run in exactly the order you registered them. When you need control:

@include system_order.cpp

| Field | Meaning |
|---|---|
| `after` | Systems that must run **before** this system |
| `before` | Systems that must run **after** this system |
| `order` | A smaller value runs first, among the systems allowed by `after`/`before` (default 100) |
| `scene` | Only runs while this scene is running. Leave empty to run in every scene (see @ref scenes) |

When two systems are both ready and have the same `order`, the one registered first runs first.

@warning `after`/`before` can only refer to systems in the **same module and the same
phase**. A reference to a system elsewhere is ignored and logs a warning. If the
constraints form a cycle (A after B, B after A), the engine logs an error and lets the remaining systems
run in registration order.

## A complete example: movement

A `velocity` component defined by the game itself, and a system that adds velocity to position:

@include movement.cpp

Note the `njin::delta(ctx)` line: it multiplies velocity by the frame time so that speed
does not depend on FPS.
