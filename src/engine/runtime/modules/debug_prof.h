#pragma once
#include "../njin_internal_only.h"
#include "njin_json.h"
#include "_tilemap.h"
#include "njin_level.h"
#include "njin_particles.h"
#include <cstddef>
#include <entt/entity/fwd.hpp>

namespace njin {
struct context;

// What the debug link reports about consumption. Built here and sent by
// debug.cpp; the inspector shows them (Systems, Memory and Assets windows).

// `prof`: milliseconds per phase and per system in the last finished frame,
// with a smoothed value and a slowly decaying peak.
json_value debug_build_prof(const context &ctx);

// `mem`: for each component type its count, size, heap and total bytes, and
// what the registry itself holds.
json_value debug_build_mem(const context &ctx);

// `res`: every loaded texture, render target, font, shader, sound and music
// with the memory it holds, split into GPU and CPU (RAM), plus the
// engine-owned targets (tilemap chunk images, post-processing buffers).
json_value debug_build_res(const context &ctx);

// Bytes an entity's components take (payload plus what they own on the heap),
// and the GPU memory attributed to it (a tilemap's baked chunk images).
struct entity_cost {
  size_t ram = 0;
  size_t gpu = 0;
};
entity_cost debug_entity_cost(const context &ctx, entt::entity entity);

// What debug_entity_cost counts per component beyond its payload: EnTT keeps
// its slot in the packed and the sparse arrays, about two 32-bit ids.
inline constexpr std::size_t debug_component_overhead = 8;

// The GPU part of debug_entity_cost alone, for a caller that already walks the
// component pools and adds the RAM itself.
std::size_t debug_entity_gpu(const context &ctx, entt::entity entity);

// Heap owned by the engine's own components, attached to their debug entries.
std::size_t debug_heap_tilemap(const tilemap &m);
std::size_t debug_heap_particles(const particle_emitter &p);
std::size_t debug_heap_level_object(const level_object &o);
} // namespace njin
