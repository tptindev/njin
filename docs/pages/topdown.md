# Làm game top-down {#topdown}

Nhân vật đi 8 hướng, quái đuổi theo qua các lối đi, cây che nhân vật đúng chỗ. Game mẫu
`njin_topdown` (@ref samples) dùng đúng những thứ này. Chưa làm gì bao giờ? Bắt đầu với @ref first_walk : một nhân
vật đi được trong 50 dòng.

@image html topdown.gif "Game mẫu njin_topdown: nhân vật đi và lướt, con slime hồng đuổi theo, hiện nhắc phím tương tác khi lại gần biển báo"

@include topdown_nav.cpp

## Nhân vật: njin::topdown_body

Gắn njin::topdown_body cạnh transform và một collider (hộp hoặc tròn). Engine di chuyển nó trong
`phase_fixed_update` bằng collision_move(), nên nó trượt dọc tường thay vì dính vào.

- Ghi hướng đi vào `body.input.move`. Độ dài lớn hơn 1 được đưa về 1: **đi chéo không nhanh hơn**
  đi thẳng, kể cả khi bạn cộng hai axis.
- `speed`, `accel`, `decel` cho cảm giác trượt hay bám.
- **Lướt**: đặt `dash_speed` lớn hơn 0 rồi ghi `input.dash = true` (một yêu cầu, tự xóa).
  `dash_time`, `dash_cooldown` chỉnh độ dài và hồi. njin::body_dashed cho tiếng và bụi.
- `facing` là hướng nhìn gần nhất khác 0; dùng để chọn hình và để vung kiếm về đúng phía.

njin::topdown_input_map đọc hai axis và một action giúp bạn.

## Sắp xếp theo Y

Một cái cây phải che nhân vật đứng sau nó, và bị nhân vật che khi đứng trước. Bật cho một lớp vẽ:

@code
njin::draw_set_y_sort(ctx, 5, true); // mọi sprite lớp 5: y lớn hơn vẽ sau
@endcode

`y` là `transform.pos.y + sprite::sort_offset`. Đặt `sprite::origin` ở chân (`{0.5, 1}`) thì không cần
`sort_offset`. Tilemap trong lớp đó vẫn vẽ trước mọi sprite, làm nền. Đặt **cây, nhà, nhân vật, quái,
rương** cùng một lớp, và đặt collider ở gốc thân để va chạm khớp với chỗ đứng.

## Tìm đường

njin::nav_grid là một lưới ô với chi phí đi vào mỗi ô (0 là vật cản). Dựng từ những gì đang có:

@code
g.nav = njin::nav_grid_from_world(ctx, njin::level_bounds(ctx, level), {16, 16}, layer_world);
@endcode

Mọi collider không phải trigger thuộc `layer_world` (kể cả ô của tilemap khác `tile_none`) thành
vật cản. Sửa từng ô khi cửa mở, tường vỡ: nav_set_cost(), nav_set_area().

| Việc | Hàm |
|---|---|
| Đường ngắn nhất (A\*) | nav_find_path(): đi chéo, không cạ góc tường, rút gọn đường |
| Có nhìn thấy nhau không | collision_line_of_sight() |
| Đi theo đường | njin::nav_agent, nav_steer(): hướng đi, gán vào `topdown_body::input.move` |

Lưu ý hai điều:

- Quái đuổi theo tọa độ **tâm collider**, cùng chỗ lưới được dựng, không theo chân. Đi theo chân
  thì quái đi sát mép tường và bị kẹt.
- A\* tốn CPU, nên tìm lại mỗi 0.3 đến 0.5 giây và chỉ khi quái đang đuổi; `nav_path_opts::max_nodes`
  chặn một đích không tới được trên bản đồ lớn.

Quái chỉ nên đuổi khi **nhìn thấy** người chơi (tia không bị tường chắn), rồi bỏ cuộc khi người chơi
ở quá xa. Đó là tất cả "trí tuệ" mà một game nhỏ cần.

## Đặt vật lên lưới

Game xây làng, nông trại, túi đồ kiểu lưới đặt những vật chiếm nhiều ô và không được chồng lên nhau.
njin::place_grid nhớ ô nào thuộc vật nào. Mỗi vật là một khối `w` x `h` ô có góc trên trái ở `at`, mang
một số `id` do game chọn (khác njin::place_none, ví dụ chỉ số công trình cộng 1). Xoay 90 độ thì đổi `w`
và `h`.

| Việc | Hàm |
|---|---|
| Ô dưới chuột, bắt vật đang kéo theo ô (chuột nằm giữa vật) | place_cell_at(), place_snap() |
| Vùng thế giới của vật, để vẽ hay đặt sprite | place_rect() |
| Đặt được không (trong lưới, không chồng) | place_fits(); `ignore` là vật đang dời chỗ |
| Đặt, dời chỗ, gỡ | place_put(), place_remove() |
| Vật nào ở ô này | place_owner() |
| Chỗ trống đầu tiên (nhặt đồ vào túi, đặt sẵn đồ vừa mua) | place_find_free() |

@code
njin::place_grid village = njin::place_grid_make({0, 0}, {32, 32}, 40, 40);

// Đang kéo nhà 3x3 theo chuột: tô xanh nếu đặt được, đỏ nếu không.
const njin::cell at = njin::place_snap(village, njin::scr2w(ctx, njin::mouse_pos(ctx)), 3, 3);
const bool ok = njin::place_fits(village, at, 3, 3, dragging_id);
njin::draw_rect(ctx, njin::place_rect(village, at, 3, 3), ok ? green : red);
if (ok && njin::mouse_released(ctx, njin::mouse_left)) {
  njin::place_put(village, at, 3, 3, dragging_id); // dời chỗ nếu nhà đã có trên lưới
  njin::nav_set_area(g.nav, njin::place_rect(village, at, 3, 3)); // quái đi vòng qua nhà
}
@endcode

Lưới đặt vật không chặn đường đi: muốn quái đi vòng thì đánh dấu thêm trên njin::nav_grid như trên
(lúc dời chỗ, trả lại chỗ cũ bằng `nav_set_area(..., 1)`). Lưu lưới cùng game: `owner` là một mảng số.

## Ô có animation

Nước, đuốc, cỏ lay: khai báo animation cho ô trong Tiled (animation editor của tileset) và engine chạy
chúng, cả khi bản đồ đã bake vào ảnh chunk. Hoặc tự đặt bằng tilemap_animate(). Xem @ref tilemap.
