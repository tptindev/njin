# Clay people / Người đất sét Sa Bàn Chiến Trận

Model procedural theo ảnh tham chiếu người dùng: đầu trơn đa diện, áo đỏ,
quần xanh, tay chân dài. Các biến thể phục vụ nhận diện cá thể ở góc RTS:
`reference`, `lookout` (mũ), `collector` (túi đeo), `enforcer` (vai rộng/tóc).

## Sản phẩm

- `generated/*.blend`: mesh chỉnh sửa được, rig FK 18 xương, 13 Action và studio.
- `generated/*.glb`: skin + animation + material PBR màu phẳng; không cần texture ngoài.
- `generated/lineup.png`, `poses.png`: ảnh render trực tiếp bằng Blender.
- `generated/walk.gif`: preview loop đi bộ từ rig thật, 20 fps.
- `generated/manifest.json`: thông số, số tam giác, SHA-256 các GLB.
- `generated/validation.json`: kết quả đọc byte GLB, import lại vào Blender,
  kiểm tra skin, trọng số và tọa độ của các pose được lấy mẫu.

Base mesh có 524 tam giác; phụ kiện tăng nhẹ ngân sách. 18 xương, tối đa
2 ảnh hưởng/xuất phát vertex; cạnh cứng làm tăng vertex count sau export.
4 material ở bản cơ bản, tối đa 5 khi có túi. Không dùng asset tải bên ngoài.

## Sinh lại bằng PowerShell

Chạy từ thư mục `src/games/sandtable`:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --background --factory-startup --python-exit-code 1 --python assets/characters/clay_people/generate.py
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --background --factory-startup --python-exit-code 1 --python assets/characters/clay_people/verify.py
```

Sinh 12 cá thể với cùng seed cho cùng thông số hình học:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --background --factory-startup --python-exit-code 1 --python assets/characters/clay_people/generate.py -- --seed 123 --count 12 --output assets/characters/clay_people/crowd_123
```

Hoặc `--config assets/characters/clay_people/example.json --output PATH`.
File cấu hình là mảng object. `name` phải duy nhất; `width` chỉnh vóc ngang,
`head` chỉnh cỡ đầu, `height` tính bằng mét; `shirt/pants/skin` là RGB tuyến
tính 0–1; `accessory` nhận `none`, `cap`, `hair`, `satchel`.
Có thể sửa trực tiếp `Builder.build()` để thêm mesh, gán trọng số vào xương
tương ứng; sửa `animate()` để thêm chuyển động. Mỗi GLB độc lập có đủ clip.
Đầu ra cùng tên sẽ được tạo lại; dùng thư mục output mới để giữ bản cũ.

## Animation / hợp đồng với game hiện tại

Các tên tương ứng trực tiếp bảng `motions` trong `person.cpp`:

| Clip | Giây | Loop |
|---|---:|---|
| Idle_Loop | 2.4 | có |
| Idle_Talking_Loop | 2.4 | có |
| Walk_Loop | 1.0 | có |
| Jog_Fwd_Loop | .7333 | có |
| Sprint_Loop | .5667 | có |
| Punch_Jab | .60 | không |
| Punch_Cross | .75 | không |
| Hit_Chest | .65 | không |
| Death01 | 1.50 | không |
| Sitting_Idle_Loop | 2.4 | có |
| Crouch_Idle_Loop | 2.4 | có |
| Pistol_Idle_Loop | 2.0 | có |
| Pistol_Shoot | .40 | không |

30 fps, thời gian bắt đầu GLB bằng 0, di chuyển tại chỗ, không root motion theo quãng đường. Root có dịch
chuyển đứng/ngồi/ngã; không loại bỏ track root khi import. Rig FK đơn giản,
không IK controller, không ngón tay/biểu cảm mặt. Hai clip pistol chỉ là tư
thế và recoil; không kèm model súng. Action là chuyển động stylized thủ công
bằng code, không phải motion capture.

GLB Y-up, hướng mặt +Z; Blender Z-up, hướng mặt -Y. Bản gốc cao khoảng
1.83 m phù hợp `model_height` hiện có. Bản thay đổi `height` cần dùng đúng
chiều cao trong hệ thống scale của game nếu muốn giữ hoặc chuẩn hóa khác biệt.

Chưa thay model đang dùng của game. Khi tích hợp:

1. Load một trong các GLB mới bằng `model_load` và tìm clip qua tên hiện có.
2. Bỏ vòng ghi đè tất cả màu material trong `person_init`; vòng hiện tại dành
   cho mannequin Quaternius và sẽ phá bảng màu mới.
3. Dùng tint trắng để giữ màu áo/quần/da. Global `person_draw.tint` hiện nhân
   màu toàn thân; muốn chỉ tint áo, cần mở rộng API material theo instance
   hoặc cache các model variant. Tránh đổi material model dùng chung giữa
   hai người mà không có cơ chế khôi phục/batching rõ ràng.
4. Giữ cache model và animation dùng chung, chọn variant theo ID/seed ổn định.
   Điều chỉnh `walk_pace` theo stride khi kiểm tra thực tế trong game.

Đã kiểm tra xuất/import ở Blender và đầu/cuối các clip loop trùng nhau;
chưa kiểm thử renderer njin, blending
trong game, hiệu năng đám đông hoặc tương tác ghế/vũ khí. `game-dev` chưa có
trên PATH nên đây chưa phải canonical package đã verify bởi Game Development
Studio. Blender MCP không kết nối ở phiên tạo; sử dụng Blender CLI cục bộ.

## Nguồn gốc

Nguồn hình dáng: `Create-one-isolated-full-body-character.png` do người dùng
cung cấp. Hình học, rig, clip và material do `generate.py` tạo cục bộ; không
dùng lại model/animation Quaternius trong thư mục `assets/models/person.glb`.
Quyền đối với ảnh tham chiếu không được suy đoán hoặc gán giấy phép mới.
