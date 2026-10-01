# Nhân vật đất sét SDF trong Sandtable

Bản đang chạy trong game dùng `person.cpp`, không dùng mesh low-poly `.glb`
đã tạo trước đây. Đây là SDF ray marching thực trong renderer njin: rounded
cones, smooth-min, ghi độ sâu điểm chạm, nhận/đổ bóng qua shader SDF của engine.

## Hình và chuyển động

- Rig FK procedural cho 20 `act`; blend góc để giữ độ dài tay/chân.
- 7 action boxing + đá chân: xem [BOXING.md](BOXING.md) và preview riêng.
- Khuỷu tay và đầu gối đi qua một đoạn cong quadratic, chia thành 3 phần SDF.
  Độ nối giữa các phần ngắn được giới hạn để tránh nổi vòng quanh khớp.
- Nhịp thở, xoay thân và độ trễ của tay khi đi; bàn chân chạm đất theo độ cao hông.
- 4 nhóm màu độc lập: da, áo, quần và giày/phụ kiện. `tint` chỉ tô áo.
- Bề mặt mờ, sần nhẹ bằng normal/albedo noise liên tục trong không gian của
  nhóm SDF; nhiễu giảm khi hạt nhỏ hơn pixel để hạn chế nhấp nháy ở góc RTS.
  Đây là shading bề mặt, không phải displacement hay mô phỏng vật liệu mềm.

## Chỉnh bằng code

`person_draw.identity` giữ nhận diện ổn định: 0 là mẫu gốc; seed khác thay
vóc ngang, đầu và màu da. `identity % 4`: 0 đầu trơn, 1 mũ, 2 tóc, 3 túi đeo.

`person_draw.style` có `width`, `head`, `limb`, `softness`, `clay`. Các giá trị
được clamp trong khoảng rig hỗ trợ. `softness = 0` dùng union cứng; `clay = 0`
tắt vân bề mặt. Cùng seed, cùng style và cùng thời điểm cho cùng hình.

```cpp
draw_person(ctx, {.at = position,
                  .facing = 90,
                  .now = act::walk,
                  .time = animation_time,
                  .tint = rgb(177, 68, 54),
                  .identity = 37,
                  .style = {.width = 1.1f, .head = 1.05f,
                            .limb = 1.15f, .softness = 1.0f, .clay = .38f}});
```

## Kiểm tra trực tiếp

Sau khi build `njin_sandtable`:

```powershell
& 'D:/projects/njin/build/bin/sandtable/njin_sandtable.exe' --clay-preview
& 'D:/projects/njin/build/bin/sandtable/njin_sandtable.exe' --clay-test
```

Preview chạy chuyển động liên tục. Test tự kết thúc, ghi vào thư mục chạy:
`clay_sdf_closeup.png`, `clay_sdf_poses.png`, 24 ảnh `clay_sdf_walk_XX.png`.
`clay_sdf_all_actions.png` đã ghi trước khi thêm kung fu, kiểm tra 13 tư thế gốc.
Chế độ này dùng renderer game và không chờ debugger.

Phiên kiểm tra ngày 01/10/2026 đã compile và link thành công; shader biên dịch
trên OpenGL 3.3 / RTX 3050 Laptop. `--clay-test` kết thúc với mã 0 và đã kiểm
tra trực quan ảnh cận, các biến thể và tư thế. `clay_sdf_walk.gif` được ghép
từ 24 frame renderer game, không phải render Blender. Không đo FPS bằng thời
gian test vì việc ghi PNG làm tăng thời gian mỗi frame.

Bản EXE preview riêng cùng ảnh/log kiểm tra đã được dọn để giảm dung lượng.
Build lại game để chạy các chế độ kiểm tra ở trên.

Các file `.blend`/`.glb` cũ là bản nghiên cứu mesh và rig, **không chứa shader
ray marching này**. GLB thông thường không lưu được renderer SDF procedural.
Muốn trao đổi mesh sang Blender cần một bước lấy mặt đẳng trị/bake riêng.

Giới hạn hiện tại: FK stylized, chưa có IK khóa chân vào địa hình hay va chạm
tay/chân; bề mặt procedural chưa có UV theo biến dạng; tư thế cầm súng chưa có
model vũ khí. Chi phí SDF theo số pixel và số phần, nên không coi preview một
người là bằng chứng hiệu năng toàn bộ đám đông.
