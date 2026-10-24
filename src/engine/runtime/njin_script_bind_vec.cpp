// The `njin` Lua module: the vec2 and vec3 types. Split from
// njin_script_bind.cpp so the two compile in parallel.
#include "njin_script_rt.h"

#include "_math.h"

namespace njin {
void script_bind_vectors(sol::state &lua) {
  lua.new_usertype<vec2>(
      "vec2", sol::call_constructor,
      sol::factories([](sol::optional<f32> x, sol::optional<f32> y) { return vec2{x.value_or(0.0f), y.value_or(0.0f)}; }),
      "x", &vec2::x, "y", &vec2::y, sol::meta_function::addition, [](vec2 a, vec2 b) { return a + b; },
      sol::meta_function::subtraction, [](vec2 a, vec2 b) { return a - b; }, sol::meta_function::multiplication,
      sol::overload([](vec2 a, vec2 b) { return a * b; }, [](vec2 a, f32 s) { return a * s; },
                    [](f32 s, vec2 a) { return a * s; }),
      sol::meta_function::division,
      sol::overload([](vec2 a, vec2 b) { return a / b; }, [](vec2 a, f32 s) { return a / s; }),
      sol::meta_function::unary_minus, [](vec2 a) { return -a; }, sol::meta_function::equal_to,
      [](vec2 a, vec2 b) { return a.x == b.x && a.y == b.y; }, sol::meta_function::to_string,
      [](vec2 a) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "vec2(%g, %g)", a.x, a.y);
        return std::string(buf);
      },
      "length", [](vec2 a) { return length(a); }, "normalized", [](vec2 a) { return normalize(a); }, "dot",
      [](vec2 a, vec2 b) { return dot(a, b); });
  lua.new_usertype<vec3>(
      "vec3", sol::call_constructor,
      sol::factories([](sol::optional<f32> x, sol::optional<f32> y, sol::optional<f32> z) {
        return vec3{x.value_or(0.0f), y.value_or(0.0f), z.value_or(0.0f)};
      }),
      "x", &vec3::x, "y", &vec3::y, "z", &vec3::z, sol::meta_function::addition,
      [](vec3 a, vec3 b) { return a + b; }, sol::meta_function::subtraction, [](vec3 a, vec3 b) { return a - b; },
      sol::meta_function::multiplication,
      sol::overload([](vec3 a, vec3 b) { return vec3{a.x * b.x, a.y * b.y, a.z * b.z}; },
                    [](vec3 a, f32 s) { return a * s; }, [](f32 s, vec3 a) { return a * s; }),
      sol::meta_function::division,
      sol::overload([](vec3 a, vec3 b) { return vec3{a.x / b.x, a.y / b.y, a.z / b.z}; },
                    [](vec3 a, f32 s) { return a * (1.0f / s); }),
      sol::meta_function::unary_minus, [](vec3 a) { return vec3{} - a; }, sol::meta_function::equal_to,
      [](vec3 a, vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }, sol::meta_function::to_string,
      [](vec3 a) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "vec3(%g, %g, %g)", a.x, a.y, a.z);
        return std::string(buf);
      },
      "length", [](vec3 a) { return length(a); }, "normalized", [](vec3 a) { return normalize(a); }, "dot",
      [](vec3 a, vec3 b) { return dot(a, b); }, "cross", [](vec3 a, vec3 b) { return cross(a, b); });
}
} // namespace njin
