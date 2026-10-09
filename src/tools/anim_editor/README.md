# njin Animation Editor

Editor animation theo khung xương, viết bằng C++/raylib/Dear ImGui trong njin. Mở một model
glTF đã có rig (mesh có skin, xuất từ Blender, Mixamo...), tạo tư thế cho xương bằng gizmo và
dựng clip trên timeline keyframe, rồi xuất lại `.glb` có đủ model và mọi clip để game nạp bằng
`model_load()`.

Editor không dựng hình và không sửa rig: mesh, skin và cây xương lấy nguyên từ file glTF.

## Chạy

Trên Windows, chạy `run_anim_editor.bat`. Hoặc:

```powershell
cmake --preset debug
cmake --build --preset debug --target njin_anim_editor --parallel
.\build\bin\njin_anim_editor.exe "D:\models\hero.glb"          # mở một model
.\build\bin\njin_anim_editor.exe "D:\models\hero.anim.json"    # mở một dự án
```

Tắt build công cụ bằng `-DNJIN_BUILD_ANIM_EDITOR=OFF` khi cấu hình CMake.

## Mở model

1. Trong **Project**, nhập đường dẫn `.glb`/`.gltf` vào **Model path**, bấm **Open model**
   (hoặc **File > Open model**).
2. Model phải có skin. Editor dùng skin đầu tiên của file, tối đa 128 xương (giới hạn của njin).
3. Các animation có sẵn trong file mở thành clip sửa được. Kênh LINEAR giữ nguyên key; kênh STEP
   và CUBICSPLINE được lấy mẫu thành key ở mỗi khung theo FPS của clip.

Cửa sổ **Skeleton** liệt kê xương theo cây. Viewport vẽ mesh đã skin theo tư thế đang xem, kèm
lớp khớp xương vẽ đè. Chuột phải kéo để xoay, chuột giữa kéo để di chuyển góc nhìn, lăn chuột để
zoom, **F** để lấy toàn cảnh. Bấm vào một khớp để chọn xương.

## Tạo animation

1. Trong **Animation**, bấm **+ Clip**, đặt tên (`Idle`, `Walk`, `Attack`), chỉnh **Duration**
   (giây), **FPS** và **Loop**.
2. Chọn xương (trong Skeleton, trên timeline hoặc bấm khớp trong viewport), đưa playhead tới
   thời điểm muốn đặt tư thế bằng **Time**, thước timeline hoặc **< Frame / Frame >**.
3. Chọn **Rotate** (`E`) hay **Move** (`W`) rồi kéo gizmo, hoặc sửa **Key rotation / Key
   translation** trong Properties. **Mỗi thay đổi tự tạo/cập nhật key ở playhead.** Tư thế gốc
   (rest) của model không bị sửa. Dừng phát trước khi chỉnh.
4. **Add key** (`I`) ghi tư thế hiện tại của xương đang chọn; **Key all bones** ghi mọi xương.
   Kéo hình thoi trên timeline để đổi thời điểm (bắt theo FPS). **Delete key** (hoặc `Delete`
   khi Animation đang focus) xóa key tại playhead.
5. **Play / Pause** hoặc `Space` để phát, **Stop** về đầu clip; **Speed** chỉ đổi tốc độ xem.
6. **Copy** nhân đôi clip, **Delete clip** xóa clip. Mọi thay đổi clip và key đều Undo/Redo được
   (`Ctrl+Z` / `Ctrl+Y`, tối đa 100 bước); một lần kéo gizmo hay kéo key là một bước.

Key translation cộng vào vị trí nghỉ của xương, trong không gian của nút cha; key rotation xoay
thêm sau góc nghỉ. Góc xoay giữa các key được nội suy bằng quaternion theo đường ngắn nhất, vị trí
nội suy tuyến tính. Muốn xoay nhiều vòng, thêm key trung gian cách nhau dưới 180 độ. Trước key đầu
và sau key cuối, xương giữ tư thế của key gần nhất; xương không có key ở tư thế nghỉ.

## Lưu dự án và xuất glTF

- **Project path** + **Save** (`Ctrl+S`) lưu dự án `.anim.json`: đường dẫn model (tương đối so
  với file dự án, nên có thể chuyển cả hai đi cùng nhau) và toàn bộ clip. Key trỏ tới xương theo
  tên. **Open project** (`Ctrl+O`) mở lại; dự án nhắc tới xương mà model không còn có bị từ chối.
  File `.model.json` của Model Editor cũ (SDF) không có rig glTF nên không mở được.
- **GLB path** + **Export glTF** (`Ctrl+E`) ghi một `.glb` gồm mesh, skin, vật liệu, texture (ảnh
  nằm cạnh file `.gltf` được nhúng vào) và mọi clip thành animation glTF: kênh translation và
  rotation cho từng xương có key, nội suy LINEAR, thời gian bằng giây. Kênh mà editor không sửa
  (scale, trọng số morph, nút ngoài skin) của animation cùng tên trong file nguồn được giữ lại.
  Không ghi đè được file nguồn.

Nạp trong game như mọi model khác:

```cpp
auto hero = njin::model_load(ctx, "assets/hero.glb");
const njin::i32 walk = njin::model_anim_find(ctx, hero, "Walk");
// Giữa begin_3d()/end_3d():
njin::draw_model_anim(ctx, hero, {.position = pos}, {.anim = walk, .time = t});
```

**Giới hạn hiện tại:** chỉ skin đầu tiên của file; không key scale; không sửa rig, tên xương hay
trọng số skin; clip CUBICSPLINE/STEP thành key theo từng khung; không có IK trong editor. Viewport
dùng skin CPU của raylib và vẽ không chiếu sáng. Tối đa 64 clip và 10000 key mỗi clip.
ImGuizmo (MIT) nằm trong `third_party/ImGuizmo`, cgltf_write (MIT) trong `third_party/cgltf`.

## Kiểm tra

```powershell
.\build\bin\njin_anim_editor.exe --self-test
.\build\bin\njin_anim_editor.exe --roundtrip-test [model.glb]
.\build\bin\njin_anim_editor.exe --smoke-test [model.glb]
```

Self-test tự sinh một model glTF có rig nhỏ rồi kiểm tra: nạp rig và clip, lấy mẫu, key, JSON dự
án, xuất `.glb` rồi nạp lại, giữ mesh/skin/vật liệu và kênh không sửa; cùng undo/redo, lưu, mở
lại và chặn khi chưa lưu. Round-trip test xuất model (mặc định là model tự sinh với một clip mới)
rồi nạp bằng `model_load()` của njin và so tư thế xương của engine với editor. Smoke test mở
cửa sổ, đặt key cho một xương, kiểm tra hình thay đổi và lưu `build/anim_editor_smoke.png`.
