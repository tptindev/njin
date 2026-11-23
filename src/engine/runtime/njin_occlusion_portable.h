#pragma once
#include "njin_internal_only.h"
#include <array>
namespace njin {
struct context;
struct model_store;
struct MeshDraw;
std::array<float,4> portable_cull_bounds(context &,const MeshDraw &);
void portable_prepare_occlusion(context &,model_store &);
}
