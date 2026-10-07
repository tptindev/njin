# Thế giới ngoài trời 3D {#world_3d}

Trang này dựng một vùng đất ngoài trời: địa hình đồi núi có va chạm, cỏ đung đưa theo gió, đá và cây rải
theo luật, hồ hay biển có sóng và vật nổi, bầu trời theo giờ trong ngày, mây, mưa, tuyết và sương. Mọi
thứ khai báo trong `njin_world3d.h`, chạy trên OpenGL 3.3 như phần 3D còn lại, và nhận ánh sáng, bóng đổ,
đèn và sương mù của @ref graphics_3d.

Cần biết trước: @ref graphics_3d (begin_3d(), ánh sáng, vật lý 3D).

@image html world_3d.png "Địa hình (lớp cỏ và lớp đá theo độ dốc), đá rải, hồ có sóng Gerstner, bọt ở bờ, thùng nổi và bầu trời có mây"

## Địa hình {#terrain3d}

terrain3d_create() dựng một lưới độ cao vuông: `resolution` x `resolution` mẫu phủ hình vuông cạnh `size`
mét, góc nhỏ nhất ở `origin`. Độ cao lấy từ một trong ba nguồn:

| Nguồn | Trường | Ghi chú |
|---|---|---|
| Mảng của game | `heights` | Mét, theo hàng (z) rồi cột (x) |
| Ảnh độ cao | `heightmap` | Ảnh xám (kênh đỏ) hoặc file `.r16`/`.raw` 16 bit. Ảnh 8 bit chỉ có 256 bậc: đặt `smooth` |
| Nhiễu | `noise` | njin::noise_desc của @ref procgen, tính theo mét; kéo giãn 0..1 rồi nhân `height_scale` |

```cpp
const njin::terrain3d_handle ground = njin::terrain3d_create(ctx, {.origin = {-256, 0, -256},
                                                                   .size = 512.0f,
                                                                   .resolution = 513,
                                                                   .height_scale = 45.0f});
```

**Lớp bề mặt.** Tối đa 4 lớp (njin::terrain3d_layer): mỗi lớp một ảnh lặp lại theo vị trí thế giới (mỗi ô
`tile` mét) hoặc một màu trơn, có normal map nếu muốn. Bật `auto_splat` (mặc định) thì engine tự tính lớp
nào phủ chỗ nào theo quy tắc của lớp: khoảng độ cao (`min_height`, `max_height`) và khoảng độ dốc
(`min_slope`, `max_slope`), mép mờ trong `blend`. Lớp sau đè lên lớp trước, nên lớp 0 là lớp phủ rộng nhất
(cỏ), rồi đá ở chỗ dốc, tuyết trên cao. Tắt `auto_splat` thì dùng ảnh `splatmap` (kênh đỏ, lục, lam, alpha là
lớp 0 đến 3). Chỗ dốc đứng ảnh được chiếu từ ba phía nên vách đá không bị kéo dài.

**Mức chi tiết.** Lưới được chia thành các mảnh `chunk_quads` x `chunk_quads` ô. Mảnh ngoài tầm nhìn bị bỏ;
mảnh xa hơn `lod_distance` vẽ với một nửa số ô mỗi cạnh, xa gấp đôi nữa thì một nửa nữa, tới `lod_levels`
mức. Mép mỗi mảnh có "váy" thả xuống, nên hai mảnh khác mức chi tiết không để lộ khe. Pháp tuyến luôn lấy
từ lưới đầy đủ, nên ánh sáng không đổi khi mảnh đổi mức.

**Hỏi địa hình.** terrain3d_height() cho độ cao tại `(x, z)` đúng như lưới vẽ ở mức chi tiết cao nhất và như
body va chạm: cả ba chia mỗi ô vuông thành hai tam giác theo cùng một đường chéo. terrain3d_normal() cho
pháp tuyến, terrain3d_layer_weight() cho biết đang đứng trên lớp gì (đổi tiếng bước chân, bụi khi chạy).

**Va chạm.** Với `collision` (mặc định) địa hình có một body tĩnh là height field của Jolt: nhân vật, xe,
vật động đứng trên nó, raycast chạm nó. terrain3d_body() trả body đó để nhận ra trong kết quả raycast.

**Sửa lúc chạy.** terrain3d_edit() nâng, hạ, san phẳng hay làm mượt một vùng tròn (njin::terrain3d_brush),
terrain3d_paint() vẽ thêm một lớp. Mọi thứ theo ngay: lưới vẽ, body va chạm, lớp tự phủ (đồi đắp cao thành
dốc thì lộ đá), cỏ mọc lại, đá và cây rải trên vùng đó đổi độ cao theo mặt đất. Vật đang nằm trên chỗ đất
được nâng lên cũng được nâng theo, không bị đất mới nuốt mất.

```cpp
// Một quả bom tạo hố: hạ 2 m ở tâm, mép mờ.
njin::terrain3d_edit(ctx, ground, {.kind = njin::terrain3d_lower, .center = blast, .radius = 4.0f, .strength = 2.0f});
```

## Cỏ và vật rải {#grass3d}

grass3d_create() trồng cỏ trên một lớp của địa hình (`layer`, -1 là mọi nơi), không mọc chỗ dốc hơn
`max_slope`, thành đám theo `patchiness`. Mỗi ngọn là vài tam giác vẽ bằng instancing; cỏ chỉ được tạo cho vùng
quanh camera (`draw_distance`), thưa và thấp dần về xa, nên địa hình rộng mấy cũng chỉ tốn cho phần gần.
Cỏ đung đưa theo gió: wind3d_set(), hoặc gió của thời tiết khi vẽ trời bằng draw_sky3d(). Cỏ nhận bóng đổ;
đổ bóng thì bật `cast_shadows` (tốn).

scatter3d_create() rải một model (đá, cây, bụi) lên địa hình theo luật: mật độ, khoảng cách tối thiểu
`spacing`, khoảng độ cao, độ dốc, lớp, cụm theo nhiễu. Vật nghiêng theo mặt đất theo `align`, lún xuống
`sink`, to nhỏ ngẫu nhiên. Chỗ đặt chọn một lần theo `seed`, vẽ bằng draw_instanced3d() theo vùng: bỏ vùng
ngoài tầm nhìn, dùng `far_model` cho vùng xa hơn `lod_distance`. Cây cần va chạm thì lấy vị trí bằng
scatter3d_transforms() rồi tạo body cho từng cây.

```cpp
const njin::scatter3d_handle trees = njin::scatter3d_create(ctx, {.terrain = ground,
                                                                  .model = pine,
                                                                  .far_model = pine_low,
                                                                  .density = 0.004f,
                                                                  .spacing = 6.0f,
                                                                  .max_slope = 25.0f,
                                                                  .patchiness = 0.8f});
```

## Nước {#water3d}

water3d_create() tạo một mặt nước ở độ cao `level`: hồ hình chữ nhật (`center`, `size`), hoặc biển trải tới
chân trời khi `size` là `{0, 0}` (lưới đi theo camera, dày ở gần và thưa ở xa). Mặt nước gồm tối đa 8 sóng
Gerstner (njin::water3d_wave): đỉnh nhọn, đáy bẹt, sóng dài đi nhanh hơn sóng ngắn như sóng nước sâu.

Gắn `terrain` thì nước biết độ sâu từng chỗ: màu từ `shallow_color` tới `deep_color`, chỗ nông trong suốt
thấy đáy (`clarity`), bọt ở bờ (`foam_width`). Nước phản chiếu bầu trời của draw_sky3d() (cả mây), có nắng lấp
lánh và nhận bóng đổ. Nước vẽ sau mọi hình đục, như kính.

water3d_height() và water3d_normal() tính đúng công thức sóng của shader, nên vật trong game đặt theo mặt
nước khớp với hình vẽ. water3d_float() cho một body động nổi: mỗi bước vật lý, phần thể tích dưới mặt sóng
đẩy nó lên (`buoyancy` 1 là lơ lửng, lớn hơn là nổi), nước cản chuyển động và xoay, `flow` kéo theo dòng chảy.

```cpp
const njin::water3d_handle sea = njin::water3d_create(ctx, {.level = 0.0f, .terrain = island});
njin::water3d_float(ctx, sea, raft, {.buoyancy = 1.8f});
```

## Bầu trời và thời tiết {#sky3d}

draw_sky3d() vẽ bầu trời phía sau mọi thứ của lần vẽ 3D (chỉ phủ chỗ chưa có gì): nền theo giờ, mặt trời, mây
trôi theo gió, sao và trăng ban đêm. njin::sky3d đặt giờ (`hour`), vĩ độ và mùa: mặt trời mọc ở phía đông
(`+x`), lên cao nhất lúc 12 giờ ở phía nam (`+z`), lặn ở phía tây. Với `drive_light` (mặc định), draw_sky3d()
đặt luôn ánh sáng của lần vẽ: hướng và màu nắng (vàng cam lúc sớm và chiều, trăng xanh nhạt ban đêm), ánh sáng
nền theo màu trời, màu sương bằng màu chân trời; cài đặt bóng đổ của ánh sáng hiện tại được giữ. Muốn tự lo
thì tắt nó và gọi sky3d_light().

Thời tiết (njin::weather3d) đặt trong `sky3d::weather`: mây che (`clouds`) và độ tối của mây, sương, mưa, tuyết,
gió, độ ướt của mặt đất. Có sẵn năm kiểu (weather3d_preset()): trời quang, u ám, mưa, tuyết, sương mù.
weather3d_lerp() chuyển dần từ kiểu này sang kiểu kia. Mưa và tuyết rơi trong một khối quanh camera, tính
hoàn toàn trên GPU; mỗi hạt giữ chỗ của nó trong thế giới khi camera di chuyển, gió làm mưa rơi xiên.

```cpp
njin::sky3d sky{.hour = 17.5f};
sky.weather = njin::weather3d_lerp(njin::weather3d_preset(njin::weather3d_clear),
                                   njin::weather3d_preset(njin::weather3d_rain), storm);
njin::draw_sky3d(ctx, sky);
```

@image html world_3d_rain.png "Mưa: trời u ám, mặt đất ướt tối và bóng hơn, hạt mưa xiên theo gió"

## Ví dụ đầy đủ

Đồi có cỏ, đá và tuyết; một hồ; thùng gỗ nổi; ngày trôi qua trong hai phút; phím R chuyển mưa; chuột trái
đắp đất.

@include world3d.cpp

## Hiệu năng và giới hạn

Đo trên RTX 3050 Laptop, bản Release, 960 x 540: địa hình 1 km² (1025 x 1025 mẫu, 1 m một mẫu) cùng cỏ, gần
9000 viên đá, hồ, trời và bóng đổ của mặt trời vẽ trong khoảng 1,7 ms mỗi frame. Tạo địa hình đó mất khoảng
0,4 giây (sinh nhiễu, lớp tự phủ, body va chạm), nên tạo lúc nạp màn, không giữa trận.

- Mưa và tuyết rơi cả trong nhà: không có gì che chúng ngoài độ sâu của cảnh.
- Nước không phản chiếu vật trên bờ, chỉ phản chiếu bầu trời. Bờ và độ sâu chỉ tính theo địa hình gắn vào, không
  theo các model khác dưới nước.
- Vật rải được đặt một lần khi tạo; sửa địa hình chỉ đổi độ cao của chúng.
- Mỗi địa hình là một hình vuông; thế giới lớn hơn thì đặt nhiều địa hình cạnh nhau.
