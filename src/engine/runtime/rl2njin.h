#pragma once

#include "_types.h"

struct Vector2;
struct Vector4;
struct Color;
struct Camera2D;

namespace njin {
void from_raylib(Vector2 from, vec2 &to);
void from_raylib(Vector4 from, vec4 &to);
void from_raylib(Color from, vec4 &to);
void from_raylib(Color from, rgba &to);
void from_raylib(Camera2D from, camera_view &to);
} // namespace njin
