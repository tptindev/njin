#pragma once
#include "../njin_internal_only.h"
#include "_types.h"

namespace njin {
// The GPU time of end_3d's stages (render_stats::gpu3d), by timestamp queries
// (GL 3.3): a pass writes one timestamp at its start and one after each stage,
// into a record of its own; the records are read back a few passes later,
// only once their results are in, so nothing waits for the GPU. Where timer
// queries are missing, every call is a no-op and the times stay zero.
constexpr i32 gpu_timer_stages = 7;

// Starts a pass's record: the first timestamp. False when unsupported.
bool gpu_timer_begin();
// The timestamp after stage `index` (0..gpu_timer_stages - 1) of this pass.
void gpu_timer_stage(i32 index);
// Adds the stage times of every record whose results are in to `ms`
// (gpu_timer_stages floats), and frees those records for reuse.
void gpu_timer_collect(f32 *ms);
// Deletes the queries, while the GL context is open.
void gpu_timer_free();
} // namespace njin
