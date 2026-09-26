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

## Ô có animation

Nước, đuốc, cỏ lay: khai báo animation cho ô trong Tiled (animation editor của tileset) và engine chạy
chúng, cả khi bản đồ đã bake vào ảnh chunk. Hoặc tự đặt bằng tilemap_animate(). Xem @ref tilemap.
