# Mutex sở hữu dữ liệu trong runtime

Runtime dùng `njin::detail::mutex<T>` trong `src/engine/runtime/njin_mutex.h`.
Kiểu này gói dữ liệu cùng mutex, chỉ cho truy cập qua guard đang giữ khóa.
Đây là phần triển khai nội bộ, không thuộc API mà game include.

Ý tưởng tham khảo từ [cpp-mutex-guard](https://github.com/p-avital/cpp-mutex-guard):
mutex sở hữu dữ liệu như Rust. Wrapper nhỏ của njin dùng `std::mutex` và
`std::unique_lock` của C++20; không sao chép mã nguồn hay thêm dependency của
thư viện đó.

```cpp
#include "njin_mutex.h"

struct shared_state {
  int completed = 0;
};
njin::detail::mutex<shared_state> state;

{
  auto guard = state.lock();
  ++guard->completed;
} // tự mở khóa, kể cả khi thoát scope bởi exception

if (auto guard = state.try_lock()) {
  ++(*guard)->completed;
} // try_lock trả std::nullopt nếu không lấy được khóa

const auto &view = state;
auto guard = view.lock(); // chỉ truy cập const shared_state
```

- `mutex<T>` không copy hoặc move, để địa chỉ owner ổn định. Có thể khởi tạo
  dữ liệu bằng `mutex<T>(std::in_place, args...)`.
- Guard chỉ move, và không có hàm mở khóa sớm hay lấy dữ liệu không khóa.
  Không sử dụng guard đã bị move.
- Owner phải sống lâu hơn guard. Không giữ con trỏ/reference tới dữ liệu sau
  khi guard bị hủy. Không chuyển guard sang luồng khác: luồng lấy khóa phải
  là luồng mở khóa. C++ không kiểm tra được các điều kiện vòng đời này.
- Mutex không recursive: không lấy lại cùng khóa khi đang giữ guard, và
  không giữ guard qua `co_await`, I/O chờ lâu hoặc `thread.join()`.
- `const` bảo vệ trực tiếp `T`, không làm đối tượng mà con trỏ bên trong `T`
  trỏ tới trở thành const; các đối tượng đó vẫn cần quy tắc đồng bộ riêng.

Asio dùng mutex sở hữu từng bảng TCP/server/UDP và hàng đợi event. Lookup
copy `shared_ptr` trước khi thả guard; socket và coroutine vẫn do luồng mạng
quản lý. ENet dùng một `mutex<mp_state>` cho toàn bộ host, peer và trạng thái
liên quan, vì ENet cần được truy cập tuần tự. Worker dừng và được join trước
khi state bị hủy. API mạng của game giữ nguyên.

Kiểm tra primitive bằng:

```sh
cmake -S . -B build -DNJIN_BUILD_MUTEX_TESTS=ON
cmake --build build --target njin_mutex_test njin_net --parallel
ctest --test-dir build -R '^mutex_guard$' --output-on-failure
```

Test kiểm tra tăng bộ đếm từ 8 luồng, tranh chấp `try_lock`, move guard,
move assignment giải phóng khóa cũ, tự mở khóa khi có exception, const access
và khởi tạo dữ liệu chỉ move. Test hoạt động cả khi có `NDEBUG`.
