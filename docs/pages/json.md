# JSON và lưu game {#json}

njin::json_value đọc và ghi JSON: lưu game, file cấu hình, bảng số liệu của game. Đây cũng là
kiểu của thuộc tính đọc từ Tiled và LDtk (xem @ref level).

@include save_json.cpp

## Đọc

- `doc["key"]` và `doc[i]` không bao giờ lỗi: khóa không có, sai kiểu, ngoài mảng đều trả về
  một giá trị null. Đọc sâu bao nhiêu tầng cũng an toàn: `doc["a"]["b"]["c"]`.
- Lấy giá trị kèm dự phòng: `int_or()`, `f32_or()`, `number_or()`, `bool_or()`, `string_or()`.
  Nhờ vậy bản lưu cũ thiếu khóa mới vẫn nạp được.
- Duyệt: mảng qua `items`, object qua `members` (giữ đúng thứ tự trong file).
- `is()`, `has()`, `size()` để kiểm tra.

## Ghi

- `json_value::make_object()`, `json_value::make_array()` tạo giá trị rỗng.
- `set(key, value)` đặt thành viên (thay nếu đã có, vị trí giữ nguyên), `push(value)` thêm vào
  mảng. Cả hai trả về chính giá trị đó, nên gọi nối tiếp được.
- Số nguyên được ghi không có phần thập phân; số thực được ghi ngắn nhất mà vẫn đọc lại đúng
  (`0.1` chứ không phải `0.10000000000000001`).

## File

| Hàm | Việc làm |
|---|---|
| njin::json_load() | Đọc và phân tích file. Lỗi được ghi log kèm vị trí byte |
| njin::json_save() | Ghi file qua một file tạm rồi đổi tên: mất điện giữa chừng không làm hỏng bản cũ |
| njin::json_parse() | Phân tích một chuỗi |
| njin::json_dump() | Viết ra chuỗi, có thụt lề hoặc một dòng |

Lưu game nên nằm trong thư mục của người dùng: njin::save_path() (xem @ref window_files).
