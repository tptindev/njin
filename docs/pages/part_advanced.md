# Phần 7: Nâng cao {#part_advanced}

**Mức: nâng cao.** Những trang này không cần cho một game chạy được. Đọc khi game đã có và bạn muốn đẹp hơn, nhanh
hơn, nhiều nội dung hơn, hoặc muốn hiểu engine bên trong. Mỗi trang độc lập, đọc theo nhu cầu.

**Phạm vi:** @ref rendering, @ref shader_advanced và @ref post_processing là **2D**. @ref graphics_3d là
**3D**, trang riêng cho toàn bộ `njin_3d.h` (camera phối cảnh, hình khối, SDF, model, ánh sáng có bóng
đổ, instancing). @ref procgen và @ref debug dùng **chung** cho cả hai.

## Đồ họa

| Trang | Bạn được gì |
|---|---|
| @subpage rendering | Texture, atlas, shader, instancing (hàng nghìn hình bằng một lệnh vẽ), sửa ảnh và shader khi game đang chạy |
| @subpage graphics_3d | Camera phối cảnh, hình SDF mịn, model glTF, ánh sáng có bóng đổ, hạt 3D, instancing, chọn vật bằng tia, gizmo |
| @subpage world_3d | Địa hình có va chạm, cỏ, đá và cây rải theo luật, hồ và biển có sóng, vật nổi, bầu trời theo giờ, mưa, tuyết, sương |
| @subpage post_3d | Góc tường tối đi (SSAO), sàn bóng phản chiếu, vết đạn và sơn dán lên mọi bề mặt (decal), mờ chuyển động, tia nắng, lóa ống kính |
| @subpage video | Phát video có tiếng: cutscene toàn màn hình, màn hình TV trong thế giới 3D |
| @subpage spatial_batch | Chỉ vẽ phần camera thấy của hàng chục nghìn instance, hình mịn ở gần và hình ít mặt ở xa |
| @subpage lighting | Ánh sáng 2D dựa trên vật lý (PBR): đèn điểm, nón, hướng; bóng đổ mềm từ vật chắn hình bất kỳ; normal map, vật liệu MRA, phát sáng, tonemap; bóng từng pixel theo bài của mattdesl |
| @subpage shader_advanced | Shader đọc thêm ảnh (bảng màu, nhiễu), nhận mảng uniform (đèn), chạy trên cả khung hình, nhiều lượt vẽ nối nhau |
| @subpage post_processing | Hiệu ứng toàn màn hình: bloom, CRT, vignette, blur |
| @subpage quality | Chạy được trên máy yếu: mức đồ họa thấp/vừa/cao, vẽ thế giới ở độ phân giải thấp hơn, tự đoán mức theo GPU, lưu cùng cài đặt |

Muốn tự viết shader thì học trước ba bài shader trong @ref learn (bài 10 đến 12).

## Di chuyển

| Trang | Bạn được gì |
|---|---|
| @subpage nav_3d | Quái tự tìm đường quanh tường, lên dốc, qua khe trong màn 3D, đi thành đám đông không đâm vào nhau |
| @subpage spline | Camera chạy trên ray, bục di chuyển, lối tuần tra dọc đường cong mượt, đi đều theo khoảng cách |

## Nội dung

| Trang | Bạn được gì |
|---|---|
| @subpage procgen | Tự sinh bản đồ bằng nhiễu, luật, bo góc và Wave Function Collapse |

## Script

| Trang | Bạn được gì |
|---|---|
| @subpage scripting | Viết luật chơi và hành vi của entity bằng Lua, sửa file khi game đang chạy, gọi hàm qua lại giữa C++ và Lua |

## Mạng

| Trang | Bạn được gì |
|---|---|
| @subpage network | Nhiều máy chơi với nhau qua TCP và UDP: mở phòng, vào phòng, gửi message mà game không bao giờ phải chờ mạng |
| @subpage netcode | Server quyết định, máy khách đoán trước nhân vật của mình, người khác được nội suy mượt, thế giới tự replicate theo tầm nhìn và băng thông |
| @subpage lockstep | Mọi máy chạy cùng một mô phỏng, chỉ gửi lệnh: hàng nghìn thứ chuyển động không tốn byte nào trên mạng, lệch thì biết ngay |
| @subpage multiplayer | Game realtime: kênh tin cậy và không tin cậy trên một kết nối, gói nhỏ theo bit, đọc input ngay trong tick, thử với độ trễ và mất gói giả |

## Công cụ

| Trang | Bạn được gì |
|---|---|
| @subpage debug | njin_inspector: xem FPS, entity, collider, log trong một cửa sổ riêng |

Muốn hiểu engine bên trong thì xem @ref architecture ở @ref part_appendix.
