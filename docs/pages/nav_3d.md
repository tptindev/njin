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

Độ cao của các điểm trên navmesh là gần đúng: trên sàn phẳng sai vài cm, trên đồi lượn sai đến vài chục cm. Để
đặt hình đúng mặt đất, lấy độ cao từ terrain3d_height() hay một tia xuống, hoặc cho tác tử lái một nhân vật vật
lý (phần dưới).

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

## Ví dụ đầy đủ

Một căn phòng có tường và bục có dốc; tám tác tử đi tới chỗ chuột trái bấm; đường từ con đầu tới chỗ chuột được
vẽ thử.

@include nav3d.cpp

## Từ Lua

Script (@ref scripting) gọi được `njin.nav3d_path(navmesh, from, to)` (bảng các điểm, hay nil),
`njin.nav3d_set_target(agent, target)`, `njin.nav3d_stop`, `njin.nav3d_position`, `njin.nav3d_velocity` và
`njin.nav3d_arrived`, với `navmesh` và `agent` là số `id` của handle mà phía C++ đưa sang (ví dụ bằng
script_set_global()).

## Giới hạn

- Navmesh dựng từ hình học tĩnh. Vật động (thùng bị đẩy, cửa) không tự cắt navmesh: thêm hay bỏ hình rồi
  navmesh3d_rebuild() vùng đó.
- Các tác tử chỉ né nhau trên cùng một navmesh. Mỗi navmesh dành cho một cỡ tác tử; quái to và quái nhỏ cần hai
  navmesh.
- Không có vùng có giá (đầm lầy đi chậm, đường cấm): mọi chỗ đi được ngang nhau.
