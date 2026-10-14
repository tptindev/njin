# Chất lượng đồ họa cho máy yếu {#quality}

Một game đẹp trên máy có card rời có thể chỉ chạy 15 FPS trên laptop văn phòng có GPU tích hợp. Thay vì
bắt game tự tắt từng hiệu ứng, njin có một **mức trần chất lượng** (njin::graphics_quality) áp lên mọi
thiết lập đồ họa của game: bóng, hiệu ứng màn hình, ánh sáng 2D, số hạt, LOD, tầm vẽ, và độ phân giải
mà thế giới được vẽ.

@include graphics_quality.cpp

## Game đặt hiệu ứng, người chơi đặt mức trần

Game vẫn gọi light3d_set(), post3d_set(), post_fx_set(), lighting_set() như bình thường, với hình ảnh
đẹp nhất nó muốn. graphics_quality_set() chỉ giới hạn những gì thật sự được vẽ: tắt bóng, tắt SSAO, hạ
độ phân giải bản đồ ánh sáng, bớt hạt. Các hàm `_get` vẫn trả về đúng giá trị game đã đặt, nên:

- code của game không cần `if (máy yếu)` ở khắp nơi;
- đổi mức lúc đang chơi có hiệu lực từ frame sau, đổi lại `quality_high` là mọi thứ về như cũ;
- một game không gọi graphics_quality_set() vẽ y hệt như trước: giá trị mặc định của mọi trường là
  "không giới hạn".

## Ba mức dựng sẵn

graphics_quality_preset() trả về bộ giới hạn của một mức. Lấy về rồi sửa từng trường nếu game muốn khác.

| Trường | `quality_low` | `quality_medium` | `quality_high` |
|---|---|---|---|
| `world_scale` (độ phân giải của thế giới) | 0.7 | 1 | 1 |
| Bóng mặt trời 3D | tắt | tối đa 1024 | như game đặt |
| Bóng đèn điểm và đèn rọi 3D | tắt | tắt | như game đặt |
| SSAO | tắt | tối đa 8 mẫu | như game đặt |
| SSR, TAA, mờ chuyển động | tắt | tắt | như game đặt |
| Tia nắng, lóa ống kính | tắt | như game đặt | như game đặt |
| Bloom, độ sâu trường ảnh (post_fx) | tắt | như game đặt | như game đặt |
| Bản đồ ánh sáng 2D (`lighting_desc::scale`) | tối đa 0.5 | tối đa 0.75 | như game đặt |
| Số hạt (2D và 3D) | 50% | 75% | 100% |
| Ngưỡng đổi LOD của model | ×2.5 | ×1.5 | ×1 |
| Tầm vẽ cỏ, vật rải; khoảng đổi LOD địa hình | 50% | 75% | 100% |

Thứ đắt nhất trên GPU tích hợp thường là **bóng của đèn điểm** (mỗi đèn vẽ lại cảnh sáu lần), rồi tới
**số pixel phải tô**: màn hình 1920×1080 có hai triệu pixel, mỗi pixel chạy shader ánh sáng.

## Vẽ thế giới ở độ phân giải thấp hơn

`world_scale` là cái núm mạnh nhất. 0.7 chỉ còn khoảng một nửa số pixel, 0.5 còn một phần tư. Thế giới
(pha `phase_render`, cả 2D lẫn 3D) được vẽ vào một ảnh nhỏ hơn rồi phóng lên mượt; giao diện
(`phase_post_render`) và chữ vẫn vẽ ở độ phân giải thật của cửa sổ, nên menu và HUD vẫn nét.

Tọa độ không đổi gì: screen_size(), chuột, camera, w2scr() vẫn tính theo màn hình như cũ.

`world_scale` không áp khi game dùng độ phân giải ảo (window_set_virtual_size()): pixel art đã vẽ ở ảnh
nhỏ rồi, thu nhỏ thêm chỉ làm vỡ hình. Nó cũng khác `config::render_scale`: cái kia vẽ **to hơn** màn hình
để khử răng cưa và chỉ đặt được lúc create(); `world_scale` vẽ **nhỏ hơn** và đổi được bất cứ lúc nào.
Đặt cả hai thì chúng nhân với nhau.

## Đoán mức cho lần chạy đầu

graphics_quality_suggest() đọc tên GPU mà driver OpenGL báo và trả về một mức:

- `quality_low`: máy không có GPU thật (renderer phần mềm), Intel HD đời cũ, Intel GMA, và GPU di động
  (Mali đời cũ, Adreno dưới 600, VideoCore của Raspberry Pi, PowerVR);
- `quality_medium`: GPU tích hợp đời mới (Intel UHD, Iris, Radeon Graphics/Vega của APU, Adreno 600 trở
  lên, Mali-G7x);
- `quality_high`: còn lại.

Đây chỉ là gợi ý: tên GPU không nói được máy nhanh tới đâu, và gợi ý nghiêng về phía thấp cho an toàn.
Dùng nó khi chưa có file cài đặt, rồi để người chơi đổi trong menu.

## Lưu cùng cài đặt

Sau lần gọi graphics_quality_set() đầu tiên, settings_save() lưu cả bộ giới hạn dưới khóa `graphics` của
`settings.json`, và settings_load() nạp lại. Game chưa từng gọi graphics_quality_set() thì file cài đặt
không có khóa này. Tên mức hiện trong menu ("Thấp", "Vừa"...) là của game: lưu nó dưới khóa `game` như
trong ví dụ trên.
