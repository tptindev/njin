# Tìm đường 3D và đám đông {#nav_3d}

Trang này cho nhân vật do máy điều khiển tự đi trong màn 3D: tìm đường quanh tường, lên dốc, lên bục, nhảy qua
khe; và đi thành đám đông mà không đâm vào nhau. Mọi thứ khai báo trong `njin_nav3d.h`, dựng trên Recast và
Detour (thư viện tìm đường của rất nhiều game 3D), nằm trong engine.

Cần biết trước: @ref graphics_3d (begin_3d(), vật lý 3D). Game 2D theo lưới ô thì dùng `nav_grid` của
@ref topdown, đơn giản hơn nhiều.

## Navmesh là gì

Navmesh là các đa giác phủ lên **chỗ đi được** của màn chơi: mặt sàn, mặt dốc, mặt bục, đã chừa ra một khoảng
bằng bán kính nhân vật quanh tường và vật cản. Tìm đường trên đó nhanh hơn trên lưới ô rất nhiều, và đường đi
ra là những đoạn thẳng gấp khúc ở góc vật cản, không đi zíc zắc theo ô.

Engine dựng navmesh từ hình học tĩnh mà game đưa vào, theo cỡ của nhân vật:

| Trường của njin::navmesh3d_desc | Nghĩa |
|---|---|
| `agent_radius` | Chỗ đi được cách tường bấy nhiêu mét |
| `agent_height` | Chỉ chui qua chỗ trần cao hơn số này |
| `agent_climb` | Bậc cao nhất bước lên được |
| `max_slope` | Dốc nhất đi được, độ |
| `cell_size`, `cell_height` | Độ mịn khi dựng: nhỏ thì sát tường hơn, dựng lâu hơn |
| `tile_size` | Cạnh mỗi ô vuông của navmesh, để dựng lại từng vùng |

## Dựng navmesh

Tạo navmesh, thêm hình học, rồi dựng một lần lúc nạp màn:

```cpp
const njin::navmesh3d_handle nav = njin::navmesh3d_create(ctx, {.agent_radius = 0.4f});
njin::navmesh3d_add_box(ctx, nav, {0, -0.5f, 0}, {30, 1, 30});   // sàn
njin::navmesh3d_add_model(ctx, nav, level_model, {0, 0, 0});     // model của màn
njin::navmesh3d_add_terrain(ctx, nav, ground);                   // địa hình của @ref world_3d
njin::navmesh3d_build(ctx, nav);
```

| Hàm | Thêm gì |
|---|---|
| navmesh3d_add_box() | Một hình hộp, xoay được: sàn, tường, bục, bàn |
| navmesh3d_add_mesh() | Một lưới tam giác trong thế giới (được chép lại) |
| navmesh3d_add_model() | Các tam giác của một model, đặt như draw_model() |
| navmesh3d_add_terrain() | Một địa hình; độ cao đọc lại mỗi lần dựng |
| navmesh3d_add_link() | Lối tắt không liền mặt đất: nhảy qua khe, leo thang, nhảy xuống bục |

Tam giác cần quay mặt trước lên trên (ngược chiều kim đồng hồ khi nhìn từ trên xuống, như mọi lưới của njin).
Dựng mất từ vài chục mili giây (một căn phòng) đến vài giây (bản đồ lớn với `cell_size` nhỏ).

Khi màn chơi đổi (sửa địa hình, mở cửa, đặt thêm vật cản bằng navmesh3d_add_box()), navmesh3d_rebuild() dựng lại
các ô vuông trong một vùng, không phải cả bản đồ. Tác tử đang đi tự tìm lại đường.

## Hỏi navmesh

| Hàm | Trả lời |
|---|---|
| navmesh3d_path() | Đường từ A đến B: các điểm gấp khúc. Không đến được B thì dừng ở chỗ gần B nhất |
| navmesh3d_nearest() | Điểm gần nhất trên navmesh |
| navmesh3d_raycast() | Đi thẳng trên mặt đất từ A về B, dừng ở mép đầu tiên: có thấy nhau không, lao tới được bao xa |
| navmesh3d_random_point() | Một điểm ngẫu nhiên trên cả navmesh |
| navmesh3d_random_point_near() | Một điểm ngẫu nhiên đi tới được, cách một tâm không quá bán kính: quái đi lang thang quanh ổ |
| navmesh3d_draw_debug() | Vẽ các đa giác bằng gizmo, để xem engine thấy chỗ nào đi được |

```cpp
std::vector<njin::vec3> path;
if (njin::navmesh3d_path(ctx, nav, guard_pos, player_pos, path))
  for (size_t i = 0; i + 1 < path.size(); i++)
    njin::gizmo_line3d(ctx, path[i], path[i + 1], njin::colors::yellow);
```

Điểm của đường đi và vị trí tác tử nằm trên mặt đất. Trên địa hình đã thêm bằng navmesh3d_add_terrain(), độ cao
lấy đúng theo terrain3d_height(), và đoạn nào cắt qua đồi thì có thêm điểm giữa để đường bám mặt đất (cách không quá
5 cm). Trên hình học khác (model, hộp nghiêng) độ cao theo lưới chi tiết của navmesh, có thể sai vài cm; muốn chính
xác thì lấy độ cao bằng một tia xuống, hoặc cho tác tử lái một nhân vật vật lý (phần dưới).

## Đám đông

Tác tử (nav3d_agent_add()) là một nhân vật tự tìm đường và tự đi: game chỉ đặt đích. Các tác tử trên cùng một
navmesh né nhau trên đường đi và tách nhau ra khi đứng sát, nên một nhóm quái đuổi theo người chơi không dồn
thành một cục. Engine cập nhật chúng trong `phase_post_update` theo delta().

```cpp
const njin::nav3d_agent_handle orc = njin::nav3d_agent_add(ctx, nav, {.position = spawn, .max_speed = 4.0f});
njin::nav3d_agent_set_target(ctx, orc, player_pos);    // gọi lại mỗi frame khi đích chạy cũng được
const njin::vec3 p = njin::nav3d_agent_position(ctx, orc);
const njin::vec3 v = njin::nav3d_agent_velocity(ctx, orc); // hướng quay mặt, chọn animation đi hay chạy
if (njin::nav3d_agent_arrived(ctx, orc))
  attack();
```

nav3d_agent_stop() bỏ đích, nav3d_agent_teleport() đặt sang chỗ khác ngay, nav3d_agent_remove() bỏ tác tử.

**Lái nhân vật vật lý.** Đặt `nav3d_agent_desc::character` là một njin::character3d_handle: mỗi frame vận tốc
của tác tử thành vận tốc ngang của nhân vật, nhân vật rơi theo trọng lực khi không đứng trên sàn, và chỗ nhân
vật thật sự đến (bị đẩy, va vào thùng) thành chỗ của tác tử. Game đừng tự đặt vận tốc cho nhân vật đó nữa.

## Vùng và chi phí {#nav_3d_areas}

Mỗi chỗ đi được thuộc một **vùng**, số từ 0 đến 15 (njin::nav3d_max_areas). Vùng 0 là mặt đất thường; các số
khác game tự đặt nghĩa: đường cái, cỏ, đầm lầy, nước nông, ô cửa. Đánh dấu vùng theo hai cách:

- Cả một mảng hình học: tham số `area` cuối của navmesh3d_add_mesh(), navmesh3d_add_model(), navmesh3d_add_box(),
  navmesh3d_add_terrain() và navmesh3d_add_link() (lối tắt cũng có vùng: nhảy qua khe thì đắt).
- Một khối đứng trên bản đồ: navmesh3d_add_area() với tâm, cỡ, góc xoay quanh trục đứng và số vùng. Navmesh đã
  dựng thì các ô vuông khối chạm vào được dựng lại ngay; navmesh3d_remove_area() bỏ khối.

**Bộ lọc** (njin::nav3d_filter) nói cách tìm đường nhìn các vùng: `cost[vùng]` là giá mỗi mét (1 là bình thường,
8 là một mét ở đó tốn như tám mét đường thường) và `excluded` là các vùng cấm hẳn (bit `1 << vùng`). Mỗi navmesh
giữ 16 bộ lọc, đặt bằng navmesh3d_set_filter(); đổi bộ lọc **không** cần dựng lại navmesh, và tác tử đang đi theo
nó tìm lại đường ngay. Bộ lọc 0 là bộ lọc mặc định: navmesh3d_path() không chọn bộ lọc dùng nó. Tác tử chọn bộ lọc
bằng `nav3d_agent_desc::filter`, đổi bằng nav3d_agent_set_filter().

```cpp
constexpr njin::u8 swamp = 2, door = 3;
njin::navmesh3d_add_area(ctx, nav, {-4.0f, 0.0f, 0.0f}, {10.0f, 4.0f, 14.0f}, 0.0f, swamp);
njin::nav3d_filter f{};
f.cost[swamp] = 8.0f;     // vòng qua đầm lầy, trừ khi vòng quá xa
f.excluded = 1u << door;  // không bao giờ đi qua cửa khóa
njin::navmesh3d_set_filter(ctx, nav, 1, f);
std::vector<njin::vec3> path;
njin::navmesh3d_path(ctx, nav, from, to, path, 1); // tìm đường theo bộ lọc 1
```

Khi hai mặt nằm sát nhau (đường cái phủ lên sàn), vùng số lớn hơn thắng. Khối của navmesh3d_add_area() luôn thắng
vùng của hình học bên dưới.

## Nhiều cỡ tác tử

Khoảng chừa quanh tường (`agent_radius`) được tính lúc dựng, nên mỗi cỡ tác tử cần một navmesh. navmesh3d_clone()
chép hình học, vùng, lối tắt, vật cản và bộ lọc của một navmesh sang navmesh mới dựng theo cỡ khác; dựng cả hai,
rồi thêm tác tử to vào navmesh to. Hình học, vùng hay vật cản thêm về sau thì thêm cho từng navmesh. Tác tử trên
hai navmesh khác nhau không né nhau.

## Vật cản di động

navmesh3d_add_obstacle() đặt một hộp (hay hình trụ, khi `radius` lớn hơn 0) lên navmesh: chỗ nó đứng không còn đi
được, cách nó một khoảng `agent_radius` như tường. navmesh3d_move_obstacle() dời nó, navmesh3d_remove_obstacle()
bỏ nó. Không cần thêm hình học hay gọi navmesh3d_rebuild(): engine dựng lại các ô vuông nó chạm vào ở các frame
sau, tối đa `navmesh3d_desc::obstacle_tiles_per_frame` ô mỗi frame (mặc định 4), và tác tử tìm đường vòng qua nó.
navmesh3d_pending_tiles() cho biết còn bao nhiêu ô chờ. Mỗi ô vuông 8 m dựng lại mất vài mili giây, nên dời vật cản
khi nó thật sự đổi chỗ (thùng dừng lại, cửa đóng xong), không phải mỗi frame khi nó đang trượt.

## Ví dụ đầy đủ

Một căn phòng có tường và bục có dốc; tám tác tử đi tới chỗ chuột trái bấm; đường từ con đầu tới chỗ chuột được
vẽ thử.

@include nav3d.cpp

Vùng, bộ lọc, hai cỡ tác tử và một vật cản di động: lính gác đi qua cửa, tránh đầm lầy, đi đường cái; con quái to
có navmesh riêng; phím K lấy chìa khóa của lính gác.

@include nav3d_areas.cpp

## Từ Lua

Script (@ref scripting) gọi được `njin.nav3d_path(navmesh, from, to)` (bảng các điểm, hay nil),
`njin.nav3d_set_target(agent, target)`, `njin.nav3d_stop`, `njin.nav3d_position`, `njin.nav3d_velocity` và
`njin.nav3d_arrived`, với `navmesh` và `agent` là số `id` của handle mà phía C++ đưa sang (ví dụ bằng
script_set_global()). Vùng và vật cản:

- `njin.nav3d_path(navmesh, from, to, filter)`: tìm đường theo bộ lọc số `filter`.
- `njin.nav3d_set_filter(navmesh, index, {cost = {[2] = 8}, exclude = {3}})`, `njin.nav3d_agent_filter(agent, index)`.
- `njin.nav3d_add_area(navmesh, center, size, yaw, area)` trả về số của khối; `njin.nav3d_remove_area(navmesh, id)`.
- `njin.nav3d_add_obstacle(navmesh, {position = ..., size = ..., yaw = 0, radius = 0})` trả về số của vật cản;
  `njin.nav3d_move_obstacle(navmesh, id, position, yaw)`, `njin.nav3d_remove_obstacle(navmesh, id)`.

## Giới hạn

- Vật cản cắt navmesh bằng cách dựng lại cả ô vuông nó chạm vào (vài mili giây mỗi ô 8 m), không phải bằng tile
  cache của Detour: hợp với thùng, xe đậu, cửa; không hợp với hàng chục vật cản cùng chạy mỗi frame.
- Các tác tử chỉ né nhau trên cùng một navmesh, nên tác tử to (navmesh to) và tác tử nhỏ không né nhau.
- Vùng chỉ có 16 số (0 đến 15).
