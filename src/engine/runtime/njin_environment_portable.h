#pragma once
#include "njin_internal_only.h"
namespace njin {
struct context;
struct model_store;
// CPU preparation never creates a raylib context or touches OpenGL state.
// Prepared textures belong to RendererResources; the model store caches IDs.
void portable_environment_update(context &,model_store &);
void portable_environment_sky(context &,model_store &);
}
