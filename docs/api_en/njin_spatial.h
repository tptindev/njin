#pragma once
#include "_math.h"
#include "_types.h"
#include <memory>
#include <vector>

namespace njin {

/// @addtogroup grp_spatial
/// @{

/// The structure under a njin::spatial_index. Both return the same results and
/// differ only in speed: measure in your game, then choose.
enum spatial_kind : u8 {
  /// Uniform grid. Very cheap to rebuild; fastest when items are about the same
  /// size and spread fairly evenly over a bounded area (crowds, swarms, bullets).
  spatial_grid,
  /// Quadtree: splits where it is crowded, skips what is empty. Suits items in
  /// clumps, wide sparse maps, or queries with a large radius.
  spatial_quadtree,
};

/// One item in the index: a circle centred on `pos`. Its position in the array
/// given to spatial_build() is its name in every result.
struct spatial_item {
  vec2 pos{};        ///< Centre, in the world.
  f32 radius = 0.0f; ///< Radius. 0 is a point.
  /// Group, 0 for none. Items of the same non-zero group do not push each other
  /// in spatial_separate(), and spatial_query::group skips them.
  u32 group = 0;
  /// spatial_separate() does not move this item; others overlapping it back off
  /// by the whole overlap: someone sitting, a lamp post, a marching line.
  bool fixed = false;
};

/// How to build a njin::spatial_index.
struct spatial_desc {
  spatial_kind kind = spatial_grid; ///< Grid or quadtree.
  /// The area holding the items. Left empty (the default), the bounding box of
  /// all items. Items outside it are still found, only more slowly.
  rect bounds{};
  /// Grid: cell size, in world units. 0 (the default) is twice the largest
  /// radius, which suits spatial_separate(). The grid never has more than 4
  /// cells per item: smaller cells are widened.
  f32 cell_size = 0.0f;
  u32 leaf_size = 8;  ///< Quadtree: a leaf with more items than this splits in four.
  u32 max_depth = 12; ///< Quadtree: maximum depth, so many items on one spot do not split forever.
};

struct spatial_impl;

/// A spatial index for thousands of circles: find the k nearest and push
/// overlapping ones apart, with no colliders and no events.
///
/// Rebuild it every frame with spatial_build() from the new positions, then ask
/// as often as you like. The index keeps a copy of the items, so changing the
/// source array after building does not break it. Keep the index across frames
/// to reuse its memory.
struct spatial_index {
  spatial_index();  ///< An empty index; build it with spatial_build().
  ~spatial_index(); ///< Frees the index's memory.
  spatial_index(spatial_index &&) noexcept;            ///< Moves the index, no copy.
  /// Moves the index, no copy.
  /// @return This index.
  spatial_index &operator=(spatial_index &&) noexcept;
  std::unique_ptr<spatial_impl> impl; ///< Internal, do not touch.
};

/// Builds (or rebuilds) the index from `count` items.
/// @param index Index, rebuilt from scratch.
/// @param desc Grid or quadtree, and its settings.
/// @param items The items. Item `i` is named `i` in results.
/// @param count Number of items.
void spatial_build(spatial_index &index, const spatial_desc &desc, const spatial_item *items, u32 count);

/// As above, from a vector.
/// @param index Index.
/// @param desc How to build.
/// @param items The items.
inline void spatial_build(spatial_index &index, const spatial_desc &desc, const std::vector<spatial_item> &items) {
  spatial_build(index, desc, items.data(), (u32)items.size());
}

/// Number of items in the index.
/// @param index Index.
/// @return Items of the latest build, 0 if never built.
u32 spatial_size(const spatial_index &index);

/// A question for spatial_nearest().
struct spatial_query {
  vec2 at{};         ///< The point asked about.
  /// Search radius around `at`. Use `INFINITY` for no limit (the k nearest,
  /// however far).
  f32 radius = 0.0f;
  /// `false`: an item counts when its **centre** is closer than `radius` to `at`.
  /// `true`: when its **circle** overlaps the circle (`at`, `radius`), that is
  /// centres closer than `radius + item.radius`: who is touching me.
  bool touching = false;
  u32 skip = 0xFFFFFFFFu; ///< Name of one item to skip, usually the asker.
  u32 group = 0;          ///< Non-zero: skip items of this group.
  /// Extra filter: return `false` to skip item `item`. Only called for items
  /// that passed the distance test, so it may cost more than a compare. May be null.
  bool (*filter)(u32 item, void *user) = nullptr;
  void *user = nullptr; ///< Passed as is to `filter`.
};

/// One item found.
struct spatial_hit {
  u32 item = 0;           ///< Name of the item: its position in the built array.
  f32 distance_sq = 0.0f; ///< Squared distance from `at` to its centre.
};

/// Finds up to `k` items nearest to `q.at` that match `q`, nearest first.
/// @code
/// njin::spatial_hit near[4];
/// const njin::u32 n = njin::spatial_nearest(index, {.at = pos, .radius = 120.0f, .skip = self}, near, 4);
/// @endcode
/// @param index A built index.
/// @param q The question.
/// @param out Receives the results, room for `k`.
/// @param k Maximum number of items.
/// @return Number found, 0 to `k`.
u32 spatial_nearest(const spatial_index &index, const spatial_query &q, spatial_hit *out, u32 k);

/// As above; `out` is cleared, then receives the results.
/// @param index A built index.
/// @param q The question.
/// @param out Receives the results, nearest first.
/// @param k Maximum number of items.
/// @return Number found.
inline u32 spatial_nearest(const spatial_index &index, const spatial_query &q, std::vector<spatial_hit> &out, u32 k) {
  out.resize(k);
  const u32 n = spatial_nearest(index, q, out.data(), k);
  out.resize(n);
  return n;
}

/// Works out pushes that part overlapping items: soft collision for crowds.
///
/// Every item that is not `fixed` is pushed by the **k nearest items
/// overlapping it**: when both can move, each backs off half the overlap; from
/// a `fixed` item, the other backs off all of it. Items of the same non-zero
/// group do not push each other. Only the k nearest push, so in a dense clump
/// an item is not thrown out by dozens at once; what overlap remains melts over
/// the next frames. Pushes come from the positions at build time, so they do
/// not depend on order, and never exceed the item's radius.
///
/// Call it every frame after rebuilding the index, then add `push[i]` to the
/// position of item `i`.
/// @param index A built index.
/// @param push Receives each item's push, in the order of the built array (0 for
/// `fixed` items and items touching nothing).
/// @param k How many nearest overlapping items count for each item.
/// @return Total pushes (a pair of two movable items counts twice).
u32 spatial_separate(const spatial_index &index, std::vector<vec2> &push, u32 k = 6);
/// @}
} // namespace njin
