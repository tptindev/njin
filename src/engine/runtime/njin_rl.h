#pragma once
#include "njin_internal_only.h"
// The raylib vocabulary of Njin's shared runtime code (Vector3, Matrix, Mesh, Model,
// raymath...), from one place (docs/renderer-legacy-removal.md, step 3E). While the
// legacy renderer is built it is raylib's own headers; with NJIN_RL_OWN (a build
// without raylib) it is njin_rl_own.inl, the same types and arithmetic as Njin's,
// checked bit for bit against raylib by the rl_own test.
#if defined(NJIN_RL_OWN)
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "njin_rl_own.inl"
#else
#include <raylib.h>
#include <raymath.h>
#endif
