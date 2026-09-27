#pragma once
// The steps creature_tick drives, one per part of the creature, plus the one
// question the parts ask of each other.
//
// Private to creature/: these are the inside of a tick, and the order they run
// in is tick.cpp's business. Each lives in a file of its own.
#include "creature.h"

namespace moteswarm {
// Where the circles sit this frame, and how far the shape they make reaches
// from the centre. The second reads what the first wrote, so they run in that
// order, and both run before anything that asks about the silhouette.
void place_blobs(creature &c, vec2 center, f32 breath_uniform, f32 breath_tall);
void measure_extents(creature &c, vec2 center);

// How far out of round the body is, along the heading against across it.
// Reads what measure_extents wrote.
f32 body_aspect(const creature &c);

// effort is everything the body is doing under its own power this frame, the
// swimming and the sculling of a turn added together, in world units per
// second.
void step_tails(creature &c, vec2 center, f32 effort, f32 dt);

// vel is the body's real velocity through the world, which the eye leans into
// and leads with. c.moving says whether it counts as going anywhere under its
// own power, which is not the same question and not read off the same number.
void step_eye(creature &c, vec2 center, vec2 vel, f32 breath_uniform, f32 breath_tall, f32 dt);
} // namespace moteswarm
