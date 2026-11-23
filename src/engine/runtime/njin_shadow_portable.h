#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
namespace njin {
struct context;
struct model_store;
std::shared_ptr<const RenderShadowData> portable_render_shadows(context &,model_store &);
}
