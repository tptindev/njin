#pragma once
#include "njin_internal_only.h"
// The math half of njin_rl.h (Vector3, Matrix, Quaternion and raymath), for sources
// that must not see raylib.h itself (one that includes <windows.h> would clash with its
// Rectangle/CloseWindow/ShowCursor). raymath.h while raylib is built; Njin's own copy
// (njin_rl_own.inl, its math part only) with NJIN_RL_OWN.
#if defined(NJIN_RL_OWN)
#include <cmath>
#include <cstdlib>
#include <cstring>
#define NJIN_RL_MATH_ONLY
#include "njin_rl_own.inl"
#undef NJIN_RL_MATH_ONLY
#else
#include <raymath.h>
#endif
