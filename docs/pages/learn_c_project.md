# Bài 3: Nhiều file, preprocessor và liên kết {#learn_c_project}

**Bài này dạy gì:** chia chương trình ra nhiều file (header và source), preprocessor, bốn giai đoạn từ code tới file chạy, và đọc được lỗi `undefined reference`.

**Cần biết trước:** @ref learn_c_memory (con trỏ, chuỗi) và biết chạy `gcc` từ terminal.

Chương trình thật không nằm trong một file. njin có hàng trăm file `.h` và `.cpp`, và lý do nó chia được như vậy là
những điều trong bài này. Mọi ví dụ và lỗi dưới đây được chạy thật với GCC 15.2 trên Windows (w64devkit),
`gcc -std=c17 -Wall -Wextra`; chỗ nào khác trên Linux thì bài nói rõ, và đó là kết quả chạy trong WSL Ubuntu
(GCC 15.2).

## Ba file, một chương trình

Ví dụ: một module vector nhỏ. `learn_c_vec.h` **khai báo** những gì module cung cấp:

@include learn_c_vec.h

`learn_c_vec.c` **định nghĩa** (cài đặt) chúng:

@include learn_c_vec.c

`learn_c_vecmain.c` dùng module:

@include learn_c_vecmain.c

Biên dịch từng file thành **file đối tượng** (`.o`), rồi liên kết:

```
gcc -std=c17 -Wall -Wextra -c learn_c_vecmain.c -o main.o
gcc -std=c17 -Wall -Wextra -c learn_c_vec.c -o vec.o
gcc main.o vec.o -o app
./app
```

```
sum=(3, 4) length=5.0
so lan goi ham: 2
```

Một lệnh làm cả ba việc: `gcc learn_c_vecmain.c learn_c_vec.c -o app`. (Trên Linux thêm `-lm` ở cuối, xem mục lỗi.)

### Khai báo và định nghĩa

| | Ví dụ | Ý nghĩa | Đặt ở đâu |
|---|---|---|---|
| **Khai báo** (declaration) | `float vec2_length(vec2 v);` | "có một hàm tên này, kiểu này" | header, được nhiều file `#include` |
| **Định nghĩa** (definition) | `float vec2_length(vec2 v) { ... }` | chính thân hàm, và cấp bộ nhớ cho biến | **đúng một** file `.c` |

Quy tắc: một thứ được **khai báo nhiều lần** thì được, nhưng **định nghĩa đúng một lần** trong cả chương trình.
Header chứa khai báo, để mọi file dùng chung; file `.c` chứa định nghĩa.

## `#include` và include guard

`#include "learn_c_vec.h"` **chép nguyên văn** nội dung file đó vào chỗ dòng này, trước khi biên dịch. Dấu ngoặc kép tìm
trước trong thư mục của file hiện tại, `<stdio.h>` tìm trong thư mục hệ thống. Thêm thư mục tìm kiếm bằng `-I`;
đó là cách game njin viết `#include <njin.h>`: CMake thêm thư mục `src/engine/api` vào đường tìm.

Vì chỉ là chép, một header có thể bị chép **hai lần** vào một file (`a.h` và `b.h` cùng include `base.h`, rồi `main.c` include cả
hai). Khai báo hai lần thì được, nhưng `typedef struct {...} point;` lần hai là lỗi. Thật:

```
base.h:3:3: error: conflicting types for 'point'; have 'struct <anonymous>'
```

Giải pháp là **include guard**, bọc cả header:

```c
#ifndef LEARN_C_VEC_H   // nếu chưa từng định nghĩa tên này...
#define LEARN_C_VEC_H   // ...thì định nghĩa nó và giữ nội dung
...
#endif                  // lần thứ hai, #ifndef sai và cả khối bị bỏ qua
```

Hoặc một dòng `#pragma once` ở đầu file, ngắn hơn và mọi trình biên dịch phổ biến đều hiểu (nhưng không nằm trong chuẩn C).
njin dùng `#pragma once` ở đầu mọi header (cả 40 header trong `src/engine/api` và 42 header trong `src/engine/runtime`). Cả hai cách đã thử sửa lỗi trên, và biên dịch hết lỗi.

## `static` và `extern`

- **`static` trên hàm hoặc biến toàn cục**: "chỉ file này thấy". `square` trong `learn_c_vec.c` không thể gọi từ file khác, và không va tên với một hàm `square` của file khác. Hàm nội bộ nên là `static`.
- **`extern`**: "biến này có thật, nhưng được định nghĩa ở file khác". `extern int vec2_calls;` trong header khai báo; `int vec2_calls = 0;` trong `.c` định nghĩa.

Vì sao không viết luôn `int vec2_calls = 0;` trong header? Vì mỗi file `.c` include nó sẽ tự định nghĩa một bản, và liên kết
báo lỗi (xem mục lỗi bên dưới). njin có đúng mẫu này trong game mẫu platformer: `src/games/platformer/game.h` có
`extern game_state g;`, còn `main.cpp` có `game_state g;`, định nghĩa duy nhất.

## Preprocessor: chỉnh code trước khi biên dịch

Mọi dòng bắt đầu bằng `#` là chỉ thị cho **preprocessor**, chạy trước trình biên dịch, và làm việc trên **chữ**, không hiểu C.

@include learn_c_platform.c

Kết quả trên Windows, rồi khi thêm `-DDEBUG_LOG` vào lệnh biên dịch:

```
nen tang: Windows
MAX_SPRITES=8
SQUARE_BAD(1 + 2) = 5, SQUARE_OK(1 + 2) = 9
```

```
nen tang: Windows
[log] dang chay
MAX_SPRITES=8
SQUARE_BAD(1 + 2) = 5, SQUARE_OK(1 + 2) = 9
```

Cùng file chạy trên Linux (WSL) cho `nen tang: Linux`, còn các dòng khác giống hệt.

- **`#define TÊN giá trị`**: thay chữ. `MAX_SPRITES` thành `8`.
- **`#if defined(...)` / `#ifdef`**: giữ hoặc bỏ cả một đoạn code. Trình biên dịch tự định nghĩa `_WIN32`, `__linux__`, `__APPLE__` tùy hệ điều hành, nên một file nguồn chọn được mã theo nền tảng. `-DDEBUG_LOG` bật `LOG` thật; không có nó `LOG(...)` thành `((void)0)`, không tốn gì.
- **Macro chỉ thay chữ.** `SQUARE_BAD(1 + 2)` biến thành `1 + 2 * 1 + 2`, bằng 5 chứ không phải 9. Luôn bọc tham số và cả biểu thức trong ngoặc như `SQUARE_OK`, hoặc dùng hàm `static` thay vì macro.

njin dùng chính mẫu này. Trong `src/engine/runtime/njin_file.cpp`, thư mục lưu dữ liệu người dùng khác nhau ở mỗi hệ điều hành:

```cpp
fs::path user_data_dir() {
#if defined(_WIN32)
  ...APPDATA...
#elif defined(__APPLE__)
  ...~/Library/Application Support...
#else
  ...~/.local/share...
#endif
```

`njin_log.h` cũng dùng `#if defined(__MINGW_PRINTF_FORMAT)` để chọn thuộc tính `printf` phù hợp (bài @ref learn_c_start).

## Bốn giai đoạn từ code tới file chạy

`gcc` làm bốn việc liên tiếp. Bạn có thể dừng ở giữa để xem từng kết quả:

| Giai đoạn | Lệnh | Kết quả | Việc làm |
|---|---|---|---|
| 1. Preprocess | `gcc -E learn_c_vecmain.c -o main.i` | `main.i`, C thuần | mở `#include`, thay `#define`, chọn `#if` |
| 2. Compile | `gcc -S learn_c_vecmain.c -o main.s` | `main.s`, assembly | dịch C sang lệnh của CPU |
| 3. Assemble | `gcc -c learn_c_vecmain.c -o main.o` | `main.o`, mã máy | đóng gói lệnh thành file đối tượng |
| 4. Link | `gcc main.o vec.o -o app` | `app`, chương trình | ghép các `.o` và thư viện thành một |

Nhìn tận mắt:

- **Preprocess**: `main.i` dài **1059 dòng** trên máy mình (phần lớn là header của `stdio.h`), và trong đó `#include "learn_c_vec.h"` đã biến thành chính nội dung header:

```
vec2 vec2_add(vec2 a, vec2 b);
float vec2_length(vec2 v);
extern int vec2_calls;
```

- **Compile**: trong `main.s`, lời gọi `vec2_add` chỉ là `call vec2_add`, một cái tên, chưa có địa chỉ nào:

```
32:	call	vec2_add
36:	call	vec2_length
```

- **Assemble**: `nm` liệt kê các **ký hiệu** (tên) mà file đối tượng có hoặc cần. Rút gọn:

```
$ nm main.o      (chỉ giữ các dòng về vec2)
                 U vec2_add
                 U vec2_calls
                 U vec2_length

$ nm vec.o        (chỉ giữ các ký hiệu, bỏ các dòng phân đoạn như .text)
                 U sqrtf
0000000000000000 t square
0000000000000014 T vec2_add
0000000000000000 B vec2_calls
000000000000007e T vec2_length
```

Đọc như sau: `U` (undefined) là "tôi cần cái này, ai đó cho tôi"; `T` là hàm được định nghĩa và **xuất ra** cho file khác;
`B` là biến toàn cục (dữ liệu); **`t` chữ thường là `static`**: `square` chỉ tồn tại nội bộ. `main.o` chưa biết `vec2_add`
nằm đâu, chỉ biết nó cần một cái như vậy.

- **Link**: liên kết khớp mọi `U` của một file với `T`/`B` của file khác, và báo lỗi nếu có `U` không ai cung cấp.

## Đọc lỗi

Lỗi biên dịch (`error: ...` từ `gcc`) xảy ra ở giai đoạn 1 đến 3 và nói **file và dòng**. Lỗi liên kết đến từ `ld` ở giai đoạn 4, chỉ nói **tên ký hiệu**. Đây là bốn lỗi mình gây ra và chạy thật.

### `undefined reference to ...`: quên file `.c`

```
$ gcc main.o -o app
ld.exe: main.o:learn_c_vecmain.c:(.text+0x45): undefined reference to `vec2_add'
ld.exe: main.o:learn_c_vecmain.c:(.text+0x55): undefined reference to `vec2_length'
ld.exe: main.o:...: undefined reference to `vec2_calls'
collect2.exe: error: ld returned 1 exit status
```

Đúng bằng ba dòng `U` ở trên: `main.o` cần ba ký hiệu và không có `.o` nào cung cấp. Cách sửa: thêm `vec.o` (hoặc `learn_c_vec.c`)
vào lệnh liên kết. Đọc `undefined reference to X` như: "tôi đã được hứa X tồn tại (bởi một khai báo), nhưng không thấy định nghĩa".

**Ba nguyên nhân thường gặp của cùng lỗi này:** (1) quên file `.c` trong lệnh build; (2) hàm đó là `static`, không xuất ra. Thử khai báo `float square(float v);` trong `usesq.c` rồi liên kết với `vec.o` cho: ``undefined reference to `square'``. (3) quên thư viện: xem ngay dưới.

### `undefined reference to sqrtf` chỉ trên Linux: thiếu `-lm`

Trên Windows/MinGW, `sqrtf` tự có. Trên Linux (GCC 15.2, WSL), cùng lệnh một dòng cho:

```
$ gcc -std=c17 -Wall -Wextra learn_c_vecmain.c learn_c_vec.c -o vec
learn_c_vec.c:(.text+0xda): undefined reference to `sqrtf'
collect2: error: ld returned 1 exit status
```

`sqrtf` nằm trong thư viện toán `libm`, phải xin bằng `-lm` **ở cuối lệnh**. Thêm `-lm` thì chạy đúng. Chi tiết đáng nhớ: thêm `-O2` thì
**không** cần `-lm` (trình tối ưu thay `sqrtf` bằng đúng một lệnh CPU, không gọi thư viện). Lỗi liên kết phụ thuộc cả cờ
biên dịch, không chỉ code. njin cũng có phần liên kết thư viện phụ thuộc hệ điều hành: trong `src/engine/runtime/CMakeLists.txt`, `ws2_32` chỉ được liên kết trên
Windows và `${CMAKE_DL_LIBS}` (thư viện nạp động) chỉ trên Linux. Bài @ref learn_cmake_basics nói cách khai báo thư viện đúng chỗ.

### `multiple definition of ...`: định nghĩa trong header

Đổi `extern int vec2_calls;` trong header thành `int vec2_calls = 0;`, rồi include header vào hai file `.c`:

```
ld.exe: bad_vec.o:bad_vec.c:(.bss+0x0): multiple definition of `vec2_calls'; bad_main.o:bad_main.c:(.bss+0x0): first defined here
```

Hai file đối tượng đều định nghĩa `vec2_calls`, và link không biết chọn cái nào. Định nghĩa đúng **một** nơi (file `.c`), header chỉ có `extern`.

### `unknown type name` và `implicit declaration`: quên `#include`

Bỏ dòng `#include "learn_c_vec.h"` khỏi `learn_c_vecmain.c`:

```
nodecl.c:4:3: error: unknown type name 'vec2'
nodecl.c:6:14: error: implicit declaration of function 'vec2_add' [-Wimplicit-function-declaration]
```

Đây là lỗi **biên dịch** (giai đoạn 2, có file và dòng): trình biên dịch không có khai báo của `vec2` và `vec2_add`. Với GCC 15.2 và `-std=c17`, gọi hàm chưa khai báo là
lỗi chứ không chỉ là cảnh báo. Phân biệt: có số dòng thì sửa `#include` hay chính tả; chỉ có tên ký hiệu thì sửa lệnh liên kết hoặc định nghĩa.

## Khi có hàng trăm file

Gõ tay `gcc -c` cho ba file thì được. Với hàng trăm file, bạn muốn: chỉ biên dịch lại file nào đổi, tìm header đúng chỗ,
liên kết đúng thư viện theo từng hệ điều hành, và một lệnh duy nhất cho mọi người dùng. Việc đó có tên là **hệ thống build**,
và của njin là CMake: @ref learn_cmake_basics.

## Tự kiểm tra

1. Khác nhau giữa khai báo và định nghĩa? Cái nào được lặp lại nhiều lần?
2. Vì sao mọi header cần include guard hay `#pragma once`?
3. `nm` in `U foo` cho một file đối tượng. Điều đó nói gì?
4. Lỗi nào đến từ trình biên dịch, lỗi nào từ trình liên kết? Nhận biết bằng cách nào?
5. Vì sao không nên viết `int counter = 0;` trong header?

## Bài tập

1. Tách hàm `float clampf(float value, float lo, float hi)` ra `clamp.h` và `clamp.c`, và gọi nó từ `main1.c`. Biên dịch cả ba bằng một lệnh.
2. Cho `add.c` chứa `int add(int a, int b)` và `main2.c` khai báo `int add(int, int);` rồi gọi nó. Biên dịch **chỉ** `main2.c`, đọc lỗi, và sửa.
3. `base.h` có `typedef struct { float x, y; } point;`, `a.h` và `b.h` đều include nó, `main3.c` include cả `a.h` lẫn `b.h`. Gây lỗi rồi sửa bằng hai cách.

## Đáp án

**Tự kiểm tra.**

1. Khai báo nói "có thứ này, kiểu này" và được lặp lại tùy ý; định nghĩa là chính thân hàm hay biến, cấp bộ nhớ, và chỉ được có **đúng một** trong cả chương trình.
2. `#include` chép nguyên văn, nên một header có thể bị chép hai lần vào một file (qua hai header khác nhau). Định nghĩa kiểu hai lần là lỗi; guard làm lần hai thành rỗng.
3. File đó cần ký hiệu `foo` nhưng không định nghĩa nó. Khi liên kết, phải có file nào khác cung cấp, nếu không sẽ có `undefined reference to foo`.
4. Lỗi biên dịch: do `gcc` (`cc1`), có tên file và số dòng. Lỗi liên kết: do `ld`, kết thúc bằng `collect2: error: ld returned 1 exit status`, chỉ có tên ký hiệu.
5. Mỗi file `.c` include header đó sẽ tự định nghĩa một bản `counter`, và link báo `multiple definition`. Dùng `extern int counter;` trong header và một định nghĩa duy nhất trong file `.c`.

**Bài 1**

```c
// clamp.h
#ifndef CLAMP_H
#define CLAMP_H

float clampf(float value, float lo, float hi);

#endif
```

```c
// clamp.c
#include "clamp.h"

float clampf(float value, float lo, float hi) {
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return value;
}
```

```c
// main1.c
#include "clamp.h"
#include <stdio.h>

int main(void) {
  printf("%.1f %.1f %.1f\n", clampf(-3.0f, 0.0f, 10.0f), clampf(4.5f, 0.0f, 10.0f), clampf(99.0f, 0.0f, 10.0f));
  return 0;
}
```

```
$ gcc -std=c17 -Wall -Wextra main1.c clamp.c -o ex1
$ ./ex1
0.0 4.5 10.0
```

**Bài 2**

```
$ gcc -std=c17 -Wall -Wextra main2.c -o ex2
ld.exe: ...main2.c:(.text+0x18): undefined reference to `add'
collect2.exe: error: ld returned 1 exit status
```

`main2.c` chỉ có khai báo `add`, không ai định nghĩa. Sửa: đưa `add.c` vào lệnh:

```
$ gcc -std=c17 -Wall -Wextra main2.c add.c -o ex2
$ ./ex2
5
```

**Bài 3**

Lỗi thật khi chưa có guard:

```
base.h:3:3: error: conflicting types for 'point'; have 'struct <anonymous>'
```

Cách 1: bọc `base.h` trong include guard.

```c
#ifndef BASE_H
#define BASE_H
typedef struct {
  float x, y;
} point;
#endif
```

Cách 2: chỉ cần `#pragma once` ở dòng đầu của `base.h`. Cả hai cách đã biên dịch `main3.c` không lỗi.

## Bước tiếp theo

@ref learn_cpp_from_c : từ C sang C++: tham chiếu, `namespace`, `std::string`, `std::vector`, và những gì thay thế cho các mẫu thủ công trong ba bài này.
