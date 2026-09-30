# njin Model Editor

Editor tạo model SDF và khung xương, viết bằng C++/raylib/Dear ImGui trong njin.
Tham khảo cách tổ chức primitive, phép CSG và bảng thuộc tính của dự án Godot
`SDF Editor` được cung cấp; không sao chép mã nguồn, shader hay icon của dự án đó.

## Chạy

Trên Windows, chạy `run_model_editor.bat`. Hoặc:

```powershell
cmake --preset debug
cmake --build --preset debug --target njin_model_editor --parallel
.\build\bin\njin_model_editor.exe
# Mở một dự án có sẵn:
.\build\bin\njin_model_editor.exe "D:\models\character.model.json"
```

Tắt build công cụ bằng `-DNJIN_BUILD_MODEL_EDITOR=OFF` khi cấu hình CMake.
Editor dùng chung thư viện ImGui với UI Editor, nhưng chạy trong cửa sổ riêng.

## Tạo hình

1. Bắt đầu với nhân vật mẫu, hoặc **File > New empty model**.
2. Trong **Scene**, chọn **+ Shape**: Sphere, Box, Capsule, Cylinder hoặc Torus.
3. Chọn khối rồi chỉnh Position, Rotation, Radius, Height hoặc Half extents trong
   **Properties**. Với Box, Half extents là nửa kích thước; với Capsule, Height là
   khoảng cách giữa tâm hai đầu cầu, tổng chiều cao bằng Height + 2 × Radius.
4. Chọn phép **Union**, **Smooth union**, **Subtract** hoặc **Intersect**. Smooth
   union có tham số Blend để làm mềm chỗ nối. Phép tính chạy lần lượt từ trên xuống;
   khối đầu tiên đang hiện là khối nền. **Move up/down** thay đổi thứ tự phép tính.
5. **Duplicate** nhân đôi khối; checkbox ẩn/hiện khối. Clay color đổi màu xem trước.

Viewport: chuột phải kéo để xoay, chuột giữa kéo để di chuyển góc nhìn, lăn chuột
để zoom. Nhấn **F** khi trỏ vào viewport để lấy toàn cảnh; Front/Side/Top chuyển
góc nhìn. Bấm khối để chọn, kéo đầu trục đỏ/xanh lá/xanh dương để di chuyển theo
trục tọa độ của cha. Khối cắt cũng chọn được theo hình gốc của nó.

Viewport mặc định vẽ SDF trực tiếp trên GPU, không phải dựng lại lưới khi xoay
xương. **Quality: Fast (50%)** phù hợp GPU tích hợp; **Full (100%)** cho ảnh sắc
nét hơn. **Wire** chuyển sang xem lưới dựng nền; Mesh detail tăng độ mịn của lưới.
Wire tạm không áp dụng khi bật xem trước animation. Chất lượng viewport không
ảnh hưởng độ chi tiết của OBJ xuất ra.

## Tạo khung xương và tư thế

1. **+ Root bone** tạo xương gốc. Chọn xương, **+ Child** tạo xương con ở đầu xương cha.
2. Chỉnh **Rest position**, **Rest rotation**, **Bone length**; chọn Parent để đổi
   cha. Editor chặn việc tạo vòng lặp. Đổi cha giữ tọa độ cục bộ, nên xương có thể
   đổi vị trí trên thế giới.
3. Chọn khối và chọn **Attach to bone**. Khối giữ vị trí/hướng ở tư thế nghỉ khi
   đổi xương gắn; tọa độ trong Properties sau đó tính theo xương đó.
4. Bật **Pose preview** và sửa **Pose rotation** của xương. Các xương con và khối
   gắn vào chúng chuyển động theo. **Reset all poses** đưa tư thế về trạng thái nghỉ.
5. Có thể chọn xương bằng cách bấm khớp trong viewport. **Bones** bật/tắt lớp khung
   xương được vẽ đè để nhìn rõ cả khi xương nằm trong model.

Bone length điều chỉnh chiều dài hiển thị và vị trí mặc định khi tạo xương con;
không tự thay đổi hình khối đã gắn hoặc vị trí của xương con hiện có. Xóa một
xương sẽ xóa cả nhánh con và tháo các khối khỏi nhánh đó, giữ chúng ở vị trí nghỉ.

## Tạo animation ngay trong editor

1. Trong cửa sổ **Animation**, bấm **+ Clip**, đặt tên như `Idle`, `Walk` hoặc
   `Attack`, chỉnh **Duration** (giây), **FPS** và **Loop**. Clip mới ghi tư thế
   hiện có ở giây 0 cho các xương. Với nhân vật mẫu có thể bấm **Wave demo** để
   thử ngay một animation vẫy tay.
2. Bật **Preview**, chọn xương trong Scene hoặc trên một hàng timeline. Di chuyển
   playhead đến thời điểm muốn tạo tư thế bằng thanh **Time**, thước timeline
   hoặc **< Frame / Frame >**.
3. Chọn **Rotate** (`E`) hoặc **Move** (`W`) trong viewport rồi kéo gizmo
   ImGuizmo. Cũng có thể sửa **Key rotation / Key translation** trong Properties.
   **Các thay đổi này tự tạo/cập nhật keyframe ở thời điểm đang chọn.** Chúng không
   sửa tư thế nghỉ của model. Dừng phát trước khi chỉnh.
4. **Add key** (`I`) ghi lại tư thế hiện tại của xương đã chọn; **Key all bones**
   ghi toàn bộ tư thế. Bấm hình thoi để chọn key; kéo hình thoi để đổi thời điểm,
   có bắt theo FPS. Editor không cho thả đè lên một key khác của cùng xương.
   **Delete key** (hoặc `Delete` khi Animation đang focus) xóa key tại playhead
   của xương đang chọn.
5. **Play / Pause** hoặc `Space` phát/dừng; **Stop** trở về đầu clip. **Speed**
   chỉ đổi tốc độ xem trước. Loop tắt thì phát đến tư thế cuối và dừng. Có thể xem
   và sửa key ở đúng cuối clip kể cả khi bật Loop.
6. **Copy** nhân đôi clip, **Delete clip** xóa clip. Các thao tác sửa key và clip
   đều hỗ trợ Undo/Redo; một lần kéo gizmo hoặc kéo key là một bước. Việc phát và
   tua playhead không làm bẩn dự án và không tạo lịch sử hoàn tác.

Timeline cuộn dọc khi có nhiều xương. Nếu bố cục cũ chưa có cửa sổ Animation,
chọn **Window > Reset layout**. `W`/`E` dùng khi trỏ vào viewport; `I` và `Space`
dùng trong Animation hoặc viewport khi không nhập văn bản.

Góc xoay giữa các key được nội suy bằng quaternion theo đường ngắn nhất; vị trí
được nội suy tuyến tính. Muốn xoay nhiều vòng, thêm các key trung gian cách nhau
dưới 180 độ. Trước key đầu và sau key cuối, xương giữ tư thế tại key gần nhất;
muốn Loop liền mạch, đặt tư thế đầu và cuối giống nhau. Không thể rút Duration
ngắn hơn thời điểm key cuối; hãy di chuyển hoặc xóa key cuối trước.

Khi Preview bật, Properties khóa các thuộc tính tư thế nghỉ của xương để tránh
sửa nhầm. Tắt Preview để thay đổi rig. Khi xóa một nhánh xương, các key của nhánh
đó cũng được xóa, còn track của các xương khác được cập nhật chỉ số.

## Lưu, hoàn tác và xuất model

- **Project path**: nhập đường dẫn `.model.json`, **Save** / `Ctrl+S` để lưu,
  **Open** / `Ctrl+O` để mở. Dự án giữ các khối, thứ tự CSG, màu, cây xương,
  phép gắn, các góc pose và toàn bộ clip/keyframe. File lỗi được từ chối mà không
  thay model đang sửa. File phiên bản 1 vẫn mở được; khi lưu sẽ thành phiên bản 2.
- `Ctrl+Z` / `Ctrl+Y`: hoàn tác / làm lại, giữ tối đa 100 bước. Một lần kéo hoặc
  nhập thuộc tính là một bước. `Ctrl+D`: nhân đôi khối; `Delete`: xóa mục chọn.
- **OBJ path**, **Export detail**, **Export OBJ**: xuất lưới tam giác có pháp tuyến
  ở tư thế/khung animation đang xem. Detail 64–96 phù hợp để xuất.
  Trục đứng là +Y, đơn vị tọa độ được giữ nguyên.

Nạp OBJ trong game qua API hiện có:

```cpp
auto model = njin::model_load(ctx, "assets/character.obj");
// Giữa begin_3d()/end_3d():
njin::draw_model(ctx, model, {}, njin::colors::white);
```

**Giới hạn hiện tại:** OBJ là model tĩnh, không chứa rig, animation, texture hay
màu xem trước. Rig vẫn chỉnh sửa được trong JSON; engine chưa có loader cho định
dạng dự án này. Cơ chế pose biến đổi cứng từng khối theo một xương rồi ghép SDF,
chưa có skin weights, IK hoặc xuất glTF có xương. Animation hiện được tạo và phát
trong editor, chưa có API engine để phát trực tiếp file dự án. Tối đa 128 khối,
128 xương, 64 clip và 10000 key/clip. File JSON của editor Godot tham khảo chưa
được nhập trực tiếp. ImGuizmo được tích hợp kèm giấy phép MIT trong
`third_party/ImGuizmo`; không cần cài thêm thư viện để dùng gizmo.

## Kiểm tra

```powershell
.\build\bin\njin_model_editor.exe --self-test
.\build\bin\njin_model_editor.exe --smoke-test
.\build\bin\njin_model_editor.exe --animation-smoke-test
```

Self-test kiểm tra JSON round trip, dữ liệu không hợp lệ, chu trình xương,
transform khi pose/đổi gắn/xóa xương, CSG, mặt lưới, pháp tuyến và OBJ. Smoke-test
mở cửa sổ, kiểm tra shader SDF, lưu `build/model_editor_smoke.png` và tự thoát.
Animation smoke-test so sánh ảnh render ở hai tư thế và kiểm tra playback không
thay dữ liệu/lịch sử, lưu thêm `build/animation_rest.png` và `build/animation_key.png`.
