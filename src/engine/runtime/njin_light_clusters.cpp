#include "njin_light_clusters.h"
#include <algorithm>
#include <cmath>
#include "njin_model.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_window.h"
#include <cstring>
#include "njin_rlmath.h"
#include <stdexcept>
namespace njin {
void light_clusters_build(LightClusters &l,std::span<const light3d_source> lights,const camera3d &cam,vec2 size) {
  const i32 n=i32(lights.size()),index_width=4096;
  // The clusters, in the camera's view space (x right, y up, z forward).
  const bool ortho = cam.projection == camera3d_orthographic;
  const vec3 fwd = normalize(cam.target - cam.position);
  const vec3 right = normalize(cross(fwd, cam.up));
  const vec3 up = cross(right, fwd);
  const f32 aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
  const f32 tan_y = std::tan(cam.fovy * 0.5f * DEG2RAD);
  const f32 half_y = ortho ? std::max(cam.ortho_height, 1e-4f) * 0.5f : 0.0f;
  const f32 near_d = std::max(cam.near_plane, 0.01f);
  // Slices only as deep as the farthest light reaches.
  f32 far_d = near_d * 2.0f;
  for (i32 i = 0; i < n; i++) {
    const light3d_source &src = lights[(usize)i];
    far_d = std::max(far_d, dot(src.position - cam.position, fwd) + src.radius);
  }
  far_d = std::min(far_d, std::max(cam.far_plane, near_d * 2.0f));
  const i32 tx = l.tiles_x, ty = l.tiles_y, sz = l.slices;
  l.tile_px = std::max(std::ceil(size.x / (f32)tx), std::ceil(size.y / (f32)ty));
  // The tiles cover tile_px * tiles pixels, a little past the screen's edge.
  const f32 span_x = l.tile_px * (f32)tx / std::max(size.x, 1.0f);
  const f32 span_y = l.tile_px * (f32)ty / std::max(size.y, 1.0f);
  const f32 log_ratio = std::log(far_d / near_d);
  l.depth = {near_d, far_d, log_ratio, ortho ? 1.0f : 0.0f};
  const auto slice_of = [&](f32 z) {
    const f32 t = ortho ? (z - near_d) / std::max(far_d - near_d, 1e-4f) : std::log(std::max(z, near_d) / near_d) / log_ratio;
    return std::clamp((i32)std::floor(t * (f32)sz), 0, sz - 1);
  };
  const auto slice_near = [&](i32 k) {
    const f32 t = (f32)k / (f32)sz;
    return ortho ? near_d + (far_d - near_d) * t : near_d * std::exp(log_ratio * t);
  };
  // The tile range a box [x0, x1] x [y0, y1] at depths [z0, z1] covers.
  const auto tiles_of = [&](f32 x0, f32 x1, f32 y0, f32 y1, f32 z0, f32 z1, i32 &a0, i32 &a1, i32 &b0, i32 &b1) {
    f32 u0, u1, v0, v1;
    if (ortho) {
      u0 = x0 / (half_y * aspect), u1 = x1 / (half_y * aspect);
      v0 = y0 / half_y, v1 = y1 / half_y;
    } else {
      const f32 zx = tan_y * aspect, zy = tan_y;
      u0 = std::min(x0 / (z0 * zx), x0 / (z1 * zx)), u1 = std::max(x1 / (z0 * zx), x1 / (z1 * zx));
      v0 = std::min(y0 / (z0 * zy), y0 / (z1 * zy)), v1 = std::max(y1 / (z0 * zy), y1 / (z1 * zy));
    }
    // NDC to tiles: the tiles start at the bottom left, as gl_FragCoord does.
    const auto tile = [](f32 ndc, f32 span, i32 count) { return (ndc * 0.5f + 0.5f) / span * (f32)count; };
    a0 = std::clamp((i32)std::floor(tile(u0, span_x, tx)), 0, tx - 1);
    a1 = std::clamp((i32)std::floor(tile(u1, span_x, tx)), 0, tx - 1);
    b0 = std::clamp((i32)std::floor(tile(v0, span_y, ty)), 0, ty - 1);
    b1 = std::clamp((i32)std::floor(tile(v1, span_y, ty)), 0, ty - 1);
    return u1 >= -1.0f && u0 <= 1.0f && v1 >= -1.0f && v0 <= 1.0f;
  };
  const i32 clusters = tx * ty * sz;
  l.counts.assign((usize)clusters, 0);
  // Two passes over the lights: count, then fill.
  const auto each = [&](auto &&fn) {
    for (i32 i = 0; i < n; i++) {
      const light3d_source &src = lights[(usize)i];
      const vec3 rel = src.position - cam.position;
      const f32 x = dot(rel, right), y = dot(rel, up), z = dot(rel, fwd);
      const f32 r = std::max(src.radius, 0.001f);
      if (z + r < near_d || z - r > far_d)
        continue;
      const i32 s0 = slice_of(std::max(z - r, near_d)), s1 = slice_of(std::min(z + r, far_d));
      for (i32 k = s0; k <= s1; k++) {
        // The slice's own depth range keeps the tile box tight.
        const f32 z0 = std::max({z - r, slice_near(k), near_d}), z1 = std::min(z + r, slice_near(k + 1));
        if (z1 < z0)
          continue;
        i32 a0, a1, b0, b1;
        if (!tiles_of(x - r, x + r, y - r, y + r, std::max(z0, near_d), std::max(z1, near_d), a0, a1, b0, b1))
          continue;
        for (i32 b = b0; b <= b1; b++)
          for (i32 a = a0; a <= a1; a++)
            fn((k * ty + b) * tx + a, i);
      }
    }
  };
  each([&](i32 c, i32) { l.counts[(usize)c]++; });
  l.offsets.assign((usize)clusters, 0);
  i32 total = 0;
  for (i32 c = 0; c < clusters; c++) {
    l.offsets[(usize)c] = total;
    total += l.counts[(usize)c];
  }
  l.cluster_data.assign((usize)clusters * 4, 0.0f);
  for (i32 c = 0; c < clusters; c++) {
    l.cluster_data[(usize)c * 4] = (f32)l.offsets[(usize)c];
    l.cluster_data[(usize)c * 4 + 1] = (f32)l.counts[(usize)c];
  }
  const i32 index_rows_needed = std::max((total + index_width - 1) / index_width, 1);
  l.index_data.assign((usize)index_rows_needed * index_width, 0.0f);
  std::fill(l.counts.begin(), l.counts.end(), 0);
  each([&](i32 c, i32 i) { l.index_data[(usize)(l.offsets[(usize)c] + l.counts[(usize)c]++)] = (f32)i; });
}
std::shared_ptr<const RenderClusterData> portable_light_clusters(context &ctx,model_store &store,const std::shared_ptr<const RenderShadowData> &shadows) {
  if(store.portable_frame_light.lights_max<=16) return {};
  auto &grid=store.portable_cluster_grid;
  const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  vec2 size=window_size(ctx);
  const auto target=store.portable_hdr_active?store.portable_hdr:store.portable_target;
  if(target) {const auto &t=resources.targets.at(target.id);size={float(t.width),float(t.height)};}
  light_clusters_build(grid,store.portable_lights,store.portable_camera,size);
  const size_t headers=grid.cluster_data.size()/4,light_base=headers,index_base=light_base+store.portable_lights.size()*4;
  const unsigned width=256,height=unsigned((index_base+(grid.index_data.size()+3)/4+width-1)/width);
  if(height>ctx_ref<window_guard>(ctx).graphics->max_texture_size()) throw std::length_error("njin clustered lighting: light-list texture exceeds device limits");
  std::vector<float> packed(size_t(width)*height*4,0);
  std::copy(grid.cluster_data.begin(),grid.cluster_data.end(),packed.begin());
  for(size_t i=0;i<store.portable_lights.size();++i) {
    const auto &l=store.portable_lights[i];const float half=l.cone*3.14159265359f/360;
    const float values[]{l.position.x,l.position.y,l.position.z,l.radius,l.color.r,l.color.g,l.color.b,l.intensity,
      l.direction.x,l.direction.y,l.direction.z,std::cos(half),std::cos(half*(1-l.softness)),l.kind==light3d_spot?1.f:0.f,shadows?float(shadows->local_base[i]+1):0.f,shadows?float(shadows->ray_channel[i]):0.f};
    std::copy_n(values,16,packed.data()+(light_base+i*4)*4);
  }
  std::copy(grid.index_data.begin(),grid.index_data.end(),packed.begin()+index_base*4);
  std::vector<unsigned char> bytes(packed.size()*sizeof(float));std::memcpy(bytes.data(),packed.data(),bytes.size());
  const RenderImage image{width,height,bytes,RenderImageFormat::RGBA32F};
  if(!store.portable_cluster_texture) store.portable_cluster_texture=renderer_create_texture(ctx,image);
  else renderer_update_texture(ctx,store.portable_cluster_texture,image);
  auto result=std::make_shared<RenderClusterData>();result->texture=store.portable_cluster_texture;
  result->parameters={{{float(grid.tiles_x),float(grid.tiles_y),float(grid.slices),grid.tile_px},
    {grid.depth.x,grid.depth.y,grid.depth.z,grid.depth.w},{float(width),float(height),float(light_base),float(index_base)},
    {size.x,size.y,1,float(store.portable_lights.size())}}};
  const auto view=MatrixToFloatV(store.portable_view),vp=MatrixToFloatV(store.portable_view_projection);
  std::copy_n(view.v,16,result->view.begin());std::copy_n(vp.v,16,result->view_projection.begin());
  return result;
}
}
