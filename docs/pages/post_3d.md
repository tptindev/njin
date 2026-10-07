# Hiệu ứng màn hình 3D {#post_3d}

Trang này thêm cho cảnh 3D những hiệu ứng tính trên ảnh của cả khung hình, từ độ sâu của từng pixel: góc
tường và chân vật tối đi (SSAO), sàn bóng phản chiếu (SSR), vết đạn, vết cháy, sơn dán lên mọi bề mặt
(decal), nhòe khi camera quay (mờ chuyển động), tia nắng xuyên qua khe và lóa ống kính. Mọi thứ khai báo
trong `njin_post3d.h` và chạy trên OpenGL 3.3 như phần 3D còn lại.

Cần biết trước: @ref graphics_3d (begin_3d(), ánh sáng, vật liệu). Tia nắng đi cùng bầu trời của
@ref world_3d nhưng không cần nó.

@image html post_3d.png "SSAO ở góc tường và chân vật, sàn bóng phản chiếu khối cầu, khối hộp và cột, vết decal đỏ sẫm trên tường"

## Bật hiệu ứng

njin::post3d gom mọi hiệu ứng; mỗi cái tắt khi độ mạnh của nó bằng 0 (mặc định), nên `post3d{}` vẽ y như khi
không có trang này. Đặt bằng post3d_set(), sửa được mỗi frame (tắt mờ chuyển động khi mở menu, tăng tia nắng
lúc hoàng hôn).

```cpp
njin::post3d fx{};
fx.ssao = 0.8f;
fx.ssr = 1.0f;
fx.shafts = 1.0f;
njin::post3d_set(ctx, fx);
```

| Hiệu ứng | Trường chính | Áp lên |
|---|---|---|
| Che khuất môi trường (SSAO) | `ssao`, `ssao_radius` | Mọi hình đục |
| Phản chiếu (SSR) | `ssr`, cùng `material3d::reflect` của bề mặt | Bề mặt có `reflect` > 0 |
| Decal | decal3d_add() | Mọi hình đục trong hộp của decal |
| Mờ chuyển động | `motion_blur` | Cả ảnh 3D, khi camera di chuyển hay quay |
| Tia nắng | `shafts` | Trời quanh mặt trời, qua khe giữa các vật |
| Lóa ống kính | `flare` | Cả ảnh, khi mặt trời trong khung hình và không bị che |

Hiệu ứng chỉ áp cho lần vẽ 3D vào thế giới, không cho lần vẽ vào render texture (begin_3d() có `target`).
Chúng chạy trong end_3d() theo thứ tự: sau mọi hình đục là decal, SSAO, phản chiếu; rồi kính, nước và hạt 3D;
rồi tia nắng, lóa ống kính và mờ chuyển động. post_fx_set() (@ref post_processing) và shader riêng của game
chạy sau cùng, trên cả ảnh.

## Che khuất môi trường (SSAO) {#post3d_ssao}

Ánh sáng nền (`light3d::ambient`) chiếu đều mọi mặt, nên một khối đặt trên sàn trông như lơ lửng. SSAO xét
quanh mỗi điểm trên màn hình, trong bán kính `ssao_radius` đơn vị 3D, xem có bao nhiêu chỗ bị bề mặt khác che,
rồi làm tối điểm đó theo `ssao`: góc tường, khe giữa hai vật, chân vật trên sàn tối đi, mặt trống thì giữ nguyên.

Mặc định tính ở nửa độ phân giải (`ssao_half`) rồi phóng lên theo độ sâu, nên mép vật vẫn sắc: rẻ gấp bốn và
gần như không khác bản đầy đủ. `ssao_samples` là số mẫu mỗi pixel; ít thì nhanh, nhiều thì mịn.

## Phản chiếu (SSR) {#post3d_ssr}

Bề mặt phản chiếu là bề mặt có `material3d::reflect` lớn hơn 0: đặt bằng material3d_set() cho hình khối, hoặc
`model_material::surface.reflect` cho model. 1 là gương ở mọi góc nhìn; nhỏ hơn thì phản chiếu rõ khi nhìn xiên,
mờ khi nhìn thẳng xuống, như sàn bóng hay mặt nước. `post3d::ssr` nhân với nó cho cả cảnh.

```cpp
njin::material3d marble{};
marble.reflect = 0.5f;
njin::material3d_set(ctx, marble);
njin::draw_plane3d(ctx, {0, 0, 0}, {20, 20}, {0.9f, 0.9f, 0.92f, 1});
njin::material3d_set(ctx, {});
```

Mỗi pixel phản chiếu dò tia phản xạ của nó trên ảnh độ sâu, tới `ssr_distance` đơn vị, và lấy màu chỗ tia gặp
một bề mặt. Vì chỉ có những gì đang thấy trên màn hình, thứ ngoài khung hình hay bị vật khác che không phản
chiếu được: chỗ đó mờ dần về màu sương `light3d::fog_color` (với draw_sky3d() là màu chân trời), theo
`ssr_sky`. `material3d::reflect` một mình không làm gì: không bật `post3d::ssr` thì cảnh vẽ như cũ.

## Decal {#decal3d}

Decal là một ảnh chiếu lên mọi hình đục nằm trong một hình hộp: tường, sàn, model, địa hình, theo đúng hình
của bề mặt (một vết sơn trên quả cầu cong theo quả cầu). decal3d_add() thêm một decal; nó có ở mọi lần vẽ 3D
vào thế giới cho tới khi hết `lifetime` (thời gian của game, dừng khi pause; `fade` giây cuối mờ dần),
bị decal3d_remove() xóa, hay bị decal mới thay chỗ khi quá decal3d_set_max() (mặc định 256, cái cũ nhất đi).

Ảnh nằm trên mặt xz của hộp và chiếu theo trục y: không xoay là chiếu từ trên xuống sàn. `size.y` là độ sâu mà
decal phủ tới: mỏng thì không lan sang vật bên cạnh. Để dán theo một tia chạm (vết đạn chỗ trúng),
decal3d_rotation() cho góc xoay từ pháp tuyến của bề mặt. Không có ảnh thì decal là một vết tròn mềm mép.

```cpp
const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f);
if (hit.hit)
  njin::decal3d_add(ctx, {.position = hit.point,
                          .rotation = njin::decal3d_rotation(hit.normal),
                          .size = {0.25f, 0.2f, 0.25f},
                          .texture = bullet_hole,
                          .lifetime = 30.0f});
```

Hai cách phủ (njin::decal3d_blend): `decal3d_multiply` (mặc định) nhân màu vào bề mặt, chỉ làm tối nhưng giữ
nguyên ánh sáng và bóng đổ của bề mặt, hợp cho vết đạn, vết cháy, máu, bùn; `decal3d_paint` phủ màu lên như sơn,
chiếu sáng theo nắng và ánh sáng nền, cho ký hiệu sáng màu trên nền tối. Bề mặt nghiêng nhiều so với trục chiếu
mờ đi (`angle_fade`), nên vết không bị kéo dài trên mặt bên của vật.

## Mờ chuyển động {#post3d_motion_blur}

Khi camera di chuyển hay quay, mỗi pixel trượt trên màn hình giữa hai frame; `motion_blur` nhòe ảnh theo
hướng đó, 1 là cả quãng trượt của một frame, 0.5 như màn trập máy quay phim. Camera đứng yên thì ảnh giữ nguyên
từng pixel. Chỉ có chuyển động của camera: một vật đang chạy trước camera đứng yên không nhòe. Vệt nhòe dài
nhất là 6% chiều rộng màn hình, nên camera nhảy chỗ (đổi cảnh) không làm ảnh nhòe nát.

## Tia nắng và lóa ống kính {#post3d_sun}

Mặt trời là hướng ngược với `light3d::direction`; draw_sky3d() đặt hướng đó theo giờ trong ngày, nên tia nắng đi
theo mặt trời trên trời. Trời là chỗ không có hình 3D nào (độ sâu xa nhất).

`shafts` làm trời quanh mặt trời tỏa thành tia: mỗi pixel gom ánh trời nằm giữa nó và mặt trời, nên vật che
thành bóng tối xẻ tia, khe giữa các vật thành tia sáng. `shafts_length` là tia dài bao nhiêu phần đường tới mặt
trời, `shafts_color` nhân với màu nắng. `flare` thêm quầng, vòng sáng và các đốm sáng dọc đường từ mặt trời qua
tâm màn hình. Cả hai mờ dần khi mặt trời ra khỏi khung hình (hết hẳn khi ra ngoài một phần tư màn hình), không
có khi mặt trời ở sau lưng camera hay đã lặn; lóa ống kính còn mờ theo phần đĩa mặt trời bị vật che.

@image html post_3d_sun.png "Mặt trời chiều sau cột giữa: tia nắng tỏa qua hai khe, sàn phản chiếu các cột"

## Ví dụ đầy đủ

Một góc phòng có sàn bóng; phím 1, 2, 3 bật tắt SSAO, phản chiếu và mờ chuyển động; chuột trái để lại vết đạn
trên sàn hay tường, mờ dần sau 10 giây.

@include post3d.cpp

## Hiệu năng và giới hạn

Đo trên RTX 3050 Laptop, bản Release, 1280 x 720, cảnh của ảnh trên (trung bình ba lần đo): không hiệu ứng
0,64 ms mỗi frame; SSAO nửa độ phân giải thêm khoảng 0,24 ms (đầy đủ 0,54 ms), phản chiếu 0,41 ms, 50 decal
0,28 ms, mờ chuyển động 0,15 ms, tia nắng 0,25 ms, lóa ống kính 0,14 ms; tất cả cùng 50 decal thêm khoảng 1 ms. Bật bất kỳ hiệu ứng nào
(hay có một decal) thì thế giới được vẽ vào ảnh riêng có độ sâu rồi chép ra màn hình, như khi bật post_fx_set().

- Chỉ có những gì trên màn hình: phản chiếu không thấy thứ ngoài khung hình hay bị che, SSAO không biết thứ sau
  một vật. Mép màn hình là chỗ hai hiệu ứng này yếu nhất.
- Hình SDF (draw_shape3d()), địa hình, nước và cỏ không phản chiếu; chúng vẫn hiện trong phản chiếu của bề mặt
  khác, và vẫn nhận SSAO và decal.
- Kính, nước và hạt 3D vẽ sau decal, SSAO và phản chiếu nên không có chúng; mờ chuyển động của chúng theo độ sâu
  của hình đục phía sau.
- Pháp tuyến tính từ độ sâu, nên đúng đường giao giữa hai mặt (sàn gặp tường) phản chiếu có thể lóe một pixel.
- Mờ chuyển động chỉ theo camera, không theo vật. Không có khử răng cưa theo thời gian (TAA): nó cần rung từng
  frame mọi phép chiếu, kể cả phần 2D vẽ chung ảnh thế giới, và tốc độ của từng vật để không để lại bóng mờ.
- Mỗi lần vẽ 3D vào thế giới đều có hiệu ứng: game vẽ thế giới bằng hai begin_3d() trong một frame thì decal
  được vẽ ở cả hai lần.
