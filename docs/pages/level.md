# Level từ Tiled và LDtk {#level}

Vẽ màn chơi trong editor, không viết cứng trong code. njin đọc được file của hai editor 2D
phổ biến, bằng cùng một hàm. Không dùng editor? Viết bản đồ bằng chữ, mỗi ký tự một ô: xem
@ref tilemap.

| Editor | File | Ghi chú |
|---|---|---|
| [Tiled](https://www.mapeditor.org) | `.tmx` (mặc định, XML), `.tmj` / `.json` | Tileset tách file (`.tsx`, `.tsj`) được đọc theo |
| [LDtk](https://ldtk.io) | `.ldtk` | Cả khi bật "Save levels to separate files" (`.ldtkl`) |

@include level.cpp

## Những gì được tạo ra

njin::level_load() đọc file và tạo entity ngay:

| Trong editor | Thành entity |
|---|---|
| Layer ô (Tiled tile layer, LDtk Tiles / AutoLayer) | njin::tilemap. Một layer dùng nhiều tileset thì mỗi tileset một tilemap |
| Layer IntGrid của LDtk | Một tilemap **ẩn**, giá trị ô là giá trị IntGrid (1, 2, ...). Đọc bằng njin::tilemap_get(), ví dụ ô nước là 2 |
| Object (Tiled) / entity (LDtk) | Entity có njin::transform ở **tâm** object, và njin::level_object |
| Tile object (Tiled), entity có ô hiển thị (LDtk) | Có thêm njin::sprite |
| Image layer (Tiled) | Một sprite |

Thứ tự vẽ theo editor: layer dưới cùng ở njin::sprite::layer bằng `level_desc::layer_base`, mỗi
layer phía trên cao hơn một.

Tileset có lề (margin, padding) và khoảng cách giữa ô (spacing), ô lật ngang và dọc, độ trong
suốt và vị trí lệch (offset) của layer đều được giữ đúng. Ảnh tileset mặc định lấy mẫu
`filter_nearest` cho pixel art sắc nét (đổi bằng `level_desc::filter`).

## Object và prefab

Mỗi object mang njin::level_object:

| Trường | Tiled | LDtk |
|---|---|---|
| `name` | Name | `iid` |
| `type` | Class (hoặc Type ở bản cũ) | Tên entity |
| `size` | Width, Height | Width, Height |
| `props` | Custom properties | Fields |
| `points` | Polygon, polyline (so với vị trí object) | |

**Có prefab trùng tên với `type` thì entity được dựng bằng prefab đó** (xem @ref prefabs).
njin::level_object được gắn *trước* khi hàm dựng chạy, nên hàm dựng đọc được thuộc tính:
đặt `value = 5` cho đồng xu trong editor, hàm dựng đọc `obj.props["value"].int_or(1)`.
Thuộc tính là njin::json_value (xem @ref json): chuỗi, số, bool, và cả object lồng nhau như
field Point của LDtk (`props["target"]["cx"]`).

Không có prefab thì object vẫn là entity có transform và njin::level_object. Tìm nó bằng
njin::level_find(), theo tên rồi theo lớp: tiện cho điểm xuất hiện, điểm camera, vùng kích hoạt.

## Vật cản

Layer là vật cản nếu tên của nó nằm trong `level_desc::solid_layers`. Để trống danh sách thì
dùng quy tắc mặc định: layer có thuộc tính `solid = true` (Tiled), hoặc tên chứa "collision",
"collide", "solid" hay "wall".

| Loại layer | Thành |
|---|---|
| Layer ô, IntGrid | njin::collider `collider_tiles`: mọi ô không trống chặn njin::collision_move() và raycast |
| Layer object | Object không có prefab: hình chữ nhật thành collider hộp, elip thành collider tròn |

Collider tạo ra lấy `layer` và `mask` từ `level_desc::solid`. Với LDtk, ô auto-layer vẽ đè lên
IntGrid chỉ để trang trí; va chạm là giá trị IntGrid.

## Nhiều level và unload

- njin::level_load_ldtk() chọn level theo tên; njin::level_list_ldtk() liệt kê các tên.
  `level_desc::use_world_position` đặt level đúng vị trí trong world của LDtk để nạp nhiều
  level liền nhau.
- Mặc định mọi entity của level thuộc scene đang chạy: rời scene thì chúng bị hủy **và ảnh
  của level được giải phóng**. Gọi njin::level_unload() để bỏ một level sớm hơn.
- njin::level_size(), njin::level_origin() cho biết kích thước và vị trí, để giới hạn camera.
- njin::level_properties() trả về thuộc tính của cả bản đồ (Tiled) hay của level (LDtk).

## Chỉ lưới vuông

Bộ nạp level của njin chỉ nhận bản đồ **lưới vuông** (Tiled: Orientation
"Orthogonal"; LDtk luôn là lưới vuông). Bản đồ isometric, staggered hay lục giác của Tiled bị
từ chối: njin::level_load() trả về handle id 0 và ghi lý do vào log, thay vì vẽ sai.

## Chưa hỗ trợ

Những thứ sau có cảnh báo trong log thay vì lỗi im lặng: nén zstd (hãy lưu bằng CSV, zlib hoặc
gzip), ô xoay chéo (vẽ không xoay), tileset gồm nhiều ảnh rời, ô animation, object template của
Tiled, nhiều world trong một project LDtk.

## Hình va chạm và animation của ô

- **Tiled**: đặt thuộc tính chuỗi `collision` (hoặc `shape`, hoặc lớp của ô) trên ô trong tileset:
  `solid`, `none`, `one_way`, `slope_r`, `slope_l`, `slope_r_low`, `slope_r_high`, `slope_l_low`,
  `slope_l_high`. Animation vẽ bằng trình chỉnh animation của tileset và được chạy đúng nhịp.
- **LDtk**: đặt tên giá trị IntGrid theo các tên trên, hoặc gắn enum tag hay custom data lên ô của
  tileset.

Xem @ref platformer.
