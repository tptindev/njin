# Module, system và phase {#modules_systems}

Logic của game trong njin được chia thành ba khái niệm:

| Khái niệm | Là gì | Kiểu |
|---|---|---|
| **System** | Một hàm chạy ở một thời điểm nhất định trong frame | njin::sys_fnc |
| **Phase** | Một giai đoạn của frame mà system được gắn vào | njin::sys_phase |
| **Module** | Một nhóm system có tên, đăng ký cùng lúc | njin::mod_desc |

## System

System là một hàm thường, nhận context của engine và không trả về gì:

```cpp
void move(njin::njin_ctx &ctx);
```

Nó không có trạng thái riêng. Dữ liệu nằm trong component của entity (xem
@ref ecs) hoặc trong biến của file.

## Phase

Mỗi frame gọi các phase theo thứ tự cố định. Xem chi tiết ở @ref game_loop.

| Phase | Khi nào chạy | Dùng để |
|---|---|---|
| njin::phase_startup | Một lần, trước frame đầu | Tạo entity, nạp tài nguyên, gắn phím |
| njin::phase_pre_update | Mỗi frame | Chuẩn bị trước khi cập nhật |
| njin::phase_update | Mỗi frame | Logic chính |
| njin::phase_post_update | Mỗi frame | Việc cần làm sau khi mọi thứ đã cập nhật, ví dụ camera đi theo người chơi |
| njin::phase_pre_render | Mỗi frame | Chuẩn bị vẽ |
| njin::phase_render | Mỗi frame | Vẽ trong không gian thế giới |
| njin::phase_post_render | Mỗi frame | Vẽ trong không gian màn hình (UI) |
| njin::phase_shutdown | Một lần, sau khi cửa sổ đóng | Dọn dẹp |

## Module

Module là nơi bạn khai báo system. Nó gồm một cái tên và một hàm `setup`:

@include hello_module.cpp

Cách hoạt động:

1. njin_mod_register() gọi `setup` đúng một lần.
2. Trong `setup`, bạn gọi ecs_register() cho từng system để gắn nó vào một phase.
3. Từ đó, system chạy mỗi khi phase của nó chạy.

Các quy tắc:

- njin_mod_register() phải được gọi **trước** njin_run().
- Tên module phải **duy nhất**. Đăng ký hai lần cùng một tên bị bỏ qua và ghi cảnh báo.
- ecs_register() chỉ hợp lệ **bên trong `setup`**. Gọi ở nơi khác bị bỏ qua và ghi cảnh báo.
- Module lõi của engine (camera) đã được đăng ký sẵn bởi njin_create().

## Thứ tự chạy

Có hai tầng:

1. **Giữa các module**: chạy theo thứ tự đăng ký. Module đăng ký trước, system của
   nó chạy trước, trong cùng một phase.
2. **Trong một module, trong một phase**: theo ràng buộc bạn khai báo bằng
   njin::sys_desc.

Nếu không khai báo gì, các system chạy theo đúng thứ tự bạn đăng ký. Khi cần kiểm soát:

@include system_order.cpp

| Trường | Ý nghĩa |
|---|---|
| `after` | Các system phải chạy **trước** system này |
| `before` | Các system phải chạy **sau** system này |
| `order` | Giá trị nhỏ chạy trước, trong số các system đã được `after`/`before` cho phép (mặc định 100) |

Khi hai system cùng sẵn sàng và có cùng `order`, system đăng ký trước chạy trước.

@warning `after`/`before` chỉ tham chiếu được các system **cùng module và cùng
phase**. Tham chiếu tới system ở nơi khác bị bỏ qua và ghi cảnh báo. Nếu các ràng
buộc tạo thành vòng (A sau B, B sau A), engine ghi lỗi và để các system còn lại
chạy theo thứ tự đăng ký.

## Ví dụ hoàn chỉnh: chuyển động

Một component `velocity` do game tự định nghĩa, và một system cộng vận tốc vào vị trí:

@include movement.cpp

Lưu ý dòng `njin::delta(ctx)`: nhân vận tốc với thời gian của frame để tốc độ
không phụ thuộc FPS.
