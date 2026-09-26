# Bài 1: Chương trình C đầu tiên {#learn_c_start}

**Bài này dạy gì:** viết, biên dịch và chạy một chương trình C; kiểu dữ liệu, `printf`, `if`, `for`, `while` và hàm.

**Cần biết trước:** mở được terminal và đã cài trình biên dịch (xem @ref setup). Không cần biết lập trình.

Đây là bài đầu của nhóm bài "kiến thức nền": C, C++, CMake và shader, đọc theo thứ tự trước khi bắt đầu
njin (mục lục ở @ref learn). Ai đã biết chủ đề nào thì bỏ qua bài đó. Mọi ví dụ chạy được với chỉ một trình
biên dịch, không cần njin. Các ví dụ dưới đây biên dịch với GCC 15.2 trên Windows (w64devkit) bằng
`gcc -std=c17 -Wall -Wextra`, và không có cảnh báo nào.

## Vì sao học C khi làm game bằng njin?

njin viết bằng C++, nhưng đứng trên **raylib**, một thư viện thuần C. C++ gần như chứa C bên trong, nên
mọi thứ trong bài này còn dùng nguyên vẹn khi bạn viết game:

- Đường dẫn file và chuỗi vẫn là kiểu C: `const char *path`, ví dụ `texture_load(ctx, "assets/tiles.png")`.
- Log dùng chuỗi định dạng của `printf`: `NJIN_INFO("player spawned at %.1f, %.1f", x, y)`.
- Các kiểu số như `int32_t` và `float` là của C. njin chỉ đặt tên ngắn cho chúng (`i32`, `f32`).

## Chương trình đầu tiên

@include learn_c_hello.c

Ghi vào file `hello.c`, rồi trong terminal:

```
gcc -std=c17 -Wall -Wextra hello.c -o hello
./hello
```

```
Xin chao, C!
```

(Trên Windows, GCC tạo `hello.exe`, chạy bằng `hello` hoặc `.\hello`.) Từng phần:

| Phần | Nghĩa |
|---|---|
| `#include <stdio.h>` | lấy khai báo của `printf` từ thư viện chuẩn; thiếu dòng này, trình biên dịch không biết `printf` là gì |
| `int main(void)` | điểm bắt đầu: chương trình chạy từ đây; `void` là "không nhận tham số" |
| `printf("...\n")` | in ra màn hình; `\n` là xuống dòng |
| `return 0;` | báo cho hệ điều hành "thành công"; khác 0 là "có lỗi" |

Ba cờ của `gcc` đáng nhớ: `-std=c17` chọn phiên bản ngôn ngữ, `-Wall -Wextra` bật cảnh báo. **Luôn bật
cảnh báo**: rất nhiều lỗi C được báo ở đây, trước khi chương trình chạy sai.

## Kiểu dữ liệu và `printf`

@include learn_c_types.c

```
lives=3 speed=110.500000 speed(2 so)=110.50
grade=A ma so=65 alive=1
score=1250 red=255 hex=ff
7 / 2 = 3, 7 / 2.0 = 3.5
255 + 1 trong uint8_t = 0
sizeof: char=1 int=4 float=4 double=8 int64_t=8
```

Mỗi biến có một **kiểu**, quyết định nó chiếm bao nhiêu byte và hiểu các bit thế nào:

| Kiểu | Dùng cho | Chỉ định `printf` |
|---|---|---|
| `int` | số nguyên có dấu | `%d` |
| `unsigned int`, `uint8_t`... | số nguyên không dấu | `%u` (hex: `%x`) |
| `float`, `double` | số thực | `%f` (`%.2f`: hai chữ số thập phân) |
| `char` | một ký tự (thực chất là số nhỏ: `'A'` là 65) | `%c` (hoặc `%d` để xem số) |
| `bool` | đúng/sai, cần `<stdbool.h>` | `%d` (0 hoặc 1) |
| `size_t` (kết quả của `sizeof`) | kích thước, chỉ số | `%zu` |

Vài điều kết quả trên cho thấy:

- `7 / 2` là `3`: chia hai số nguyên thì bỏ phần thập phân. Muốn `3.5`, một trong hai phải là số thực (`7 / 2.0`).
- `uint8_t` chỉ chứa 0..255, nên `255 + 1` quay về `0`. Với số **không dấu**, quay vòng là hành vi được C bảo đảm. Với số **có dấu**, tràn số là hành vi không xác định: đừng dựa vào nó.
- Kích thước của `int` không cố định trong ngôn ngữ (thường 4 byte). Nếu cần đúng bao nhiêu bit, dùng kiểu trong `<stdint.h>`.

### Kiểu có kích thước cố định

`<stdint.h>` cho `int8_t`, `int16_t`, `int32_t`, `int64_t` (có dấu) và `uint8_t`... `uint64_t` (không dấu). Game hay
dùng chúng và đặt tên ngắn:

```c
typedef int32_t i32;
typedef float f32;
typedef uint8_t u8;
```

Đó chính là điều njin làm trong `src/engine/api/_types.h`, bằng cú pháp C++ tương đương:

```cpp
using i32 = std::int32_t;
using u8 = std::uint8_t;
using f32 = float;
```

Đọc `i32` trong code njin là đọc `int32_t`.

### Chỉ định sai là lỗi thật

`printf` không biết kiểu của tham số: nó tin bạn. Đưa sai chỉ định thì chương trình in rác hoặc sập. May là trình
biên dịch kiểm tra được, với `-Wall`:

```c
int lives = 3;
printf("lives=%s\n", lives);   // %s cần một chuỗi, không phải int
printf("speed=%d\n", 110.5);   // %d cần int, không phải double
```

```
warning: format '%s' expects argument of type 'char *', but argument 2 has type 'int' [-Wformat=]
warning: format '%d' expects argument of type 'int', but argument 2 has type 'double' [-Wformat=]
```

Việc này chỉ hoạt động với `printf` và họ hàng của nó, vì trình biên dịch biết riêng về chúng. Với hàm tự viết, bạn
phải nói cho nó biết bằng một thuộc tính của GCC/Clang:

```c
__attribute__((format(printf, 1, 2))) static void log_info(const char *fmt, ...);
```

`format(printf, 1, 2)` nghĩa là "tham số 1 là chuỗi định dạng kiểu printf, các tham số kiểm tra bắt đầu từ vị trí 2".
Gọi `log_info("texture missing: %s", 42)` thì nhận đúng cảnh báo `-Wformat=` như trên, và nếu bỏ qua cảnh báo mà chạy
thì chương trình **sập** (đã thử: segmentation fault), vì `%s` đi đọc bộ nhớ ở địa chỉ 42.

Trong njin, `src/engine/api/njin_log.h` làm đúng như vậy:

```cpp
#define NJIN_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
...
void log_write(log_level level, const char *file, i32 line, const char *fmt,
               ...) NJIN_PRINTF(4, 5);
```

Chuỗi định dạng của `log_write` là tham số thứ 4, các tham số sau bắt đầu từ thứ 5. Nhờ đó `NJIN_INFO("... %d", x)` với
sai kiểu cũng bị cảnh báo lúc biên dịch. (Trên MinGW, tức GCC ở Windows, njin dùng `__MINGW_PRINTF_FORMAT` thay cho `printf`; đó là lý do có
nhánh `#if` ở đầu file: bài sau nói về `#if`.)

## Điều kiện, vòng lặp và hàm

@include learn_c_flow.c

```
frame 0: y=0.0083 vy=0.500
frame 1: y=0.0250 vy=1.000
frame 2: y=0.0500 vy=1.500
frame 3: y=0.0833 vy=2.000
frame 4: y=0.1250 vy=2.500
cham dat sau 35 frame, y=5.00
```

Chương trình mô phỏng một vật rơi, đúng cách một game cập nhật vị trí mỗi frame:

- **`if`** chọn nhánh. `clampf` dùng nó để giữ một giá trị trong khoảng `[lo, hi]`.
- **`for (khởi tạo; điều kiện; bước)`** lặp khi biết trước số lần. `frame` chỉ tồn tại trong vòng lặp.
- **`while (điều kiện)`** lặp đến khi điều kiện sai. Nếu điều kiện không bao giờ sai, chương trình chạy mãi.
  Cũng có `do { ... } while (điều kiện);` chạy thân ít nhất một lần.
- **Hàm** gồm kiểu trả về, tên, và danh sách tham số. `static` ở đây nghĩa là "chỉ file này dùng hàm này", bài
  @ref learn_c_project giải thích.
- **`const`** cam kết "không đổi": `const float dt` không thể bị gán lại. Hãy dùng nó cho mọi thứ không cần đổi.

Số `1.0f / 60.0f` là thời gian của một frame ở 60 FPS. `vy += gravity * dt` là "vận tốc tăng theo gia tốc nhân với thời gian",
`y += vy * dt` là "vị trí tăng theo vận tốc nhân với thời gian". Bạn sẽ gặp đúng hai dòng này trong mọi
game, kể cả trong `platformer_body` của njin (nhân với `delta(ctx)` thay vì hằng `dt`).

## Tự kiểm tra

1. `printf("%d\n", 7 / 2);` in ra gì, và vì sao?
2. Khác nhau giữa `int` và `int32_t` là gì? Khi nào nên dùng cái sau?
3. `uint8_t x = 250; x = x + 10;` cho `x` bằng bao nhiêu?
4. Tại sao `-Wall -Wextra` quan trọng khi học C?
5. Vòng `for` và `while` khác nhau ở điểm nào?

## Bài tập

1. Viết hàm `float lerp(float a, float b, float t)` trả về `a + (b - a) * t`, rồi in giá trị với `t` = 0, 0.25, 0.5, 0.75, 1 giữa 10 và 20.
2. Một vật bật lên với `vy = -10` (âm là hướng lên trên, `y = 0` là mặt đất) và gia tốc 30. Đếm bao nhiêu frame ở 60 FPS thì nó rơi lại mặt đất (`y >= 0`). Dùng `do ... while`.
3. Viết hàm `bool is_even(int n)` và in các số chẵn từ 1 đến 10.

## Đáp án

**Tự kiểm tra.**

1. In `3`. Cả hai toán hạng là `int`, nên phép chia là chia nguyên và bỏ phần thập phân.
2. `int` có kích thước tùy trình biên dịch (thường 32 bit); `int32_t` luôn đúng 32 bit có dấu. Dùng `int32_t` khi kích thước quan trọng: dữ liệu lưu file, gửi qua mạng, giao tiếp với thư viện.
3. `4`. `x + 10` là 260, nhưng `uint8_t` chỉ chứa 0..255 nên còn `260 - 256 = 4`.
4. Vì C tin lập trình viên: nhiều lỗi (sai chỉ định `printf`, biến chưa gán, so sánh nhầm) chỉ bị phát hiện ở cảnh báo, không phải lỗi biên dịch.
5. `for` gọn khi biết trước số lần lặp (khởi tạo, điều kiện, bước nằm cùng một dòng); `while` dùng khi lặp đến một điều kiện không đếm được, ví dụ "đến khi chạm đất".

**Bài 1**

```c
#include <stdio.h>

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

int main(void) {
  for (int i = 0; i <= 4; i++) {
    float t = i / 4.0f;
    printf("t=%.2f -> %.1f\n", t, lerp(10.0f, 20.0f, t));
  }
  return 0;
}
```

```
t=0.00 -> 10.0
t=0.25 -> 12.5
t=0.50 -> 15.0
t=0.75 -> 17.5
t=1.00 -> 20.0
```

**Bài 2**

```c
#include <stdio.h>

int main(void) {
  const float gravity = 30.0f, dt = 1.0f / 60.0f;
  float y = 0.0f;
  float vy = -10.0f;
  int frames = 0;
  do {
    vy += gravity * dt;
    y += vy * dt;
    frames++;
  } while (y < 0.0f);
  printf("roi lai mat dat sau %d frame\n", frames);
  return 0;
}
```

```
roi lai mat dat sau 39 frame
```

Dùng `do ... while` vì thân vòng phải chạy ít nhất một lần: lúc đầu `y = 0`, nên điều kiện `y < 0` sai ngay và một
vòng `while` thường sẽ không chạy lần nào.

**Bài 3**

```c
#include <stdbool.h>
#include <stdio.h>

static bool is_even(int n) { return n % 2 == 0; }

int main(void) {
  for (int n = 1; n <= 10; n++)
    if (is_even(n))
      printf("%d ", n);
  printf("\n");
  return 0;
}
```

```
2 4 6 8 10
```

## Bước tiếp theo

@ref learn_c_memory : bộ nhớ, con trỏ, mảng và chuỗi, phần khó nhất và quan trọng nhất của C.
