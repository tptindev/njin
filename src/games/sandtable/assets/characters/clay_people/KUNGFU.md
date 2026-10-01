# Kung fu procedural cho rig đất sét

> Bộ này đã được thay bằng [boxing + đá chân](BOXING.md). Nội dung bên dưới
> lưu mô tả phiên bản kung fu trước đó; các enum kungfu đã đổi tên.

Các action mới trong `person.h` dùng cùng rig FK và renderer SDF ray marching.
ID của 13 action cũ được giữ nguyên. Vóc dáng, màu áo và identity vẫn áp dụng.

| Action | Thời gian | Chuyển động |
|---|---:|---|
| `kungfu_guard` | 2.00 s, lặp | Thế thủ, nhịp thở |
| `kungfu_chain` | 1.20 s | Đấm trái rồi phải, xoay thân |
| `kungfu_palm` | 0.95 s | Thu tay, chưởng, thu về |
| `kungfu_front_kick` | 1.15 s | Co gối, đá trước, thu chân |
| `kungfu_roundhouse` | 1.35 s | Co chân, xoay hông và đá vòng |
| `kungfu_block` | 0.90 s | Hai tay đỡ cao, ngả người |
| `kungfu_sweep` | 1.40 s | Hạ trọng tâm, quét chân ngang |

Các đòn bắt đầu và kết thúc ở thế thủ. Nội suy góc bằng smoothstep giữ
chiều dài xương; rig có thêm xoay chân, xoay tay và gập bàn chân. Chiều cao
hông dựa trên chân trụ, chưa phải IK theo địa hình.

```cpp
draw_person(ctx, {.at = position,
                  .facing = 55,
                  .now = act::kungfu_roundhouse,
                  .time = seconds_since_attack,
                  .tint = rgb(177, 68, 54),
                  .identity = 37});
```

Reset `time` về 0 khi bắt đầu đòn. Khi chuyển từ đi/chạy, dùng `was`,
`was_time` và giảm `blend` từ 1 xuống 0 trong khoảng 0.12 s. Sau khi hết
`act_duration`, chuyển về `kungfu_guard` hoặc action di chuyển. Các đòn
không lặp; nếu giữ nguyên action sau thời lượng, nhân vật giữ thế thủ.

## Preview độc lập

```powershell
cmake --build D:/projects/njin/build --target njin_sandtable_kungfu -j4
& 'D:/projects/njin/build/bin/sandtable-kungfu/njin_sandtable_kungfu.exe'
# Thêm --test để ghi 48 PNG vào thư mục làm việc rồi tự kết thúc.
```

Game chính cũng hỗ trợ `--kungfu-preview` và `--kungfu-test` sau khi build lại.
Preview độc lập không phụ thuộc các source gameplay đang sửa.

Ảnh động: [kungfu.gif](generated/kungfu/kungfu.gif). Hàng trước từ trái:
đấm liên hoàn (đỏ), chưởng (xanh lá), đá trước (vàng). Hàng sau: đá vòng
(tím), đỡ cao (xanh dương), quét chân (nâu).

Kiểm tra ngày 01/10/2026: target preview build/link thành công; entry point
game chính compile thành công; phiên renderer xuất đủ 48 frame và thoát mã 0.
Đã kiểm tra trực quan các tư thế đá và bố cục không che nhau. Đây là bộ
animation; chưa gắn thêm hitbox, sát thương hay AI chọn đòn vào gameplay.
Các file Blender/GLB cũ chưa có bộ animation SDF này.
