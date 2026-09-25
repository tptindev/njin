# Nhập liệu {#input}

njin hỗ trợ **bàn phím**, **chuột**, **tay cầm** và **nhập văn bản**. Có ba cách
dùng, từ trực tiếp đến linh hoạt:

| Cách | Khi nào dùng |
|---|---|
| Hỏi thẳng thiết bị (`key_*`, `mouse_*`, `pad_*`) | Việc nhanh, cố định |
| **Action**: một tên logic, đúng hoặc sai | "Nhảy", "bắn": nút bấm |
| **Axis**: một tên logic, giá trị -1 đến 1 | "Đi ngang", "đi dọc": chuyển động |

Action và axis cho phép đổi phím sau này mà không sửa logic.

## Trạng thái nút

Trạng thái mọi thiết bị được đọc **một lần đầu mỗi frame** và giữ nguyên trong
suốt frame đó, nên mọi system trong một frame thấy cùng một kết quả. Mọi nút
(phím, chuột, tay cầm) dùng chung một quy tắc, ba hàm cho ba câu hỏi:

| Hàm | Đúng khi |
|---|---|
| `*_pressed` | Nút **vừa được nhấn** ở frame này (chỉ một frame) |
| `*_held` | Nút đã được giữ từ frame trước và **vẫn đang giữ** |
| `*_released` | Nút **vừa được thả** ở frame này (chỉ một frame) |

Ví dụ một lần nhấn giữ 3 frame rồi thả:

| Frame | Người dùng | pressed | held | released |
|---|---|---|---|---|
| 1 | chưa ấn | – | – | – |
| 2 | nhấn xuống | ✔ | – | – |
| 3 | vẫn giữ | – | ✔ | – |
| 4 | vẫn giữ | – | ✔ | – |
| 5 | thả ra | – | – | ✔ |

@warning Ở frame nhấn đầu tiên (frame 2), `*_held` trả về **false**. Muốn biết
"nút đang được ấn", dùng `*_pressed(...) || *_held(...)`.

## Bàn phím

njin::key_pressed(), njin::key_held(), njin::key_released() nhận một njin::key_code:
chữ cái, số, F1 đến F12, mũi tên, phím điều hướng, ký hiệu, phím điều khiển (Shift,
Ctrl, Alt, Windows) và bàn phím số.

## Chuột

| Hàm | Trả về |
|---|---|
| njin::mouse_pos() | Vị trí con trỏ, pixel màn hình |
| njin::mouse_delta() | Độ dời so với frame trước |
| njin::mouse_wheel() | Số nấc cuộn, dương là cuộn ra xa người dùng. Đa số frame là 0 |
| njin::mouse_pressed() / njin::mouse_held() / njin::mouse_released() | Nút chuột (njin::mouse_button), cùng quy tắc như phím |

@note njin::mouse_pos() là pixel **màn hình**, không qua camera. Đổi sang vị trí
trong thế giới bằng njin::scr2w(): xem @ref camera.

## Tay cầm

Tay cầm được đánh số từ 0 đến njin::gamepad_max - 1. Tay cầm chưa cắm không gây
lỗi: mọi hàm đọc như không nhấn và trả về 0.

| Hàm | Việc làm |
|---|---|
| njin::pad_available() | Tay cầm có đang cắm không |
| njin::pad_pressed() / njin::pad_held() / njin::pad_released() | Nút tay cầm (njin::gamepad_button) |
| njin::pad_axis() | Trục analog (njin::gamepad_axis), từ -1 đến 1 |
| njin::pad_set_deadzone() | Vùng chết của cần, mặc định 0.15 |

- Tên nút mặt theo **vị trí**: `pad_face_down` là A trên Xbox và Cross trên PlayStation.
- Giá trị trục đã qua vùng chết và được **co giãn lại**: vừa qua vùng chết thì đọc gần 0
  và đạt 1 ở mép, nên không bị nhảy bậc.
- Cò (`pad_axis_left_trigger`, `pad_axis_right_trigger`) nghỉ ở **-1** và đọc 1 khi
  nhấn hết, theo quy ước của raylib.

## Nhập văn bản

Gõ chữ và bấm phím tắt là hai câu hỏi khác nhau. njin::key_pressed() cho biết
**phím nào** được nhấn; njin::text_count() và njin::text_char() cho biết **ký tự nào**
được gõ, đã qua bố cục bàn phím và phím chết. Ô nhập văn bản cần loại thứ hai:

@include input_text_consume.cpp

Mã trả về là Unicode. Mỗi frame nhận tối đa 32 ký tự.

## Consume: nuốt input

njin::key_consume(), njin::mouse_consume() và njin::mouse_wheel_consume() xóa trạng
thái của một nút trong **phần còn lại của frame**. System chạy sau đó không thấy
nút đó nữa. Dùng khi một menu cần nuốt cú nhấp trước khi thế giới bên dưới xử lý
cùng cú nhấp đó (xem ví dụ trên).

Hiệu lực không kéo dài quá frame gọi nó, vì trạng thái được đọc lại ở frame sau.
Muốn system menu chạy trước, đặt nó ở phase sớm hơn (`phase_pre_update`) hoặc dùng
thứ tự `after`/`before`: xem @ref modules_systems.

## Action

Action là một tên logic (ví dụ `"fire"`) gắn với một hoặc nhiều nguồn: phím, nút
chuột, nút tay cầm. **Nguồn nào thỏa cũng làm action thỏa.** Game hỏi về action thay
vì phím, nên đổi phím sau này không phải sửa logic.

| Hàm | Việc làm |
|---|---|
| njin::action_register() | Tạo action. Nếu tên đã có thì trả về action cũ |
| njin::action_find() | Tìm action theo tên |
| njin::action_bind_key() | Gắn thêm một phím |
| njin::action_bind_mouse() | Gắn thêm một nút chuột |
| njin::action_bind_pad() | Gắn thêm một nút tay cầm (bất kỳ tay cầm nào đang cắm) |
| njin::action_clear_binds() | Xóa mọi nguồn đã gắn. Dùng được ở mọi lúc |
| njin::action_pressed() / njin::action_held() / njin::action_released() | Cùng quy tắc như phím |

Đổi phím trong lúc chạy là gọi njin::action_clear_binds() rồi bind lại.

## Axis

Axis đọc giá trị từ **-1 đến 1**. Nó tách khỏi action vì hai thứ trả lời hai câu hỏi
khác nhau: action là đúng hoặc sai, axis là mức độ.

| Hàm | Việc làm |
|---|---|
| njin::axis_register() / njin::axis_find() | Tạo và tìm axis theo tên |
| njin::axis_bind_keys() | Gắn một cặp phím: chỉ phím âm thì -1, chỉ phím dương thì 1, cả hai hoặc không phím nào thì 0 |
| njin::axis_bind_pad() | Gắn một trục tay cầm |
| njin::axis_clear_binds() | Xóa mọi nguồn đã gắn |
| njin::axis_value() | Giá trị hiện tại |

**Nguồn lệch xa vị trí nghỉ nhất thì thắng**: cần đẩy nửa chừng không bị cặp phím
đang đứng yên làm phẳng, và cặp phím đang giữ không bị cần không ai chạm chặn lại.

Không có axis dạng vector 2 chiều. Đọc hai axis rồi ghép lại: chỉ bạn biết có cần
chuẩn hóa vector đó hay không (đi chéo có nhanh hơn không).

## Ví dụ hoàn chỉnh

Di chuyển bằng phím hoặc cần trái, bắn bằng phím cách, chuột trái hoặc nút A:

@include input_devices.cpp

Xem thêm ví dụ action đơn giản hơn:

@include input_actions.cpp

## Danh sách phím và nút

Mọi mã nằm trong njin::key_code, njin::mouse_button, njin::gamepad_button và
njin::gamepad_axis.
