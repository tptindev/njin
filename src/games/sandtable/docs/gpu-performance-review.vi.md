# Review tải GPU — 2026-10-02

Review tĩnh code game, renderer của njin và dữ liệu GLB hiện tại. Chưa đo GPU timing trên máy người dùng; thứ tự dưới đây là ưu tiên điều tra/sửa, không phải tỷ lệ thời gian GPU đã được đo. Không thay đổi runtime hay export asset trong review này.

Người dùng xác nhận: card rời 4 GB VRAM, độ phân giải mặc định, GPU khoảng 50% cả khi tắt/bật live; zoom ra xa làm tải giảm. Vì thế ưu tiên điều tra hình học/LOD và shadow của cảnh chính. Live và độ phân giải cao là chi phí tiềm năng, chưa phải nguyên nhân chính của trường hợp đang báo. Chưa biết model GPU; 4 GB chỉ là dung lượng bộ nhớ, không xác định năng lực xử lý.

## Kết luận

Cảnh MVP đã có chi phí render đáng kể: kit retro nhiều tam giác/material, vùng chi tiết rộng, shadow pass mỗi frame, hậu kỳ luôn bật và có thể thêm hai cảnh camera live. Instancing đã được áp dụng nhưng chỉ giảm overhead gọi vẽ; mỗi bản sao vẫn cần xử lý hình học. GPU utilization 50% riêng lẻ chưa đủ kết luận có lỗi hoặc xác định bottleneck; cần GPU, độ phân giải, FPS và thời gian GPU từng pass.

## 1. Hình học kit retro và phạm vi LOD — ưu tiên cao

Đếm trực tiếp primitives tam giác trong GLB (index accessor count / 3, hoặc vertex count / 3 cho primitive không indexed):

| Asset | Tam giác nguồn | Material thực sự được dùng |
| --- | ---: | ---: |
| Balcony, cả 3 style | 35.652 | 6 |
| Indochine/ArcWindow_R2_A45 | 28.050 | 5 |
| Modern/Shopfront | 17.708 | 7 |
| Indochine/Window | 10.332 | 5 |
| Modern/Window | 5.076 | 4 |
| Modern/Wall | 236 | 2 |
| Modern/RoofSlope | 1.060 | 1 |
| MiQuangSpace | 27.040 | 15 |
| HuTieuSpace | 24.904 | 14 |
| person.glb | 13.744 | 2 |

Đây là tổng hình học của file nguồn, chưa phải lượng tam giác thực tế mỗi frame: runtime có thể tách thân và cánh cửa thành các batch riêng. Ví dụ 100 ban công nguyên mảnh tương đương 3,565 triệu tam giác cho một lần vẽ, chưa tính bóng đổ.

`city/render_lod.cpp:148–149` giữ chi tiết nhà tới 750 world units = 125 m, đạo cụ tới 800 = 133,3 m khi camera gần. LOD hiện tại chủ yếu bỏ facade/props và thay khối nhà; các mảnh mái vẫn dùng mesh gốc (`city/render_pbk.cpp:470`, `city/render_kit.cpp:610`). Chưa có các cấp mesh ít tam giác cho những mảnh nặng.

Khuyến nghị: tạo LOD mesh cho ban công, shopfront, cửa chớp và cụm quán ăn; bỏ các bevel/nan nhỏ ở xa, dùng normal hoặc hình học đơn giản để giữ phong cách retro. Chọn LOD theo kích thước trên màn hình; camera live thumbnail cần cấp chi tiết thấp hơn camera chính. Đo trước khi đặt ngân sách tam giác toàn cảnh.

## 2. Shadow pass gửi lại toàn bộ batch đã ghi — ưu tiên cao

`render.cpp:139–142` luôn bật directional shadows, 2048×2048, cả ngày và đêm. Renderer `engine/runtime/modules/render3d.cpp:925–958` đi qua commands và gửi lại caster; chưa có bước lọc caster theo shadow frustum/range. `shadow_range` giới hạn phép chiếu bóng, nhưng không loại các instance ngoài vùng đó trước khi gửi geometry. GPU vẫn xử lý vertex trước khi clipping.

Shader bóng mặt trời còn lấy mẫu PCF 3×3, tối đa 9 shadow texture fetch cho fragment nhận bóng (`render3d.cpp:105–120`). Instanced model được vẽ theo từng mesh/material trong cả shadow và color pass (`render3d.cpp:1244–1335`).

Khuyến nghị: lọc caster theo light frustum, dùng caster đơn giản cho chi tiết nhỏ; thêm quality preset shadow 1024/2048 và đánh giá giảm/tắt bóng directional vào ban đêm. Giảm resolution chỉ giảm phần raster, không giải quyết số vertex gửi thừa.

## 3. Camera live có thể thêm hai cảnh/frame — ưu tiên cao khi bật

`feeds.cpp:21–22` cho phép hai feed cập nhật mỗi frame, mỗi cảnh vẽ lại city, props, crowd và gang. Feed mặc định 320×180 nên giảm chi phí fragment nhưng vẫn có chi phí geometry và animation. Shadow mặt trời đã tắt cho feed.

`city/render_lod.cpp:159–169` dùng vùng vuông quanh nhân vật, reach 260 world units = 43,3 m và đặt cả detail/prop radius bằng reach. Đây chưa phải frustum culling theo hướng camera cho từng instance; batch ở sau camera hoặc ngoài cạnh ảnh vẫn có thể được gửi. Renderer có frustum culling cho model riêng, nhưng nhánh instanced không thực hiện bước tương tự. Không có occlusion culling theo nhà che khuất.

Khuyến nghị: cập nhật feed theo nhịp 10–15 Hz thay vì bám FPS chính, ưu tiên feed đang hiển thị, dùng LOD riêng và culling theo frustum. Feed tắt mặc định nên mục này không giải thích tải khi người dùng chưa bật feed.

## 4. Hậu kỳ và độ phân giải — ưu tiên trung bình, dễ thử A/B

`render.cpp:251` luôn bật bloom 0,55. Engine `post_fx.cpp::post_chain_run` thực hiện 5 pass bloom ở nửa chiều rộng/cao và 1 pass compositing ở full resolution: tổng 6 pass. Khi focus (`render.cpp:260`), DOF thêm 4 pass: tổng 10. Các pass không có cùng độ phân giải hay cùng chi phí; không được hiểu là 10 lần vẽ toàn bộ cảnh.

`main.cpp:159–171` render ở độ phân giải cửa sổ thực tế, resize được, chưa có render scale riêng cho 3D. Tăng từ 1280×720 lên 3840×2160 làm số pixel tăng 9 lần; phần vertex không tăng tương ứng. FPS đã giới hạn 60 và engine gọi SetTargetFPS, nên không có căn cứ quy lỗi cho FPS không giới hạn.

Khuyến nghị: preset tắt DOF/bloom, render scale 0,75 hoặc scene render resolution có giới hạn và HUD riêng; so sánh GPU ms từng cấu hình.

## 5. Point lights, nhân vật và material — tải cộng thêm

Ban đêm có tối đa 12 đèn đường + 3 đèn cửa sổ trong mode quan sát. Mode immersive/cutaway có ngân sách đèn đường nhỏ hơn và tối đa 4 đèn nội thất. Những đèn này không đổ bóng. Shader forward lặp qua tất cả lightCount cho từng fragment, kiểm tra radius rồi mới bỏ qua (`render3d.cpp:173`); chưa chia light list theo object/tile.

`crowd.cpp:16` sinh 800 cư dân cộng khách ngồi quán. Không phải tất cả đều đang hiện; code lọc vị trí/inside/khoảng cách, renderer cull model riêng. Tuy nhiên người hiện dùng mesh 13.744 tam giác, animation riêng (`person.cpp:98`), chưa có LOD nhân vật ít polygon hoặc batch skinned instancing. Cần đo số người thật sự vẽ trước khi xếp đây là bottleneck chính.

Kit được merge theo material (`city/pbk_render.cpp:157`), không thành một GL draw duy nhất cho cả model. Asset quán ăn có 14–15 material làm tăng số draw cho mỗi batch. Cần giảm material slots/atlas nơi phù hợp.

Kính phản chiếu hiện tại chỉ là highlight/rim theo trời, không có SSR, ray tracing hay scene capture. Kính trong suốt gần camera dùng model cache riêng, có thể tăng VRAM do geometry trùng, và thêm alpha blending; nhưng phạm vi 8 m đã giới hạn. Chưa có bằng chứng kính là nguyên nhân chính của GPU 50%.

## 6. Đo lường hiện tại chưa đủ để xác định thủ phạm

Log test gần đây ghi district view khoảng 21,69 ms/frame và night-near 24.042 instance submissions. Đây là frame time toàn chương trình và số instance được gửi, không phải GPU ms hoặc số object duy nhất.

Log “0 draw calls, 0 instanced” lấy ở update (`game.cpp:134`), trong khi stats reset ở đầu frame (`engine/runtime/modules/sprite.cpp:186`) và chưa tích lũy render frame đó. Không dùng số 0 này làm bằng chứng tối ưu tốt. Counter instanced_calls cũng đếm command, không đầy đủ từng GL draw/material; nên thêm counter mesh draws thực tế.

Lần đo tiếp theo cần: CPU update/render-submit ms; GPU timer cho shadow, main color, mỗi feed, post; triangles/vertices được gửi theo pass; GL draw thực tế; số người/đèn đang vẽ; resolution và FPS. GPU timestamp/timer phải đọc kết quả bất đồng bộ để tránh tạo stall trong benchmark.

So sánh cùng seed, giờ game, camera và độ phân giải trên Release: baseline; tắt shadow; tắt bloom/DOF; tắt feed; giảm detail radius; ẩn riêng balcony/shopfront/food props; ẩn crowd. Sau đó so sánh ngày/đêm và 720p/độ phân giải thường dùng. Dùng GPU ms thay vì chỉ nhìn utilization; kết quả sẽ xác định geometry, fill-rate/shading hay CPU submission là giới hạn thực sự.

## Thứ tự xử lý đề xuất

1. Sửa thời điểm lấy counters và bổ sung GPU timings để có baseline đáng tin.
2. Culling caster/instance và LOD cho các module nặng, nhất là camera thumbnail.
3. Giảm nhịp camera live, thêm quality/render-scale và hậu kỳ tùy chọn.
4. Nếu phép đo xác nhận: tối ưu light lists/materials và LOD nhân vật.
