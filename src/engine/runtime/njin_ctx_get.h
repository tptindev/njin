#pragma once
#include "njin_internal_only.h"

#include "njin_features.h"

// Reaching a member of the context without its definition (njin_ctx_impl.h).
//
// ctx_ref<T>(ctx) is the member that holds a T: the store_ref or feature_ref
// of a store or a feature's state, or the member itself for the few plain
// ones below. A source includes this and the header of each T it uses, so
// adding a member to the context, or editing one store's header, rebuilds
// only the sources that use it.
//
// Debug builds call them (njin_ctx_impl.cpp makes them, once). Other
// configurations define NJIN_CTX_INLINE: the context's definition comes with
// this header and every ctx_ref is inline, as plain member access was.
namespace njin {
struct context;
struct time_state;
struct frame_state;
struct window_guard;
struct rng;
struct config;

template <class T> struct ctx_slot {
  using type = feature_ref<T>;
};
template <> struct ctx_slot<feature_hooks> {
  using type = feature_hooks;
};
template <> struct ctx_slot<time_state> {
  using type = time_state;
};
template <> struct ctx_slot<frame_state> {
  using type = frame_state;
};
template <> struct ctx_slot<rng> {
  using type = rng;
};
template <> struct ctx_slot<config> {
  using type = config;
};
template <> struct ctx_slot<window_guard> {
  using type = window_guard;
};
template <class T> using ctx_slot_t = typename ctx_slot<T>::type;

#if defined(NJIN_CTX_INLINE)
#define NJIN_CTX_FN inline
#else
#define NJIN_CTX_FN
#endif

template <class T> NJIN_CTX_FN ctx_slot_t<T> &ctx_ref(context &ctx);
template <class T> NJIN_CTX_FN const ctx_slot_t<T> &ctx_ref(const context &ctx);
// The stores a const context still changes (the context declares them
// mutable): fonts bake atlases, sdf2d and backdrop load shaders on first draw.
template <class T> NJIN_CTX_FN ctx_slot_t<T> &ctx_mut(const context &ctx);
} // namespace njin

#if defined(NJIN_CTX_INLINE)
#include "njin_ctx_impl.h"
#endif
