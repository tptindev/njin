#pragma once
#include "_comps.h"
#include "_types.h"
#include <entt/entity/fwd.hpp>

namespace njin {
struct context;

/// @addtogroup grp_prefab
/// @{

/// Builder function of a prefab: attaches components to the newly created entity.
///
/// The entity already has a transform (and njin::scene_owned if the prefab asks for it) when
/// the function is called. The function may spawn other prefabs and attach them as children with
/// njin::child_of, for example a character with a weapon.
using prefab_fnc = void (*)(context &ctx, entt::entity entity);

/// Description of a prefab, used with prefab_register().
///
/// A prefab is a **template for creating entities**: a named builder function. Write the builder
/// once, then spawn it as many times as needed anywhere, including looking it up by name
/// (for example a name read from a level file).
struct prefab_desc {
  const char *name = nullptr; ///< Prefab name, must be unique.
  prefab_fnc build = nullptr; ///< Builder function. Must not be null.
  /// Attaches the entity to the running scene at spawn time (njin::scene_owned), so it
  /// disappears by itself when leaving the scene. Turn off for things that live across several scenes.
  bool scene_owned = true;
};

/// Registers a prefab. If the name already exists, returns the old prefab and ignores `desc`.
/// @param ctx Engine context.
/// @param desc Prefab description.
/// @return Prefab handle, or a handle with id 0 if the name or builder function is missing.
prefab_handle prefab_register(context &ctx, const prefab_desc &desc);

/// Finds a prefab by name.
/// @param ctx Engine context.
/// @param name Prefab name.
/// @return Prefab handle, or a handle with id 0 if there is none.
prefab_handle prefab_find(const context &ctx, const char *name);

/// Creates an entity from a prefab, placed at `at`.
///
/// Order: create the entity, attach the transform `at`, attach njin::scene_owned (if
/// `prefab_desc::scene_owned` and there is a scene), then call the builder function. Make any
/// further changes on the returned entity.
/// @code
/// const entt::entity e = njin::prefab_spawn(ctx, g.coin, {.pos = {120, 80}});
/// world(ctx).get<coin>(e).value = 5;
/// @endcode
/// @param ctx Engine context.
/// @param prefab Prefab to create.
/// @param at Initial transform.
/// @return The entity just created, or `entt::null` if the handle is invalid.
entt::entity prefab_spawn(context &ctx, prefab_handle prefab,
                          const transform &at = {});

/// Like prefab_spawn(), looking up the prefab by name.
/// @param ctx Engine context.
/// @param name Prefab name.
/// @param at Initial transform.
/// @return The entity just created, or `entt::null` if there is no prefab of that name.
entt::entity prefab_spawn(context &ctx, const char *name,
                          const transform &at = {});

/// Creates an entity from a prefab and attaches it as a child of `parent`.
///
/// `local` is the position relative to the parent (see njin::child_of). The entity's transform
/// is computed immediately, so it is in the right position from the first frame.
/// @param ctx Engine context.
/// @param prefab Prefab to create.
/// @param parent Parent entity. Must have a transform.
/// @param local Transform relative to the parent.
/// @return The entity just created, or `entt::null` if the handle is invalid.
entt::entity prefab_spawn_child(context &ctx, prefab_handle prefab,
                                entt::entity parent,
                                const transform &local = {});
/// @}
} // namespace njin
