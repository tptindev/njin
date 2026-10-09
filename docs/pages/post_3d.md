# Hiệu ứng màn hình 3D {#post_3d}

Trang này thêm cho cảnh 3D những hiệu ứng tính trên ảnh của cả khung hình, từ độ sâu của từng pixel: góc
tường và chân vật tối đi (SSAO), sàn bóng phản chiếu (SSR), vết đạn, vết cháy, sơn dán lên mọi bề mặt
(decal), nhòe khi camera quay (mờ chuyển động), tia nắng xuyên qua khe, lóa ống kính và khử răng cưa theo thời
gian (TAA). Mọi thứ khai báo
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
| Mờ chuyển động | `motion_blur` | Cả ảnh 3D, khi camera di chuyển hay quay; mesh và model đang chạy |
| Tia nắng | `shafts` | Trời quanh mặt trời, qua khe giữa các vật |
| Lóa ống kính | `flare` | Cả ảnh, khi mặt trời trong khung hình và không bị che |
| Khử răng cưa theo thời gian (TAA) | `taa`, `taa_sharpen` | Lần vẽ 3D đầu tiên vào thế giới của mỗi frame |

Hiệu ứng chỉ áp cho lần vẽ 3D vào thế giới, không cho lần vẽ vào render texture (begin_3d() có `target`).
Chúng chạy trong end_3d() theo thứ tự: sau mọi hình đục là decal, SSAO, phản chiếu; rồi kính, nước và hạt 3D;
rồi TAA, tia nắng, lóa ống kính và mờ chuyển động. post_fx_set() (@ref post_processing) và shader riêng của game
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

Khi camera di chuyển hay quay, hay một vật tự chạy, mỗi pixel trượt trên màn hình giữa hai frame; `motion_blur`
nhòe ảnh theo hướng đó, 1 là cả quãng trượt của một frame, 0.5 như màn trập máy quay phim. Vật đang chạy trước
camera đứng yên cũng nhòe, và vệt nhòe lan ra nền sát mép nó, như ảnh chụp thật (xem @ref post3d_motion_vectors).
Camera đứng yên và vật đứng yên thì ảnh giữ nguyên từng pixel. Vệt nhòe dài nhất là 6% chiều rộng màn hình, nên
camera nhảy chỗ (đổi cảnh) không làm ảnh nhòe nát.

## Tia nắng và lóa ống kính {#post3d_sun}

Mặt trời là hướng ngược với `light3d::direction`; draw_sky3d() đặt hướng đó theo giờ trong ngày, nên tia nắng đi
theo mặt trời trên trời. Trời là chỗ không có hình 3D nào (độ sâu xa nhất).

`shafts` làm trời quanh mặt trời tỏa thành tia: mỗi pixel gom ánh trời nằm giữa nó và mặt trời, nên vật che
thành bóng tối xẻ tia, khe giữa các vật thành tia sáng. `shafts_length` là tia dài bao nhiêu phần đường tới mặt
trời, `shafts_color` nhân với màu nắng. `flare` thêm quầng, vòng sáng và các đốm sáng dọc đường từ mặt trời qua
tâm màn hình. Cả hai mờ dần khi mặt trời ra khỏi khung hình (hết hẳn khi ra ngoài một phần tư màn hình), không
có khi mặt trời ở sau lưng camera hay đã lặn; lóa ống kính còn mờ theo phần đĩa mặt trời bị vật che.

@image html post_3d_sun.png "Mặt trời chiều sau cột giữa: tia nắng tỏa qua hai khe, sàn phản chiếu các cột"

## Khử răng cưa theo thời gian (TAA) {#post3d_taa}

`taa = true` làm mép xiên của hình 3D mịn như vẽ ở độ phân giải gấp đôi, và mép mảnh (dây, cột xa) thôi nhấp nháy.
Mỗi frame, hình chiếu 3D dịch đi một phần nhỏ của pixel, mỗi frame một chỗ (dãy Halton 2, 3, tám vị trí), nên cùng
một pixel lần lượt thấy mép vật ở những điểm khác nhau. Ảnh của frame được trộn với ảnh đã trộn của các frame trước
(khoảng 10% ảnh mới), sau khi đưa ảnh cũ về đúng chỗ theo độ sâu và chuyển động của camera. Sau khi trộn, ảnh được
làm nét lại một chút (`taa_sharpen`, mặc định 0.25), vì trộn nhiều điểm làm ảnh hơi mềm.

```cpp
njin::post3d fx{};
fx.taa = true;
njin::post3d_set(ctx, fx);
```

Ảnh cũ chỉ được dùng khi còn khớp, nên không để lại bóng mờ:

- Mỗi pixel so màu ảnh cũ với màu chín pixel quanh nó trong ảnh mới; màu cũ nằm ngoài khoảng đó thì bị kéo về.
- Độ sâu của ảnh cũ, quanh chỗ đọc, phải chứa điểm đang vẽ; không thì ảnh cũ ở đó cho thấy vật khác (một vật đã
  chạy đi, hay chỗ vừa lộ ra sau vật), nên bị bỏ.
- Camera nhảy chỗ (đi xa hơn 3 đơn vị hay quay hơn khoảng 25 độ trong một frame), đổi cỡ ảnh, hay tắt TAA rồi bật
  lại, thì ảnh cũ bị bỏ hẳn: frame đầu sau đó là ảnh mới chưa khử.

Chỉ lần vẽ 3D đầu tiên vào thế giới của mỗi frame được khử; 2D vẽ sau end_3d() vẽ lên ảnh đã khử nên không rung.
Vật đang tự chạy (mesh và model, xem mục dưới) được đưa về chỗ cũ theo chuyển động của chính nó, nên mép của nó
cũng được khử; ảnh cũ ở chỗ vừa lộ ra sau vật bị bỏ, nên không để lại vệt.

## Vận tốc của từng vật {#post3d_motion_vectors}

Khi TAA hay mờ chuyển động bật, end_3d() vẽ lại các mesh và model đục một lần nữa (chỉ ghi chuyển động, không
tô màu) để biết mỗi pixel đã ở đâu frame trước: theo transform của lần vẽ đó frame trước và bây giờ, model có xương
thì theo cả tư thế frame trước. Lần vẽ này chỉ chạy một lần mỗi frame, cho lần vẽ 3D đầu tiên vào thế giới.

Engine tự nhận ra lần vẽ nào frame này là lần vẽ nào frame trước:

- Entity có njin::model3d: theo entity.
- Các lần vẽ khác (draw_model(), draw_model_anim(), draw_cube3d()...): theo model hay hình, và thứ tự trong pass.
  Cảnh vẽ theo cùng thứ tự mỗi frame thì tự khớp.
- Thứ tự đổi giữa các frame (danh sách quái bị xóa bớt, sắp lại theo khoảng cách): gọi draw3d_motion_id() ngay
  trước lần vẽ, với một số riêng không đổi của vật, ví dụ số hiệu của nó.

```cpp
for (const enemy &e : enemies) {
  njin::draw3d_motion_id(ctx, e.id); // khác 0, không đổi theo frame
  njin::draw_model_anim(ctx, enemy_model, e.at, e.pose);
}
```

Không có tên thì hai vật cùng model đổi chỗ trong danh sách sẽ bị coi như nhảy sang chỗ của nhau: TAA bỏ ảnh cũ ở
đó, mờ chuyển động nhòe chúng dù đứng yên. Một vật nhảy xa hơn một phần tư màn hình trong một frame (dịch chuyển tức
thời, vật mới xuất hiện ở chỗ của vật cũ) được coi như chỉ có chuyển động của camera.

Hình SDF (draw_shape3d()), draw_instanced3d(), địa hình, cỏ, nước, kính và hạt 3D không có chuyển động riêng: chúng
theo chuyển động của camera, như trước.

## Ví dụ đầy đủ

Một góc phòng có sàn bóng và một hộp chạy vòng quanh quả cầu; phím 1, 2, 3, 4 bật tắt SSAO, phản chiếu, mờ chuyển
động và TAA; chuột trái để lại vết đạn trên sàn hay tường, mờ dần sau 10 giây.

@include post3d.cpp

## Hiệu năng và giới hạn

Đo trên RTX 3050 Laptop, bản Release, 1280 x 720, cảnh của ảnh trên (trung bình ba lần đo): không hiệu ứng
0,64 ms mỗi frame; SSAO nửa độ phân giải thêm khoảng 0,24 ms (đầy đủ 0,54 ms), phản chiếu 0,41 ms, 50 decal
0,28 ms, mờ chuyển động 0,15 ms, tia nắng 0,25 ms, lóa ống kính 0,14 ms; tất cả cùng 50 decal thêm khoảng 1 ms. TAA thêm khoảng 0,3 đến 0,4 ms (kể cả việc vẽ
thế giới vào ảnh riêng và vận tốc của từng vật). Với bốn vật đang chạy và một nhân vật có xương, TAA thêm khoảng
0,35 ms và mờ chuyển động khoảng 0,25 ms. Bật bất kỳ hiệu ứng nào
(hay có một decal) thì thế giới được vẽ vào ảnh riêng có độ sâu rồi chép ra màn hình, như khi bật post_fx_set().

- Chỉ có những gì trên màn hình: phản chiếu không thấy thứ ngoài khung hình hay bị che, SSAO không biết thứ sau
  một vật. Mép màn hình là chỗ hai hiệu ứng này yếu nhất.
- Hình SDF (draw_shape3d()), địa hình, nước và cỏ không phản chiếu; chúng vẫn hiện trong phản chiếu của bề mặt
  khác, và vẫn nhận SSAO và decal.
- Kính, nước và hạt 3D vẽ sau decal, SSAO và phản chiếu nên không có chúng; mờ chuyển động của chúng theo độ sâu
  của hình đục phía sau.
- Pháp tuyến tính từ độ sâu, nên đúng đường giao giữa hai mặt (sàn gặp tường) phản chiếu có thể lóe một pixel.
- Hình SDF, draw_instanced3d(), địa hình, cỏ và nước không có chuyển động riêng (chỉ theo camera): khi chúng tự
  chạy, mép của chúng còn răng cưa dưới TAA và không nhòe. Bóng của vật đang chạy nằm trên sàn đứng yên, nên mép
  bóng dưới TAA hơi mềm. Hình dạng thay đổi do morph cũng không có chuyển động riêng.
- Dưới TAA, mép vật đang chạy nhanh hơi mềm hơn ảnh không khử (ảnh mới được trộn nhiều hơn để không nhòe theo).
- Chỉ lần vẽ 3D đầu tiên vào thế giới của mỗi frame được khử; 2D vẽ vào ảnh thế giới trước begin_3d() được trộn
  cùng ảnh 3D, không rung nhưng có thể hơi mềm khi camera quay.
- Mỗi lần vẽ 3D vào thế giới đều có hiệu ứng: game vẽ thế giới bằng hai begin_3d() trong một frame thì decal
  được vẽ ở cả hai lần.
