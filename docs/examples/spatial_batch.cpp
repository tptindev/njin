#include <njin.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
using namespace njin;

// 40.000 cây trên cánh đồng 2000 x 2000, lưới 50 x 50 ô (40 đơn vị một ô).
// Mặt phẳng của lưới là x, z của thế giới 3D.
constexpr i32 tree_count = 40000;
constexpr i32 floats = 16; // vị trí, màu, góc xoay, tỉ lệ x y z
const batch_grid2d grid{.origin = {-1000.0f, -1000.0f}, .cell_size = {40.0f, 40.0f}, .cols = 50, .rows = 50};

instance_buffer_handle trees;
std::vector<u32> offsets;              // offsets[c]: cây đầu tiên của ô c trong bộ đệm
std::vector<u8> near_cells, far_cells; // mỗi frame: ô vẽ hình mịn, ô vẽ hình ít mặt

void load(context &ctx) {
  rng &r = random(ctx);
  std::vector<f32> unsorted(tree_count * floats, 0.0f);
  std::vector<i32> cell(tree_count);
  offsets.assign(grid.count() + 1, 0);
  for (i32 i = 0; i < tree_count; i++) {
    f32 *t = &unsorted[i * floats];
    t[0] = r.range(-1000.0f, 1000.0f); // x
    t[2] = r.range(-1000.0f, 1000.0f); // z
    t[5] = r.range(0.35f, 0.6f);       // màu: xanh lá
    t[4] = t[6] = 0.2f;
    t[7] = 1.0f;
    t[12] = t[14] = 0.5f;          // thân bán kính 0.5
    t[13] = r.range(4.0f, 8.0f);   // cao 4 đến 8
    cell[i] = grid.cell_at({t[0], t[2]});
    offsets[cell[i] + 1]++;
  }
  // Xếp cây theo ô: số cây mỗi ô cộng dồn thành offsets, rồi chép từng cây vào phần của ô nó.
  for (i32 c = 0; c < grid.count(); c++)
    offsets[c + 1] += offsets[c];
  std::vector<u32> next(offsets.begin(), offsets.end() - 1);
  std::vector<f32> sorted(unsorted.size());
  for (i32 i = 0; i < tree_count; i++)
    std::copy_n(&unsorted[i * floats], floats, &sorted[next[cell[i]]++ * floats]);

  trees = instance_buffer_create(ctx, floats);
  instance_buffer_upload(ctx, trees, sorted.data(), tree_count);
  near_cells.resize(grid.count());
  far_cells.resize(grid.count());
}

void render(context &ctx) {
  // Camera bay vòng quanh cánh đồng, nhìn chếch xuống.
  const f32 a = elapsed(ctx) * 0.1f;
  const vec3 target{std::cos(a) * 500.0f, 0.0f, std::sin(a) * 500.0f};
  const camera3d camera{.position = {target.x - 120.0f, 80.0f, target.z - 120.0f}, .target = target};

  // Phần mặt đất camera này thấy: một hình chữ nhật đủ rộng để bao trọn nó.
  const batch_view2d view{.lo = {target.x - 400.0f, target.z - 400.0f},
                          .hi = {target.x + 400.0f, target.z + 400.0f},
                          .focus = {camera.position.x, camera.position.z},
                          .overhang = 0.5f}; // thân cây nhô khỏi ô xa nhất bằng bán kính của nó
  for (i32 c = 0; c < grid.count(); c++) {
    const bool seen = batch_cell_visible(grid, view, c);
    const bool close = batch_cell_detailed(grid, view, c, 150.0f);
    near_cells[c] = seen && close;
    far_cells[c] = seen && !close;
  }

  begin_3d(ctx, camera);
  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {2000.0f, 2000.0f}, {0.45f, 0.5f, 0.3f, 1.0f});
  for (const auto &[from, to] : batch_instance_ranges(offsets, near_cells))
    draw_instanced3d(ctx, mesh3d_cylinder, trees, from, to - from);
  for (const auto &[from, to] : batch_instance_ranges(offsets, far_cells))
    draw_instanced3d(ctx, mesh3d_cylinder_low, trees, from, to - from);
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc spatial_batch_module() { return {.name = "spatial_batch", .setup = setup}; }
