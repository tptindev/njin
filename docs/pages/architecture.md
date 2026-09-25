# Kiến trúc bên trong {#architecture}

Trang này dành cho người muốn hiểu hoặc sửa engine. Nếu chỉ muốn làm game, bạn không cần đọc.

## Hai lớp

```
src/engine/
  api/       header công khai, chỉ có khai báo. Game include njin.h
  runtime/   phần cài đặt. Dùng raylib. Game không thấy
```

**raylib bị giấu hoàn toàn**: header trong `api/` không include raylib. Các hàm
`to_raylib` và `from_raylib` (file `njin2rl` và `rl2njin`) đổi qua lại giữa kiểu
của njin (`vec2`, `rgba`...) và kiểu của raylib. Nhờ vậy game chỉ phụ thuộc vào
`njin::api`, còn raylib là chi tiết cài đặt của `njin::rt`.

## njin_ctx

njin::njin_ctx là một struct **mờ** với game: chỉ dùng qua các hàm. Định nghĩa thật
nằm trong `runtime/njin_ctx_impl.h`:

| Thành viên | Vai trò |
|---|---|
| `dt`, `elapsed` | Thời gian, cập nhật mỗi frame |
| `cfg` | Cấu hình đã truyền vào njin_create() |
| `window` | Mở cửa sổ khi tạo, đóng khi hủy |
| `input` | Trạng thái phím của frame trước và frame này, danh sách action |
| `shader`, `texture`, `render_texture` | Các store tài nguyên GPU |
| `ecs` | Registry, dispatcher và lịch chạy system |

Thứ tự khai báo quan trọng: thành viên bị hủy theo thứ tự **ngược**, và `window`
được khai báo trước các store, nên nó đóng **sau cùng**. Các store giải phóng tài
nguyên GPU trong lúc OpenGL context còn sống.

## Store và handle

`shader_store`, `texture_store` và `render_texture_store` cùng một khuôn:

- Mỗi tài nguyên nằm trong một **slot** của một `std::vector`.
- Handle có `id = chỉ số slot + 1`. Nên `id == 0` luôn là không hợp lệ.
- Slot **không bao giờ được tái sử dụng** sau khi unload. Handle cũ vì thế không thể trỏ nhầm sang tài nguyên mới.
- Destructor của store giải phóng mọi slot còn sống.
- Store không copy được, vì copy sẽ giải phóng cùng một tài nguyên GPU hai lần.

## Lịch chạy system

```mermaid
flowchart LR
  A["ecs_register()"] --> B["pending[phase]<br/>system của module đang setup"]
  B --> C["sắp xếp theo after / before / order"]
  C --> D["schedule[phase]<br/>thứ tự chạy cuối cùng"]
  D --> E["ecs_run(phase)<br/>gọi lần lượt từng hàm"]
```

`ecs_store` (file `runtime/njin_ecs.h`) giữ hai mảng theo phase:

- `pending`: system của module **đang** chạy `setup`.
- `schedule`: thứ tự cuối cùng của mọi module đã đăng ký.

Khi njin_mod_register() được gọi:

1. Bật cờ `in_setup` để ecs_register() biết nó đang được gọi hợp lệ.
2. Gọi `setup` của module. Mỗi ecs_register() đẩy vào `pending[phase]`.
3. Với mỗi phase, sắp xếp `pending` rồi **nối vào cuối** `schedule`, sau đó xóa `pending`.

Vì bước 3 nối vào cuối nên module đăng ký trước chạy trước.

**Sắp xếp** là sắp xếp topo: lấy các system chưa bị ràng buộc `after`/`before`
chặn, chọn cái có `order` nhỏ nhất (bằng nhau thì theo thứ tự đăng ký), đặt nó vào
kết quả, gỡ ràng buộc của nó, lặp lại. Nếu còn system mà không cái nào chọn được thì có vòng phụ thuộc: ghi lỗi
và nối số còn lại theo thứ tự đăng ký.

`ecs_run(phase)` chỉ là một vòng `for` gọi từng hàm trong `schedule[phase]`.

## Module lõi

njin_create() đăng ký sẵn các module lõi trước khi trả về (hiện là module camera,
trong `runtime/modules/`). Vì chúng đăng ký **trước** module của game nên system của
chúng chạy trước trong cùng phase. Module camera dựa vào điều này để bật camera
trước mọi lệnh vẽ của game.

## Thêm một module lõi

1. Tạo `runtime/modules/<tên>.cpp` và `.h`, khai báo một hàm trả về njin::mod_desc.
2. Gọi njin_mod_register() cho nó trong `register_core_modules()` (`runtime/modules/core_modules.cpp`).

CMake tự tìm mọi file `.cpp` dưới `runtime/`, không cần sửa danh sách file.
