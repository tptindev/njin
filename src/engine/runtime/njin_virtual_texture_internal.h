#pragma once
#include "njin_internal_only.h"
#include "_types.h"
namespace njin {
struct context;
struct VirtualTexture;
void virtual_texture_forget(context &,texture_handle);
}
