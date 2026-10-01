# Boxing + đá chân — thay bộ kung fu

Giữ renderer SDF đất sét và biến thể procedural. Các slot animation 13–19
giờ dùng bộ boxing: tay thủ cao, nắm đấm đóng, hồi tay nhanh và giữ tay còn
lại bảo vệ khi đá. Không còn chưởng hay quét chân kung fu.

| Action | Giây | Nội dung |
|---|---:|---|
| `boxing_guard` | 1.20, lặp | Thủ cao, nhịp thở |
| `boxing_combo` | 0.72 | Đấm thẳng trái–phải |
| `boxing_hook` | 0.60 | Móc ngang, xoay thân |
| `boxing_front_kick` | 0.78 | Đá trước |
| `boxing_round_kick` | 0.86 | Đá vòng |
| `boxing_block` | 0.55 | Khép hai tay che đầu |
| `boxing_low_kick` | 0.70 | Đá thấp ngang chân |

Truyền action vào `person_draw.now`, thời gian từ lúc bắt đầu đòn vào `time`.
Các đòn kết thúc ở thế thủ. Có thể blend từ action cũ qua `was`, `was_time`,
`blend`. Enum kungfu cũ đã đổi tên; 13 action gốc giữ nguyên.

Game chính: `--boxing-preview` hoặc `--boxing-test`. Cờ kungfu cũ vẫn mở
preview mới. Target độc lập giữ tên build cũ để không đổi đường dẫn công cụ:

```powershell
cmake --build D:/projects/njin/build --target njin_sandtable_kungfu -j4
& 'D:/projects/njin/build/bin/sandtable-kungfu/njin_sandtable_kungfu.exe'
```

Thêm `--test` để xuất 48 frame `boxing_XX.png` vào thư mục chạy.
[Preview mới](generated/boxing/boxing.gif): trước trái→phải là combo, hook,
đá trước; sau trái→phải là đá vòng, che đầu, low kick.

Đây là thay đổi animation, chưa thêm hitbox/sát thương hay AI chọn đòn.
Blender/GLB cũ không được cập nhật bởi bộ rig SDF runtime này.
