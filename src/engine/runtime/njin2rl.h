#pragma once
#include "njin_internal_only.h"
#include "_types.h"

struct Vector2;
struct Vector4;
struct Color;
struct Camera2D;

namespace njin {
void to_raylib(vec2 from, Vector2 &to);
void to_raylib(vec4 from, Vector4 &to);
void to_raylib(vec4 from, Color &to);
void to_raylib(rgba from, Color &to);
void to_raylib(key_code from, i32 &to);
void to_raylib(camera_view from, Camera2D &to);
} // namespace njin
