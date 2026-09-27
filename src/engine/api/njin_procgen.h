#pragma once
#include "_tilemap.h"
#include "_types.h"
#include <functional>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace njin {
/// @addtogroup grp_procgen
/// @{

// ---------------------------------------------------------------------------
// Lưới ô tạm để sinh bản đồ
// ---------------------------------------------------------------------------

/// Một lưới ô hình chữ nhật, kích thước cố định, để sinh và sửa bản đồ trước khi đưa vào
/// njin::tilemap. Mỗi ô giữ số thứ tự ô trong tileset, hoặc -1 là trống.
///
/// Các bộ sinh (generate_topdown(), generate_platformer(), wfc_generate()) và các luật
/// (grid_majority(), grid_border()...) đều làm việc trên lưới này; xong thì
/// tilemap_from_grid() đặt nó vào tilemap. Lưới chỉ là dữ liệu: không cần cửa sổ, không cần
/// njin::njin_ctx, nên sinh được ở bất kỳ đâu, kể cả trước khi mở cửa sổ.
struct tile_grid {
  i32 width = 0;           ///< Số cột.
  i32 height = 0;          ///< Số hàng.
  std::vector<i32> cells{}; ///< Các ô theo hàng, từ trên xuống. `cells[y * width + x]`.

  /// Ô có nằm trong lưới không. @param x Cột. @param y Hàng. @return `true` nếu nằm trong.
  bool inside(i32 x, i32 y) const { return x >= 0 && y >= 0 && x < width && y < height; }
  /// Giá trị một ô. @param x Cột. @param y Hàng. @return Số thứ tự ô, -1 nếu trống hoặc ngoài lưới.
  i32 get(i32 x, i32 y) const { return inside(x, y) ? cells[(usize)(y * width + x)] : -1; }
  /// Đặt một ô; ngoài lưới thì bỏ qua. @param x Cột. @param y Hàng. @param tile Số thứ tự ô, -1 là trống.
  void set(i32 x, i32 y, i32 tile) {
    if (inside(x, y))
      cells[(usize)(y * width + x)] = tile;
  }
};

/// Tạo một lưới `width` x `height`, mọi ô là `fill`.
/// @param width Số cột. @param height Số hàng. @param fill Giá trị ban đầu, mặc định -1 (trống).
/// @return Lưới mới. Kích thước âm được coi là 0.
tile_grid tile_grid_make(i32 width, i32 height, i32 fill = -1);

/// Dựng lưới từ bản đồ chữ, cùng quy tắc với tilemap_from_text(): mỗi ký tự một ô, `legend` cho
/// biết ký tự nào là ô nào. Ký tự không có trong `legend` thành -1. Lưới rộng bằng dòng dài nhất.
/// Dùng để viết **mẫu** cho wfc_learn(), hoặc để chỉnh tay một lưới rồi cho luật chạy tiếp.
/// @param text Bản đồ chữ. @param legend Bảng ký tự.
/// @return Lưới.
tile_grid tile_grid_from_text(std::string_view text, std::initializer_list<tile_key> legend);

/// Đặt lưới vào tilemap, góc trên trái ở ô `origin`. Ô -1 của lưới **không** xóa ô đang có (như
/// bản đồ chữ), nên chồng được nhiều lưới; muốn sinh lại từ đầu thì gọi tilemap_clear() trước.
/// @param map Tilemap. @param grid Lưới. @param origin Ô của góc trên trái.
void tilemap_from_grid(tilemap &map, const tile_grid &grid, cell origin = {});

/// Số ô có giá trị `tile`. @param grid Lưới. @param tile Số thứ tự ô (có thể là -1).
/// @return Số ô.
i32 grid_count(const tile_grid &grid, i32 tile);

// ---------------------------------------------------------------------------
// Nhiễu
// ---------------------------------------------------------------------------

/// Loại nhiễu cơ bản.
enum noise_type {
  noise_perlin, ///< Nhiễu gradient (Perlin): mượt, tròn trịa. Mặc định, hợp cho địa hình.
  noise_value,  ///< Nhiễu giá trị: rẻ hơn, hơi vuông vức. Hợp cho vết, đốm.
};

/// Cách chồng nhiều lớp nhiễu (octave) lên nhau.
enum noise_fractal {
  fractal_none,   ///< Một lớp. Hình to, trơn.
  fractal_fbm,    ///< Cộng các lớp càng lúc càng nhỏ và nhạt: địa hình tự nhiên. Mặc định.
  fractal_ridged, ///< Gờ nhọn: dãy núi, sống đá, dòng sông khi lấy vùng thấp.
  fractal_billow, ///< Gợn tròn phồng lên: mây, đồi thấp, đá cuội.
};

/// Tham số của một nguồn nhiễu. Cùng tham số và cùng `seed` thì cùng kết quả, trên mọi máy.
///
/// Từ đơn giản đến phức tạp: chỉ đặt `seed` và `frequency` là đủ dùng; thêm `octaves`, `gain`,
/// `lacunarity` để có chi tiết nhỏ; đổi `fractal` để đổi dáng; đặt `warp` để các đường viền cong
/// queo tự nhiên thay vì tròn đều.
struct noise_desc {
  u32 seed = 1;                          ///< Hạt giống. Đổi số này là có bản đồ khác.
  noise_type type = noise_perlin;        ///< Loại nhiễu.
  f32 frequency = 0.05f;                 ///< Tần số theo ô. Nhỏ: vùng to; lớn: vùng vụn. 0.02 đến 0.1 là thường.
  noise_fractal fractal = fractal_fbm;   ///< Cách chồng lớp.
  i32 octaves = 4;                       ///< Số lớp (1 đến 8). Nhiều lớp: mép chi tiết hơn, tốn hơn.
  f32 lacunarity = 2.0f;                 ///< Mỗi lớp sau có tần số nhân với số này.
  f32 gain = 0.5f;                       ///< Mỗi lớp sau có độ mạnh nhân với số này. Lớn: gồ ghề.
  f32 warp = 0.0f;                       ///< Uốn cong toạ độ, tính bằng ô. 0 là tắt, 4 đến 20 cho viền tự nhiên.
  f32 warp_frequency = 0.03f;            ///< Tần số của nhiễu dùng để uốn.
};

/// Giá trị nhiễu tại một điểm, từ 0 đến 1.
/// @param desc Tham số. @param x Toạ độ, thường là cột của ô. @param y Toạ độ, thường là hàng.
/// @return Số trong `[0, 1]`. Với `fractal_fbm` giá trị dồn quanh 0.5, hiếm khi ra ngoài 0.2 đến 0.8;
/// generate_topdown() tự kéo giãn cho đủ 0 đến 1 (xem njin::topdown_gen_desc::normalize).
f32 noise_2d(const noise_desc &desc, f32 x, f32 y);

/// Nhiễu một chiều, từ 0 đến 1: đường mặt đất của platformer, độ cao theo cột.
/// @param desc Tham số. @param x Toạ độ. @return Số trong `[0, 1]`.
f32 noise_1d(const noise_desc &desc, f32 x);

// ---------------------------------------------------------------------------
// Luật: làm bản đồ trông tự nhiên
// ---------------------------------------------------------------------------

/// Các phía của một ô, cộng lại được: `side_up | side_down`.
enum grid_side : u8 {
  side_up = 1,    ///< Ô phía trên.
  side_down = 2,  ///< Ô phía dưới.
  side_left = 4,  ///< Ô bên trái.
  side_right = 8, ///< Ô bên phải.
  side_all = 15,  ///< Cả bốn phía.
};

/// Làm mượt: mỗi ô thành loại ô xuất hiện nhiều nhất trong ô 3 x 3 quanh nó, nếu loại đó có ít nhất
/// `threshold` ô. Xoá các chấm lẻ và răng cưa mà nhiễu để lại, với mọi loại ô cùng lúc.
/// @param grid Lưới. @param iterations Số lần làm. @param threshold Trong 9 ô, cần bao nhiêu (5 là đa số).
void grid_majority(tile_grid &grid, i32 iterations = 1, i32 threshold = 5);

/// Làm mượt hai loại ô kiểu hang động (automat tế bào): một ô thành `solid` nếu ít nhất `threshold`
/// trong 8 ô quanh nó là đặc, còn không thì thành `open`. Ô đặc là mọi ô khác `open`, kể cả ngoài lưới, nên
/// hang không thủng ra mép. Chỉ đổi những ô đang là `solid` hoặc `open`.
/// @param grid Lưới. @param solid Ô đặc (vách). @param open Ô trống (lòng hang, thường -1).
/// @param iterations Số lần làm, 2 đến 5 là thường. @param threshold 5 là luật hang cổ điển.
void grid_smooth(tile_grid &grid, i32 solid, i32 open, i32 iterations = 3, i32 threshold = 5);

/// Xoá các vùng nhỏ của một loại ô: mỗi vùng liền nhau (theo 4 hướng) của `tile` có ít hơn
/// `min_size` ô thì thành `replace`. Bỏ ao tí xíu, đảo một ô, mảng đá lạc.
/// @param grid Lưới. @param tile Loại ô. @param min_size Kích thước tối thiểu. @param replace Ô thay vào.
/// @return Số vùng đã xoá.
i32 grid_remove_small(tile_grid &grid, i32 tile, i32 min_size, i32 replace);

/// Như grid_remove_small() cho **mọi** loại ô: vùng nhỏ hơn `min_size` hoà vào loại ô bao quanh nó
/// nhiều nhất. @param grid Lưới. @param min_size Kích thước tối thiểu. @return Số vùng đã hoà.
i32 grid_merge_small(tile_grid &grid, i32 min_size);

/// Giữ mọi chỗ đi được liền một khối: trong các ô thuộc `walkable`, chỉ vùng liền nhau (4 hướng) lớn
/// nhất được giữ, các vùng khác thành `replace`. Dùng cho top-down để không có góc nào bị cô lập.
/// @param grid Lưới. @param walkable Các loại ô đi được. @param replace Ô thay cho vùng bị bỏ.
/// @return Số ô đã thay.
i32 grid_keep_largest(tile_grid &grid, std::initializer_list<i32> walkable, i32 replace);

/// Viền: ô `tile` nằm cách `touching` không quá `distance` ô (theo các phía trong `sides`) thì thành
/// `replace`. Bãi cát giữa nước và cỏ, cỏ trên mặt đất (chỉ phía trên chạm ô trống), rìa đá.
/// @param grid Lưới. @param tile Ô bị đổi. @param touching Ô gây ra viền. @param replace Ô viền.
/// @param sides Các phía được xét. @param distance Độ dày viền, tính bằng ô.
/// @return Số ô đã đổi.
i32 grid_border(tile_grid &grid, i32 tile, i32 touching, i32 replace, u8 sides = side_all, i32 distance = 1);

/// Rải ngẫu nhiên: mỗi ô `on` có xác suất `chance` thành `place`, và hai ô được rải cách nhau ít nhất
/// `min_distance` ô (0 là không giới hạn). Hoa trên cỏ, đá trên đất, cây trong rừng.
/// @param grid Lưới. @param on Ô được rải lên. @param place Ô rải. @param chance Xác suất, 0 đến 1.
/// @param seed Hạt giống. @param min_distance Khoảng cách tối thiểu (Chebyshev).
/// @return Số ô đã rải.
i32 grid_scatter(tile_grid &grid, i32 on, i32 place, f32 chance, u32 seed, i32 min_distance = 0);

/// Như bản trên, nhưng chỗ nào được rải do hàm `where` quyết định: bụi cây chỉ ở ô trống **ngay trên** mặt
/// đất, đuốc chỉ trên tường...
/// @code
/// njin::grid_scatter(grid, 14, 0.2f, 7, [](const njin::tile_grid &g, njin::i32 x, njin::i32 y) {
///   return g.get(x, y) == -1 && g.get(x, y + 1) == 0; // trống, và ngay dưới là cỏ
/// });
/// @endcode
/// @param grid Lưới. @param place Ô rải. @param chance Xác suất. @param seed Hạt giống.
/// @param where Ô `(x, y)` có được rải không. @param min_distance Khoảng cách tối thiểu.
/// @return Số ô đã rải.
i32 grid_scatter(tile_grid &grid, i32 place, f32 chance, u32 seed,
                 const std::function<bool(const tile_grid &, i32, i32)> &where, i32 min_distance = 0);

// ---------------------------------------------------------------------------
// Bo góc tự nhiên: autotile
// ---------------------------------------------------------------------------

/// Bộ ô dùng để nối một loại địa hình với chính nó.
enum autotile_layout {
  /// 16 ô, nhìn bốn ô kề (trên, phải, dưới, trái). Góc **lồi** (hai phía liền nhau để hở) tròn được;
  /// góc **lõm** (ô chéo hở) thì không. Đủ cho tường và đường đi.
  autotile_edges,
  /// 47 ô, nhìn cả tám ô kề. Có cả góc lồi lẫn góc lõm: đất, nước, mặt đá liền tự nhiên. Mặc định.
  autotile_blob,
};

/// Các ô kề, cộng lại thành mặt nạ (mask) của một ô. Ô chéo chỉ tính khi **cả hai** ô kề thẳng cạnh nó cũng
/// cùng loại, nên chỉ còn 47 kiểu mặt nạ (chứ không phải 256).
enum autotile_neighbor : u8 {
  neighbor_up = 1,          ///< Ô phía trên.
  neighbor_up_right = 2,    ///< Ô chéo phải trên.
  neighbor_right = 4,       ///< Ô bên phải.
  neighbor_down_right = 8,  ///< Ô chéo phải dưới.
  neighbor_down = 16,       ///< Ô phía dưới.
  neighbor_down_left = 32,  ///< Ô chéo trái dưới.
  neighbor_left = 64,       ///< Ô bên trái.
  neighbor_up_left = 128,   ///< Ô chéo trái trên.
};

/// Số ô của một bộ. @param layout Kiểu bộ. @return 16 hoặc 47.
i32 autotile_count(autotile_layout layout);

/// Số thứ tự ô (trong bộ, từ 0) ứng với một mặt nạ. Với `autotile_edges`: `up + 2 * right + 4 * down +
/// 8 * left`, mỗi số là 1 nếu ô kề đó cùng loại (bỏ qua ô chéo); ô tách rời hoàn toàn là 0, ô ở giữa
/// vùng là 15. Với `autotile_blob`: thứ hạng của mặt nạ (đã bỏ ô chéo thừa) trong 47 mặt nạ hợp lệ xếp
/// tăng dần; ô tách rời là 0, ô ở giữa vùng là 46. Vẽ bộ ô theo đúng thứ tự này (trang Sinh bản đồ có bảng).
/// @param mask Các bit `neighbor_*` của ô kề cùng loại. @param layout Kiểu bộ.
/// @return Số thứ tự trong `[0, autotile_count())`.
i32 autotile_index(u8 mask, autotile_layout layout = autotile_blob);

/// Một luật bo góc: những ô nào là cùng một địa hình và vẽ bằng bộ ô nào.
struct autotile_rule {
  /// Các ô của lưới mà luật này thay bằng ô của bộ (ví dụ cỏ và đất cùng là "mặt đất").
  std::vector<i32> tiles{};
  /// Ô khác vẫn được tính là nối liền (không tạo mép), nhưng không bị thay: đá dưới đất, cửa trên tường.
  std::vector<i32> joins{};
  i32 base = 0;                        ///< Số thứ tự của ô đầu tiên của bộ trong tileset. Bộ là các ô liền nhau.
  autotile_layout layout = autotile_blob; ///< Kiểu bộ.
  /// Những phía mà **ngoài lưới** coi như cùng loại (các bit njin::grid_side). Mặc định cả bốn: mép bản
  /// đồ không bo góc, đất chạy tiếp ra ngoài. Bỏ `side_up` nếu mặt đất phải có viền phía trên.
  u8 outside = side_all;
};

/// Thay mọi ô thuộc `rules[i].tiles` bằng ô của bộ tương ứng, chọn theo các ô kề: chỗ nào địa hình hở ra
/// thì ô có mép, hai mép gặp nhau thì có góc tròn (lồi), chỗ địa hình lõm vào thì có góc lõm. Mọi luật đọc
/// từ **cùng một bản chụp** lưới trước khi đổi, nên thứ tự các luật không ảnh hưởng và số thứ tự ô mới không
/// bị nhầm với ô cũ. Một ô khớp nhiều luật thì luật đầu thắng.
///
/// Gọi **sau cùng**, sau mọi luật khác (grid_border(), grid_scatter()...): vì ô đã đổi thành ô của bộ.
/// @code
/// // cỏ và đất là một địa hình, đá nối liền với nó; ngoài lưới thì trời phía trên
/// njin::grid_autotile(grid, {{.tiles = {grass, dirt}, .joins = {stone}, .base = 0, .outside = njin::side_all & ~njin::side_up},
///                            {.tiles = {stone}, .joins = {grass, dirt}, .base = 47}});
/// @endcode
/// @param grid Lưới. @param rules Các luật.
/// @return Số ô đã thay.
i32 grid_autotile(tile_grid &grid, const std::vector<autotile_rule> &rules);

// ---------------------------------------------------------------------------
// Top-down: vùng đất theo độ cao và độ ẩm
// ---------------------------------------------------------------------------

/// Một vùng đất của bản đồ top-down: ô nào đặt ở chỗ có độ cao và độ ẩm nào. Xét theo thứ tự, vùng
/// đầu tiên thoả `height <= max_height` và `moisture <= max_moisture` được chọn.
struct biome {
  i32 tile;                 ///< Ô đặt cho vùng này.
  f32 max_height = 1.0f;    ///< Độ cao tối đa (0 đến 1).
  f32 max_moisture = 1.0f;  ///< Độ ẩm tối đa (0 đến 1). Để 1 nếu không dùng độ ẩm.
};

/// Tham số sinh bản đồ top-down.
///
/// Mặc định (chỉ cần `biomes`): một độ cao từ nhiễu, chia vùng theo ngưỡng, làm mượt một lần, bỏ vùng
/// dưới 6 ô. Thêm dần: độ ẩm để có sa mạc và rừng cùng độ cao, `island` cho hình hòn đảo, `walkable`
/// để mọi chỗ đi được liền một khối, `border_tile` cho tường bao.
struct topdown_gen_desc {
  i32 width = 64;                 ///< Số cột.
  i32 height = 64;                ///< Số hàng.
  noise_desc height_noise{};      ///< Nhiễu độ cao.
  /// Nhiễu độ ẩm. Chỉ dùng khi có vùng đặt `max_moisture` dưới 1.
  noise_desc moisture_noise{.seed = 7919, .frequency = 0.03f, .octaves = 3};
  std::vector<biome> biomes{};    ///< Các vùng, xét theo thứ tự. Ô không khớp vùng nào dùng vùng cuối.
  f32 island = 0.0f;              ///< 0 là tắt. 0.5 đến 1: độ cao giảm dần về mép, thành hòn đảo giữa nước.
  /// Kéo giãn độ cao (và độ ẩm) để ô thấp nhất của bản đồ là 0 và ô cao nhất là 1. Bật (mặc định) thì
  /// ngưỡng của `biomes` đọc như **phần trăm của bản đồ**: `max_height = 0.3` là khoảng 30% thấp nhất.
  /// Tắt thì ngưỡng so thẳng với nhiễu, mà nhiễu fBm hiếm khi ra ngoài khoảng 0.2 đến 0.8.
  bool normalize = true;
  i32 smooth = 1;                 ///< Số lần grid_majority(). 0 là tắt.
  i32 min_region = 6;             ///< grid_merge_small(): vùng nhỏ hơn số ô này bị hoà vào xung quanh. 0 là tắt.
  /// Các ô đi được. Có thì chỉ giữ vùng đi được liền nhau lớn nhất (grid_keep_largest()), và điểm xuất
  /// phát nằm trong vùng đó.
  std::vector<i32> walkable{};
  i32 blocked_tile = -1;          ///< Ô thay cho vùng đi được bị bỏ, khi có `walkable`.
  i32 border_tile = -1;           ///< Ô cho vòng tường bao quanh bản đồ. -1 là không có.
};

/// Kết quả của generate_topdown().
struct topdown_gen_result {
  tile_grid grid{};               ///< Bản đồ.
  std::vector<f32> heights{};     ///< Độ cao (0 đến 1) của từng ô, cùng thứ tự với `grid.cells`. Dùng cho luật riêng.
  cell spawn{};                   ///< Điểm xuất phát: ô đi được gần tâm nhất (tâm bản đồ nếu không có `walkable`).
};

/// Sinh bản đồ top-down từ nhiễu. @param desc Tham số. @return Bản đồ, độ cao và điểm xuất phát.
topdown_gen_result generate_topdown(const topdown_gen_desc &desc);

// ---------------------------------------------------------------------------
// Platformer: mặt đất, hang, hố, bục
// ---------------------------------------------------------------------------

/// Tham số sinh màn platformer, nhìn ngang, đi từ trái sang phải.
///
/// Mặt đất là một đường nhiễu theo cột. Luật giữ cho màn **chơi được**: hai cột cạnh nhau chênh nhau
/// không quá `max_step` ô (đặt không quá độ cao nhảy của nhân vật), hang không đục lên sát mặt đất,
/// hố không rộng quá `pit_max` ô, và hai đầu màn luôn bằng phẳng.
struct platformer_gen_desc {
  i32 width = 120;                  ///< Số cột.
  i32 height = 30;                  ///< Số hàng.
  /// Nhiễu của mặt đất, theo cột.
  noise_desc surface_noise{.seed = 1, .frequency = 0.04f, .octaves = 3};
  i32 ground_min = 12;              ///< Hàng cao nhất mặt đất được lên (hàng tính từ trên xuống).
  i32 ground_max = 24;              ///< Hàng thấp nhất mặt đất được xuống.
  i32 max_step = 2;                 ///< Chênh lệch tối đa giữa hai cột cạnh nhau, tính bằng ô.
  i32 surface_tile = 0;             ///< Ô mặt đất (cỏ).
  i32 dirt_tile = 1;                ///< Ô dưới mặt đất.
  i32 deep_tile = -1;               ///< Ô ở sâu (đá). -1 là dùng `dirt_tile`.
  i32 deep_depth = 6;               ///< Từ độ sâu này (ô tính từ mặt đất) trở xuống là `deep_tile`.
  /// Ô dốc cao bên phải và cao bên trái (njin::tile_slope_r, njin::tile_slope_l), đặt ở chỗ mặt đất
  /// lên hoặc xuống đúng một ô. -1 là không dùng dốc. Nhớ đặt hình cho chúng bằng tilemap_set_shape().
  i32 slope_r_tile = -1;
  i32 slope_l_tile = -1;            ///< Xem `slope_r_tile`.
  /// Nhiễu của hang.
  noise_desc cave_noise{.seed = 3, .frequency = 0.09f, .octaves = 2};
  /// Tỉ lệ diện tích ngầm được đục thành hang, 0 (tắt) đến 0.9. Là tỉ lệ **thật** (lấy theo thứ hạng của nhiễu
  /// trên đúng những ô được phép): 0.2 là đục chừng 20% số ô nằm dưới `cave_margin`, bất kể nhiễu có dải giá
  /// trị thế nào. Sau đó hang được bo tròn bằng hai lượt automat tế bào và bỏ hốc dưới 8 ô, nên số thật lệch
  /// đôi chút. 0.15 đến 0.3 là vừa; hơn nữa là hầm hố.
  f32 caves = 0.0f;
  i32 cave_margin = 4;              ///< Số hàng ngay dưới mặt đất không bị đục hang.
  f32 pit_chance = 0.0f;            ///< Xác suất bắt đầu một hố ở mỗi cột. 0 là không có hố.
  i32 pit_min = 1;                  ///< Hố hẹp nhất, tính bằng ô.
  /// Hố rộng nhất. Giữ không quá tầm nhảy xa của nhân vật: njin::platformer_body mặc định chạy 110 px/s và
  /// bay chừng 0,54 giây, tức khoảng 59 px, nên hố 2 ô (32 px) còn dư; hố 3 ô (48 px, cộng bề rộng nhân vật)
  /// thì phải cất cánh sát mép. Tăng `run_speed` hoặc `jump_speed` của nhân vật trước khi tăng số này.
  i32 pit_max = 2;
  i32 safe_columns = 6;             ///< Số cột bằng phẳng, không hố, ở mỗi đầu màn (chỗ xuất phát và đích).
  f32 platform_chance = 0.0f;       ///< Xác suất đặt một bục lơ lửng ở mỗi cột. 0 là không có bục.
  i32 platform_tile = -1;           ///< Ô của bục (thường là bục một chiều, njin::tile_one_way).
  i32 platform_min = 3;             ///< Bục ngắn nhất.
  i32 platform_max = 5;             ///< Bục dài nhất.
  i32 platform_height = 3;          ///< Bục cao hơn mặt đất bấy nhiêu ô. Giữ không quá độ cao nhảy.
};

/// Kết quả của generate_platformer().
struct platformer_gen_result {
  tile_grid grid{};                 ///< Màn chơi.
  std::vector<i32> surface{};       ///< Hàng của mặt đất ở từng cột, -1 ở cột là hố.
  cell spawn{};                     ///< Ô trống ngay trên mặt đất, ở đầu trái: chỗ đặt nhân vật.
  cell goal{};                      ///< Ô trống ngay trên mặt đất, ở đầu phải: chỗ đặt đích.
};

/// Sinh màn platformer từ nhiễu. @param desc Tham số. @return Màn, mặt đất, điểm xuất phát và đích.
platformer_gen_result generate_platformer(const platformer_gen_desc &desc);

// ---------------------------------------------------------------------------
// Wave Function Collapse
// ---------------------------------------------------------------------------

/// Hướng từ một ô sang ô bên cạnh, dùng cho luật kề của WFC.
enum wfc_dir { wfc_right, wfc_down, wfc_left, wfc_up };

/// Luật của Wave Function Collapse: có những ô nào, mỗi ô hay gặp đến đâu, và ô nào được đứng cạnh ô
/// nào theo từng hướng. Dựng bằng wfc_learn() từ một mẫu, hoặc bằng tay với wfc_add_tile() và wfc_allow().
struct wfc_rules {
  std::vector<i32> tiles{};             ///< Các ô (số thứ tự trong tileset; -1 là ô trống cũng là một ô).
  std::vector<f32> weights{};           ///< Độ hay gặp của từng ô. Lớn: xuất hiện nhiều.
  /// Luật kề, dạng bit: `allow[(i * 4 + dir) * words + j / 64]` có bit `j % 64` nếu ô thứ `j` được đứng
  /// ở hướng `dir` của ô thứ `i`. Dùng wfc_allow() thay vì sửa trực tiếp.
  std::vector<u64> allow{};
  i32 words = 0;                        ///< Số từ 64 bit cho mỗi tập ô.
};

/// Thêm một ô vào luật (hoặc đổi độ hay gặp nếu đã có). @param rules Luật. @param tile Ô.
/// @param weight Độ hay gặp. @return Chỉ số của ô trong `rules.tiles`.
i32 wfc_add_tile(wfc_rules &rules, i32 tile, f32 weight = 1.0f);

/// Cho phép `b` đứng ở hướng `dir` của `a`, và (đối xứng) `a` đứng ở hướng ngược lại của `b`. Ô chưa có
/// được thêm vào với độ hay gặp 1.
/// @param rules Luật. @param a Ô thứ nhất. @param dir Hướng từ `a` sang `b`. @param b Ô thứ hai.
void wfc_allow(wfc_rules &rules, i32 a, wfc_dir dir, i32 b);

/// Học luật từ một mẫu: mọi cặp ô cạnh nhau trong mẫu thành luật kề, số lần xuất hiện thành độ hay gặp.
/// Viết mẫu bằng tile_grid_from_text(). Mẫu nhỏ và đa dạng cho kết quả tốt; mẫu thiếu cặp nào thì cặp đó
/// không bao giờ xuất hiện.
/// @param sample Mẫu. @param periodic `true`: coi mẫu lặp lại, mép phải nối mép trái, mép dưới nối mép trên.
/// @return Luật.
wfc_rules wfc_learn(const tile_grid &sample, bool periodic = false);

/// Tham số của wfc_generate().
struct wfc_desc {
  i32 width = 32;         ///< Số cột.
  i32 height = 32;        ///< Số hàng.
  u32 seed = 1;           ///< Hạt giống.
  i32 attempts = 20;      ///< Số lần thử lại khi đi vào ngõ cụt (mâu thuẫn), mỗi lần một dãy ngẫu nhiên mới.
  bool periodic = false;  ///< Kết quả lặp lại được: mép phải khớp mép trái, mép dưới khớp mép trên.
  /// Ràng buộc: ô `tile` có được đặt ở `(x, y)` không. Để trống là mọi chỗ đều được. Dùng để cố định
  /// hàng đáy là đất, hàng trên cùng là trời, vòng ngoài là tường...
  std::function<bool(i32 x, i32 y, i32 tile)> allowed{};
};

/// Sinh một lưới thoả mọi luật kề, bằng Wave Function Collapse (mô hình ô, không quay lui: gặp mâu thuẫn
/// thì làm lại từ đầu với dãy ngẫu nhiên khác, tối đa `attempts` lần).
/// @param rules Luật. @param desc Tham số. @param out Nhận lưới nếu thành công; không đổi nếu thất bại.
/// @return `true` nếu thành công. `false` khi luật quá chặt, ràng buộc mâu thuẫn, hoặc hết số lần thử.
bool wfc_generate(const wfc_rules &rules, const wfc_desc &desc, tile_grid &out);

/// @}
} // namespace njin
