#pragma once
#include "njin_internal_only.h"
#include "njin_post3d.h"
namespace njin {
struct context;
struct model_store;
void portable_post_validate(const post3d &);
void portable_post_resolve(context &,model_store &);
}
