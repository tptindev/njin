# Sinh bản đồ tự động {#procgen}

Thay vì vẽ từng ô, bạn mô tả **cách** bản đồ được tạo ra: nhiễu cho hình dáng, vài luật cho vẻ tự nhiên, hoặc Wave
Function Collapse (WFC) cho các mảnh ghép khớp nhau. Mỗi lần đổi `seed` là một bản đồ mới, và cùng `seed` luôn cho
cùng một bản đồ.

@image html procgen_topdown.gif "Ví dụ top-down chạy thật: mỗi lần bấm R là một hòn đảo mới, đã bo góc bờ và mỏm đá. Camera được kéo ra xa để thấy cả bản đồ"

@image html procgen_platformer.gif "Ví dụ platformer chạy thật: mỗi lần bấm R là một màn mới, có hố, hang, bục, dốc và góc bo tròn"

## Ba bước

Mọi thứ đều đi qua một lưới tạm, njin::tile_grid, chỉ là dữ liệu nên không cần cửa sổ hay njin::context:

| Bước | Việc | Hàm |
|---|---|---|
| 1. Sinh | Ra hình dáng thô | njin::generate_topdown(), njin::generate_platformer(), njin::wfc_generate() hoặc tự dùng njin::noise_2d() |
| 2. Luật | Làm cho tự nhiên: bỏ chấm lẻ, thêm viền, rải hoa | njin::grid_majority(), njin::grid_border(), njin::grid_scatter()... |
| 3. Bo góc | Đổi ô "trên giấy" thành ô có góc tròn | njin::grid_autotile() |
| Xong | Đưa vào bản đồ để vẽ và va chạm | njin::tilemap_from_grid() |

Hai ví dụ đầy đủ dưới đây đi qua cả ba bước: một cho top-down, một cho platformer. Cả hai chạy trong thư mục
game mẫu tương ứng (chúng cần `assets/terrain.png`, xem @ref procgen_autotile). **R** là màn mới.

@include procgen_topdown.cpp

@include procgen_platformer.cpp

## Nhiễu: từ đơn giản đến phức tạp

njin::noise_2d() cho một số từ 0 đến 1 tại mỗi điểm, mượt theo không gian. njin::noise_desc có mọi núm chỉnh, và
mỗi núm làm một việc dễ thấy:

| Núm | Tác dụng | Khi nào chỉnh |
|---|---|---|
| `seed` | Đổi cả bản đồ | Mỗi màn |
| `frequency` | Nhỏ: vùng to; lớn: vùng vụn (0.02 đến 0.1) | Đầu tiên: quyết định cỡ vùng |
| `octaves` | Số lớp chi tiết chồng lên | Muốn mép có chi tiết nhỏ (3 đến 6) |
| `gain` | Độ mạnh của lớp nhỏ | Muốn mép gồ ghề (0.5 mặc định) |
| `lacunarity` | Lớp sau nhỏ hơn lớp trước bao nhiêu lần | Hiếm khi |
| `fractal` | `fractal_fbm` địa hình, `fractal_ridged` sống núi và sông, `fractal_billow` đồi tròn | Đổi dáng |
| `warp` | Uốn cong toạ độ (tính bằng ô) | Muốn viền cong queo (4 đến 20) |

Đoạn dưới in nhiễu ra chữ (`#` là chỗ nhiễu cao hơn ngưỡng) với từng núm bật dần lên. Đây là kết quả chạy thật:

@include procgen_noise.cpp

```text
1. một lớp, tần số 0.06
.################.................################..........
..###############...............##############..............
....#############.............###############...............
......###########............################...............
........#########............################.........######
.........########............#################....##########
.........########.............#################.############
........#########..............##############...############
........#########................##########.......##########
##....###########..................#####............########
##################...................................#######
##....#############...................................######
........#############.................................######
..........#############..............................#######
2. fBm, 5 lớp
...############..####.............#...########...#..####...#
#.##############..##.............###...#######..............
.###############.#####..........##############..............
......################........#################......####...
......#########..####.........###############.......########
...........####................##############....###########
...............................########...##...#############
..######..#.##.####...........#########......###############
..########....#####..........#######..####...####.##########
...########...........................####..........########
##.########..######....................#................####
####.......#########.....................................###
###......##############...............................######
.........###############...........................#########
3. fBm, 5 lớp, gain 0.7
...###########...####..........#..#...########...#..####...#
#.##############..###............###...#######............#.
.######.########.######.........##############...........##.
......###################.#..##################......#####..
.....######.###..######.......###############.......########
............###....#............#######.####.....###########
................................######....##...#############
.#######..#.##.####..........###..####.......######.....####
.#########....##.###.......#########...###...####.##########
...#######........#...................#####.#.....####..####
#..#######...##.###...................##................##..
####.......#########.##...............###................###
####........########.##...............................######
........##########..####..............##...........###....##
4. fBm, 5 lớp, warp 8
..##########..####.............#..################...#######
#################................####....########.......###.
###################.............#################...........
....################.........####################...........
....########.####............####################.......####
.....######....................################......#######
........##....................################....##########
.............#...............############..###..############
.#####.#.#######............#############.....#####.########
.#######.................................###..#....#..######
.#######...#.####........................##..............###
###.....###########..#.................................#####
##.....################..............................#######
......#################.............#######..........#######
5. ridged, 4 lớp
###................###................#####.......#.########
#####..............###..............#####.............####.#
....##............###..............###................###...
.....###.........####..............##.................##....
......###........####.................................##....
........###....#.#.###...........##..................#######
.........###........###.............................########
.........##.........####...........#.................######.
........####........###.............###..............#####.#
#.......####.......###................###...........###.####
###.....####.......###.................###......######.....#
######.###..........#...................###########........#
#########.........#.#........................####...........
.########.............#.....................................
```

Đọc từ trên xuống: (1) một lớp cho những mảng trơn; (2) thêm lớp thì mép có chi tiết; (3) `gain` cao làm mép gồ
ghề hơn; (4) `warp` uốn các mảng cho tự nhiên; (5) `fractal_ridged` cho các sống hẹp, hợp với núi hay sông.

@note Nhiễu fBm dồn quanh 0.5 và hiếm khi ra ngoài 0.2 đến 0.8, nên "ngưỡng 0.3" không có nghĩa là 30% bản đồ.
njin::generate_topdown() tự kéo giãn cho đủ 0 đến 1, nên ngưỡng của các vùng đất là **phần trăm** của bản đồ. Khi
tự dùng njin::noise_2d(), hãy đặt ngưỡng bằng mắt như ở đoạn trên, hoặc xếp hạng các giá trị rồi lấy phần trăm.

## Luật: làm bản đồ trông tự nhiên

Nhiễu thô thường vụn: chấm cỏ lẻ giữa nước, vũng một ô, mép răng cưa. Các luật sửa đúng những thứ đó, và chạy
trên bất kỳ njin::tile_grid nào, dù nó đến từ nhiễu, WFC hay một bản đồ chữ (njin::tile_grid_from_text()).

| Luật | Làm gì | Ví dụ |
|---|---|---|
| njin::grid_majority() | Mỗi ô theo loại chiếm đa số quanh nó | Xoá chấm lẻ, làm mượt mép |
| njin::grid_smooth() | Automat tế bào cho hai loại ô | Hang tròn trịa |
| njin::grid_remove_small(), njin::grid_merge_small() | Bỏ vùng nhỏ | Ao một ô, đảo lạc |
| njin::grid_keep_largest() | Chỉ giữ chỗ đi được lớn nhất | Không góc nào bị cô lập |
| njin::grid_border() | Viền một loại ô quanh loại khác | Cát giữa nước và cỏ, cỏ trên mặt đất |
| njin::grid_scatter() | Rải ngẫu nhiên, có khoảng cách tối thiểu và điều kiện | Hoa, bụi, đá, đuốc |

@include procgen_rules.cpp

Bản đồ trước và sau khi áp ba luật (`.` là nước, `,` là cát, `#` là cỏ):

```text
Trước khi áp luật
,##,.,,.#,,.,,#...,,,#,..,.,...,######......,###.,,,#####,..
#####......####..,,..,,######....#######...,,#,......,,#####
###,......####....#,,###,.......#,######....,##......,,,#,.,
,,###.,#####,#.....,#####,.....,,####,......##,,,,,##.,,,.##
..####,,,#####,......####,..,.#####,#......,.,...,##,..,####
....,,...,##......#..,,####,,,####,......,##,,.....#,,,..,#,
,###,,,..,#,,,..###,...##,...###......,#####.......##...,###
#############,,,###...,#,,.,,##,.....#######,.....,##,...,#,
.###########,,,...,,###..,#####....,#########....,,,#,,....,
#######,,###.......###,.,,.,###,.....,########....,,,##,#,,,
.,###,....#####...,###,.,########......####,,,...#######,,##
,,.......,############,.....,##,.,...,,#######,.....,,##,,.,
......,,.,#,,#####.,.,..#.......,..,########,##,...........#
##########,..,,,.,,#,,,#######,#,......,,,,,....###,,#####,,
Sau khi áp luật
####,......,##,...,######,......,####,......,.......,#######
####,......,##,...,######,......,#####,.....,........,,#####
####,.....,##,....,######,......,#####,.....,........,,,####
####,.,,,,###,.....,,####,.....,#####,............,,,,,,####
#####,######,........,###,....,###,,,.......,.....,,,,,,,###
############,.........,##,...,##,,.......,,,.......,,,...,##
###########,...........,#,..,##,......,,,##,.......,,....,##
###########,..........,#,,,,##,......,#####,.......,,....,##
###########,........,,#,,,,###,......,######,.............,#
#####,,,,,##,......,##,,,,#####,.....,#######,......,,,,,,##
,,,,,.....,##,,...,###,,.,,,###,......,######,.....,########
.........,#####,,,####,.....,,,.......,######,......,,######
.........,,#########,,................,#####,,........,#####
...........,##########,................,###,.........,######
```

Mảng cỏ vụn thành vài khối liền, chấm lẻ biến mất, và chỗ cỏ chạm nước có viền cát. Thứ tự các luật quan trọng:
làm mượt và bỏ vùng nhỏ trước, viền sau cùng.

## Bộ sinh top-down

njin::generate_topdown() nhận một bảng **vùng đất** (njin::biome) theo độ cao và, nếu muốn, độ ẩm:

- `biomes`: xét theo thứ tự, vùng đầu tiên thoả `max_height` và `max_moisture` được chọn. Ví dụ
  `{{water, 0.3}, {sand, 0.35}, {grass, 0.8}, {rock, 1.0}}`: 30% thấp nhất là nước, rồi cát, cỏ, và đỉnh là đá.
- `island`: kéo độ cao xuống về mép, để bản đồ là hòn đảo giữa biển.
- `walkable`, `blocked_tile`: chỉ giữ chỗ đi được **lớn nhất**, phần còn lại thành vật cản. Điểm xuất phát
  (`result.spawn`) luôn nằm trong đó, nên người chơi không bao giờ bị nhốt.
- `border_tile`: vòng tường quanh bản đồ.
- `smooth`, `min_region`: số lần làm mượt và cỡ vùng nhỏ nhất được giữ.
- Độ ẩm (`moisture_noise`, `max_moisture`) cho sa mạc và rừng ở cùng độ cao.

`result.heights` trả lại độ cao từng ô, để luật của riêng bạn dùng (ví dụ đặt cây ở chỗ cao vừa).

## Bộ sinh platformer

njin::generate_platformer() dựng đường mặt đất bằng nhiễu một chiều, rồi đục hang, đào hố, đặt bục. Khác với top-down,
nó có **luật chơi được**, vì màn nhìn ngang mà nhảy không tới thì hỏng:

| Luật | Tham số | Mặc định | Vì sao |
|---|---|---|---|
| Hai cột cạnh nhau chênh không quá `max_step` ô | `max_step` | 2 | Nhân vật mặc định nhảy cao gần 3 ô |
| Hố không rộng quá `pit_max` ô | `pit_min`, `pit_max` | 1 đến 2 | Nhảy xa khoảng 59 px, hố 2 ô là 32 px |
| Hai cột trước hố và cột sau hố bằng nhau | | | Tránh bước xuống ngay mép hố, không còn chỗ đạp |
| Hai đầu màn bằng phẳng, không hố | `safe_columns` | 6 | Chỗ xuất phát và đích |
| Hang không đục lên sát mặt đất | `cave_margin` | 4 | Không thủng sàn, không sụp xuống hang giữa đường |
| Bục lơ lửng thấp hơn tầm nhảy | `platform_height` | 3 | Nhảy lên được |
| Chỗ đất lên hoặc xuống đúng 1 ô có thể là dốc | `slope_r_tile`, `slope_l_tile` | không dùng | Đi lên êm hơn |

`caves` là **tỉ lệ diện tích ngầm** được đục (0.2 là chừng 20%): hang được chọn theo thứ hạng của nhiễu, rồi bo tròn
bằng automat tế bào, và hốc dưới 8 ô bị lấp.

@note Những con số trên dành cho njin::platformer_body mặc định (chạy 110 px/s, nhảy cao chừng 45 px, xa chừng
59 px). Đổi `run_speed` hay `jump_speed` của nhân vật thì đổi `pit_max`, `max_step`, `platform_height` theo.

**Đã kiểm chứng thế nào:** một bot chạy sang phải và nhảy khi gặp hố hay tường, điều khiển njin::platformer_body thật
trên chính màn do ví dụ trên sinh ra (160 x 30, `caves` 0.2, `pit_chance` 0.06, `platform_chance` 0.05, không dùng dốc),
tới được đích ở **cả 20 seed liền nhau** (1 đến 20). Đó là bằng chứng cho các tham số ở trên, không phải lời hứa cho mọi
tham số: nới `pit_max` hay `max_step` quá tầm nhảy thì màn có thể không đi được.

## Bo góc tự nhiên: autotile {#procgen_autotile}

Một lưới ô vuông trông "răng cưa" vì mỗi ô là hình vuông. **Autotile** sửa việc đó: đất được vẽ bằng một **bộ ô**,
và mỗi ô của lưới chọn tấm nào trong bộ tuỳ theo các ô xung quanh nó. Chỗ đất hở ra thì có mép, hai mép gặp nhau thì
góc tròn, chỗ đất lõm vào thì có góc lõm. Bạn viết luật: "những ô nào là đất, bộ ô nào vẽ chúng".

njin::grid_autotile() làm việc đó trên njin::tile_grid, bằng njin::autotile_rule:

| Trường | Ý nghĩa |
|---|---|
| `tiles` | Các ô của lưới mà luật này thay bằng ô của bộ. Cỏ và đất có thể cùng là một địa hình |
| `joins` | Ô khác vẫn được coi là nối liền, nhưng không bị thay (đá dưới đất, cửa trên tường) |
| `base` | Số thứ tự ô đầu tiên của bộ trong tileset; bộ là các ô liền nhau |
| `layout` | njin::autotile_blob (47 ô, có cả góc lồi và góc lõm) hoặc njin::autotile_edges (16 ô, chỉ nhìn bốn phía) |
| `outside` | Những phía mà **ngoài lưới** coi như đất chạy tiếp (các bit njin::grid_side). Mặc định cả bốn |

Một ô nhìn tám ô kề, cộng thành mặt nạ (njin::autotile_neighbor). Ô chéo chỉ tính khi **cả hai** ô kề thẳng cạnh nó
cũng nối liền (nếu không, mép đã che góc rồi), nên còn đúng 47 kiểu. njin::autotile_index() đổi mặt nạ thành số thứ tự
ô trong bộ:

@include procgen_autotile.cpp

```text
29 ô đất được thay bằng ô của bộ

  .   .   .   .   .   .   .   .   .   . 
  .   .  10  31  31  26   .   .   .   . 
  .  10  33  46  46  45  26   .   .   . 
  .   4  41  46  46  46  42   .   .   . 
  .   .   4  36  41  46  42   .   .   . 
  .   .   .   .  12  46  45  26   .   . 
  .   .   .   .   4  36  36  34   .   . 
  .   .   .   .   .   .   .   .   .   . 

ô (2, 1): mặt nạ 28 -> ô số 10 của bộ (khớp: có)
```

Số 46 là ô ở giữa vùng (đủ tám ô kề). Góc trên trái của khối là ô số 10, đúng bằng mặt nạ
`neighbor_right | neighbor_down | neighbor_down_right` tính tay ở dòng cuối.

Game mẫu có sẵn hai bộ 47 ô (đất phủ cỏ và đá) trong `terrain.png`, xếp đúng thứ tự của njin::autotile_index():

@image html procgen_autotile_sheet.png "terrain.png của game mẫu platformer, vẽ lên nền trời: bộ đất phủ cỏ (trái) và bộ đá (phải), mỗi bộ 47 ô, đánh số theo thứ tự trong bộ. Ô 46 là ô ở giữa vùng"

Cùng một màn, một bên chỉ dùng ô "đầy đủ" (46 và 93) cho mọi ô, một bên qua njin::grid_autotile():

@image html procgen_autotile_compare.png "Trái: mọi ô là ô đầy đủ, không autotile. Phải: cùng màn qua njin::grid_autotile(): mép có viền, cỏ chỉ ở mặt trên, góc lồi tròn"

Vài điều nên biết:

- **Gọi sau cùng.** Autotile đổi ô "trên giấy" thành số thứ tự ô của bộ, nên các luật đọc loại ô (njin::grid_border(),
  njin::grid_scatter() với điều kiện) phải chạy **trước**. Các luật trong một lần gọi đều đọc từ một bản chụp lưới,
  nên thứ tự chúng không ảnh hưởng, và số thứ tự ô mới không bị nhầm với ô cũ.
- **Va chạm vẫn là ô vuông.** Góc tròn chỉ là hình vẽ; hộp va chạm của ô vẫn đầy đủ. Ở top-down đó thường đúng ý;
  ở platformer, đứng sát mép góc bo sẽ hơi "lơ lửng" vài pixel. Nếu cần hình khớp hẳn, dùng dốc (njin::tile_slope_r).
- **Nhiều lớp.** Ví dụ top-down ở trên chia thành ba tilemap (nước, đất, đá và hoa) với njin::tilemap::layer: đất bo
  góc để lộ nước bên dưới, đá bo góc để lộ cỏ bên dưới. Đó là cách có bờ biển và mỏm đá tròn mà không cần ô "cát
  chuyển sang nước" riêng cho từng cặp.
- **Ngoài lưới.** Mặc định ngoài lưới là đất chạy tiếp, nên mép bản đồ không bo. Bỏ `side_up` khỏi `outside` cho mặt đất
  platformer, như ví dụ; đặt `outside = 0` cho hòn đảo, như ví dụ top-down.
- **Tự vẽ bộ ô.** Bộ 47 ô là quy ước phổ biến (còn gọi "blob tileset"). Dùng ảnh của bạn thì xếp các ô theo đúng thứ tự
  của njin::autotile_index(); script `src/games/shared/tools/make_terrain.py` là ví dụ cách dựng chúng từ mặt nạ.

## Wave Function Collapse

WFC lấp lưới sao cho mọi cặp ô cạnh nhau đều **hợp lệ**. Bạn không đặt hình dáng, bạn đặt luật kề: "ô nào được đứng bên
phải, bên dưới... ô nào". Có hai cách có luật:

### Học từ một mẫu

njin::wfc_learn() đọc một njin::tile_grid mẫu (thường viết bằng njin::tile_grid_from_text()): mọi cặp ô cạnh nhau
trong mẫu thành luật kề, số lần xuất hiện thành độ hay gặp. Mẫu nhỏ và đa dạng cho kết quả tốt; cặp nào mẫu không có thì
không bao giờ xuất hiện. njin::wfc_desc::allowed cố định những chỗ bạn muốn (trời ở hàng trên, đá ở hàng dưới):

@include procgen_wfc.cpp

```text
                                                            
        ggggg          g              gg  gg g  ggg gggg ggg
       gdddddggg  gggg g       g g g  gg gdggg  gddgddddgddg
gg    ggdddsddddggdddg ggg g  gdgg gggddgddgddgggddddddddddd
ddg  gdddddsddddddddddgdgdgg ggddggddgdddddgdddddddddddddddd
dddggddddddsdddddddddddddsdg gddddddddsddddddddddddddddddddd
ddddgddddddsdddddddddddddsdgggddddddsdsdddddddddddddsdddddds
dddddddddddsdddddddddddddsdgddddddddsdsdddsdddddddddsdddddds
dddddddddddsdsdddddddddddsddddddddddsdsdddsddddddsddsdddddds
dddddddddddsdsdddddddddddsddddddddddsdsdsdsddddddsddsdddddds
dddddddddddsdsdddddddddddsddddddddddsssdsssdddddssddsdddddds
dddddddddddsdsdsdddddddddsddddddddsdsssdsssdddsdssddsdddddds
ddsdddsddddsdsdsddsddddddsddddddddsdsssdsssdsdsdssddsdddddds
ddsdddsddddsdsdsdssddddddsddddddddsdsssdsssdsdsdssddsdddddds
ddsdsdsddddsdsdsdsssdsdddsddddddsdsssssdsssdsdsdssddsdsdddds
ddsdsdsddddsdsdsdsssdsdsdsdsddsdsdsssssdsssdsdsdssddsdsdddds
sdsdsdsdsddsdsdsdsssdsdsssssddsdsssssssdsssdsssdssddsssdddds
sdsdsdsdssdsdsdsssssdsssssssdssdsssssssdsssdsssdssddsssssdds
sssdsdsdssdsdsssssssdsssssssdssdsssssssdssssssssssdsssssssds
ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
```

Mọi cột đều đúng thứ tự trời, cỏ, đất, đá, và không có cỏ nào lơ lửng, vì mẫu chưa từng cho cỏ đứng dưới đất. Nhưng
luật là theo **cặp ô**, không theo mảng ô: WFC này không biết "đồi rộng chừng nào", nên bề mặt có thể lởm chởm và đôi
chỗ thành cột. Cho hình dáng tốt hơn thì dùng nhiễu (bộ sinh ở trên) rồi để WFC lo phần chi tiết, hoặc cho mẫu nhiều
đoạn phẳng hơn (cặp cỏ với cỏ hay gặp hơn, nên bề mặt có xu hướng phẳng hơn), hoặc dùng luật tự viết như dưới đây.

### Luật tự viết: đường nối liền

Với bộ ô có **mép khớp nhau** (đường, sông, hành lang), luật viết bằng tay dễ hơn mẫu: hai ô cạnh nhau hợp lệ khi cùng có
đường hoặc cùng không có ở phía chạm nhau. Mười sáu ô đường đánh số đúng như njin::autotile_edges (bit 1 lên, 2 phải, 4 xuống,
8 trái), nên cùng bộ ô đó dùng để vẽ luôn:

@include procgen_wfc_roads.cpp

```text
     ┌┐     ┌┬─┐ ┌─┐┌┐  ┌─┐         ┌─┐ 
 ┌───┼┤    ┌┘└─┘ │ │└┘  │ │         ├┐├╴
 │   ││  ┌─┼─┐ ┌─┴┐│    └─┤ ┌┐      ├┴┴┐
 ├───┘│  │╷└┐│ └┐┌┘└┐┌────┤ │└┐     └──┘
┌┘    │ ┌┘└─┘│┌─┴┘ ┌┘│ ┌─┐│ │ │   ┌─┐   
└─────┘ │ ┌──┴┘ ┌┐ ├┬┘┌┴┬┘└─┘ │┌┐┌┘ │┌─┐
   ┌───┐│ │┌────┴┘┌┘└┐└─┤┌─┐  ││││  └┘╶┘
┌┐ │┌──┘└─┼┘ ┌┐   └┐ │  ││ └─┐││└┤      
└┤┌┴┘┌──┐┌┘╷ │└┐   └┬┘ ┌┘├┬─┐│└┘┌┘┌┐ ┌─┐
 │└┬─┤ ╷└┘ └┐│ └┐   │┌┐└─┘└┬┼┘  └─┘│ └─┘
 │╶┘ └─┘    │└──┘┌┐ └┴┼─┐  ││      │┌┐  
 └┐      ┌──┘    └┴┐  └─┘  └┘      │└┤  
  └──────┘    ┌┐   └┐   ┌───┐┌┐  ┌┐└┐│  
              └┴────┘   └───┘└┘  └┘ └┘  
```

Mọi chỗ hai ô chạm nhau, đường đều khớp, và không có đường nào chạy ra ngoài bản đồ (đã kiểm từng cặp ô của kết quả trên:
0 chỗ không khớp). Độ hay gặp (`weight`) quyết định cảnh: cỏ nhiều, đường thẳng thường gặp, ngã tư hiếm.

### Các tham số

| Tham số | Ý nghĩa |
|---|---|
| `seed` | Cùng seed cùng kết quả |
| `attempts` | WFC không quay lui: gặp ngõ cụt (một ô không còn lựa chọn) thì làm lại với thứ tự ngẫu nhiên khác, tối đa từng ấy lần. Mặc định 20 |
| `periodic` | `true`: mép phải nối mép trái, mép dưới nối mép trên (để lát nền, lặp được) |
| `allowed` | Ràng buộc cố định: ô `tile` có được đặt ở `(x, y)` không |
| njin::wfc_allow(), njin::wfc_add_tile() | Dựng luật và đổi độ hay gặp bằng tay |

njin::wfc_generate() trả `false` khi luật quá chặt, ràng buộc mâu thuẫn hoặc hết số lần thử, và **không đổi** lưới
đầu ra. Luôn kiểm tra giá trị trả về, và có chỗ dự phòng (một bản đồ vẽ tay, hay thử seed khác).

Kết quả của WFC cũng là njin::tile_grid, nên các luật ở trên (njin::grid_scatter(), njin::grid_autotile()...) chạy tiếp
được.

## Đưa vào game

njin::tilemap_from_grid() đặt lưới vào njin::tilemap, góc trên trái ở ô `origin`. Ô -1 của lưới **không** xoá ô đang
có, nên chồng được nhiều lưới; muốn sinh lại từ đầu (như khi bấm R) thì gọi njin::tilemap_clear() trước, như hai ví dụ.
Va chạm và animation của ô do njin::tilemap_set_shape() và njin::tilemap_animate() quyết định như mọi tilemap (xem
@ref tilemap). `result.spawn` (và `result.goal` của platformer) là ô để đặt nhân vật.

## Lưu ý

- **Cùng seed, cùng bản đồ**, trên mọi máy đã thử: một chuỗi băm của 12 seed cho cả ba bộ sinh ra **cùng một giá trị**
  khi biên dịch bằng gcc trên Windows (MinGW) và trên Linux (gcc 15), ở `-O0`, `-O2` và `-O3 -march=native`. Chưa thử
  bằng MSVC hay clang. Lưu `seed` (số nhỏ) thay vì lưu cả bản đồ.
- Sinh chạy một lần lúc nạp màn, không mỗi frame. Đo trên bản `-O2`: bản đồ top-down 64 x 48 mất dưới 1 ms, màn
  platformer 160 x 30 dưới 0,3 ms; WFC là phần nặng nhất (một lưới 96 x 64 mất khoảng 0,2 giây), nên sinh trước khi
  vào màn.
- **Sinh xong nên nhìn thử.** Nhiễu và luật cho ra bản đồ *hợp lệ*, không phải bản đồ *hay*. Đổi seed vài chục lần và xem.

@see njin::tile_grid, njin::generate_topdown(), njin::generate_platformer(), njin::grid_autotile(), njin::wfc_generate()
@see tilemap, level, platformer, topdown
