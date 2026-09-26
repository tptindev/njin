# Bài 7: Đọc lỗi biên dịch và lỗi liên kết {#learn_errors}

**Bài này dạy gì**: đọc thông báo lỗi của GCC (biên dịch, liên kết, lúc chạy) để tự tìm ra dòng sai thay vì
đoán, và một quy trình gỡ lỗi gồm `-Wall`, `printf` và `gdb`.

**Cần biết trước**: đã viết và biên dịch được một chương trình C++ nhỏ, xem @ref learn_cpp_from_c.

Mọi thông báo trong bài này là **đầu ra thật** của GCC 15.2 (w64devkit, Windows), chạy với
`g++ -std=c++20 -Wall -Wextra -Wpedantic`. Chỉ có độ dài được cắt bớt (chỗ cắt ghi `...`). Trên hệ điều hành
hoặc phiên bản GCC khác, chữ có thể hơi khác (đường dẫn của `ld`, tên file tạm) nhưng cách đọc thì như nhau.

## Ba giai đoạn, ba loại lỗi

Một lệnh `g++ main.cpp -o game` làm ba việc nối tiếp, và lỗi ở mỗi giai đoạn trông khác nhau:

| Giai đoạn | Việc làm | Lỗi trông như thế nào |
|---|---|---|
| **Biên dịch** (compile) | Mỗi file `.cpp` thành một file đối tượng `.o` | `file.cpp:5:3: error: ...` |
| **Liên kết** (link) | Ghép các `.o` và thư viện thành file chạy | `undefined reference to ...`, `multiple definition of ...` |
| **Chạy** (run) | Chạy chương trình | Chương trình chết: `Segmentation fault`, `Assertion failed` |

Biết mình đang ở giai đoạn nào là nửa việc sửa lỗi: lỗi liên kết không sửa được bằng cách đọc lại một dòng code,
và lỗi biên dịch thì không liên quan gì tới việc thiếu file trong build. Tách hai giai đoạn ra để thấy:

```
g++ -std=c++20 -c main.cpp     (chỉ biên dịch, ra main.o)
g++ main.o -o game             (chỉ liên kết)
```

## Đọc một thông báo của GCC

Một chương trình thiếu dấu chấm phẩy:

File `syntax.cpp`:

```cpp
#include <cstdio>

int main() {
  int lives = 3
  std::printf("%d", lives);
  return 0;
}
```

```
syntax.cpp: In function 'int main()':
syntax.cpp:5:3: error: expected ',' or ';' before 'std'
    5 |   std::printf("%d", lives);
      |   ^~~
syntax.cpp:4:7: warning: unused variable 'lives' [-Wunused-variable]
    4 |   int lives = 3
      |       ^~~~~
```

Đọc từng phần:

- `syntax.cpp:5:3:` là **file, dòng, cột**. Dòng 5, cột 3.
- `error:` hay `warning:`: lỗi thì không tạo ra file chạy; cảnh báo thì có, nhưng thường là lỗi thật đang chờ.
- Sau đó là nội dung, rồi **trích dòng code** với dấu `^~~~` chỉ chỗ.
- `[-Wunused-variable]` là tên cờ đã sinh ra cảnh báo. Đó là từ khóa để tra cứu.
- `note:` là thông tin thêm đi kèm lỗi phía trên nó (ví dụ "declared here").

Để ý điều quan trọng nhất trong ví dụ này: dấu `;` bị thiếu ở **dòng 4**, nhưng lỗi được báo ở **dòng 5**. Trình
biên dịch chỉ biết câu lệnh đã kết thúc sai khi nó đọc tới `std` ở dòng sau. Nên quy tắc là:

> Lỗi cú pháp không tìm thấy ở dòng được báo thì nhìn **dòng ngay trước nó**.

## Năm quy tắc đọc lỗi

**1. Đọc lỗi đầu tiên.** Một lỗi có thể kéo theo hàng chục lỗi sau, vì trình biên dịch đã hiểu sai từ chỗ đó.
Sửa lỗi đầu tiên rồi biên dịch lại; đừng sửa lỗi thứ 30 khi chưa xem lỗi thứ nhất.

**2. Lỗi dây chuyền.** Đây là một `struct` thiếu dấu `}` ở dòng 3:

File `cascade.cpp`:

```cpp
#include <vector>

struct Enemy {
  int hp;
  Enemy(int h) : hp(h) {}

Enemy make_boss() { return Enemy(50); }

int main() {
  std::vector<Enemy> wave;
  wave.push_back(make_boss());
  return wave.size();
}
```

```
cascade.cpp:13:2: error: expected '}' at end of input
   13 | }
      |  ^
cascade.cpp:3:14: note: to match this '{'
    3 | struct Enemy {
      |              ^
cascade.cpp:13:2: error: expected unqualified-id at end of input
```

Lỗi được báo ở **cuối file** (dòng 13) chứ không ở chỗ thiếu. Dòng `note:` mới chỉ đúng nguyên nhân: dấu `{` ở
dòng 3 chưa có `}` tương ứng. Vậy `note:` cũng quan trọng như `error:`.

**3. Đừng bỏ qua cảnh báo.** Hàm này quên `return` khi `lives` bằng 0:

File `noreturn.cpp`:

```cpp
#include <cstdio>

int lives_left(int lives) {
  if (lives > 0)
    return lives - 1;
}

int main() {
  std::printf("%d", lives_left(0));
}
```

```
noreturn.cpp:6:1: warning: control reaches end of non-void function [-Wreturn-type]
    6 | }
      | ^
```

Nó biên dịch được. Nhưng khi chạy, **chương trình chết** ngay trên máy này:

```
Illegal instruction
```

Chạy hết hàm mà không `return` là hành vi không xác định: mỗi trình biên dịch, mỗi mức tối ưu cho một kết
quả khác. Vì vậy luôn biên dịch với `-Wall -Wextra` và coi cảnh báo như lỗi.

**4. "In file included from".** Lỗi nằm trong header thì thông báo kể cả chuỗi include:

File `bad.h`:

```cpp
#pragma once
inline int twice(int x) { return x * 2 }
```

```
In file included from inc.cpp:1:
bad.h: In function 'int twice(int)':
bad.h:2:39: error: expected ';' before '}' token
    2 | inline int twice(int x) { return x * 2 }
      |                                       ^~
      |                                       ;
```

Dòng đầu chỉ nói header này được kéo vào từ `inc.cpp` dòng 1. Lỗi thật nằm ở `bad.h:2`, và GCC còn gợi ý
luôn dấu `;` cần thêm.

**5. Lỗi mẫu (template) rất dài: tìm dòng của bạn.** Đưa một chuỗi vào `std::vector<int>`:

File `tmpl.cpp`:

```cpp
#include <vector>

int main() {
  std::vector<int> scores;
  scores.push_back("ten");
}
```

```
tmpl.cpp:5:19: error: no matching function for call to 'push_back(const char [4])'
    5 |   scores.push_back("ten");
      |   ~~~~~~~~~~~~~~~~^~~~~~~~
tmpl.cpp:5:19: note: there are 2 candidates
In file included from C:/Dev/raylib/w64devkit/lib/gcc/x86_64-w64-mingw32/15.2.0/include/c++/vector:68,
                 from tmpl.cpp:1:
...stl_vector.h:1416:7: note: candidate 1: 'constexpr void std::vector<_Tp, _Alloc>::push_back(const value_type&) ...
...
tmpl.cpp:5:20: error: invalid conversion from 'const char*' to 'std::vector<int>::value_type' {aka 'int'} [-fpermissive]
```

Phần lớn chữ nằm trong thư viện chuẩn (`stl_vector.h`). Bạn chỉ cần: dòng `error:` đầu tiên và **dòng nào nằm
trong file của mình** (`tmpl.cpp:5`). Câu chữ cũng đủ để hiểu: "không có `push_back` nào nhận `const char[4]`, và
chuỗi không đổi được thành `int`".

## Các lỗi biên dịch hay gặp

**Chưa khai báo (undeclared).** Dùng tên mà trình biên dịch chưa thấy: quên `#include`, quên `std::`, hoặc gõ sai:

File `undeclared.cpp`:

```cpp
#include <cstdio>

int main() {
  int lives = 3;
  cout << lives;
  scoreboard = 10;
  return 0;
}
```

```
undeclared.cpp:5:3: error: 'cout' was not declared in this scope
    5 |   cout << lives;
      |   ^~~~
undeclared.cpp:6:3: error: 'scoreboard' was not declared in this scope
```

Cách sửa: `cout` cần `#include <iostream>` và `std::cout`; `scoreboard` chưa từng được khai báo (gõ sai tên?).
Với njin, lỗi này thường có nghĩa là thiếu `#include <njin.h>` hoặc thiếu `njin::` trước tên.

**Sai kiểu và sai số đối số.**

File `mismatch.cpp`:

```cpp
#include <string>

struct vec2 { float x, y; };

void move(vec2 &pos, float dx, float dy) { pos.x += dx; pos.y += dy; }

int main() {
  int lives = "three";
  vec2 p{0, 0};
  move(p, 1.0f);
  return lives;
}
```

```
mismatch.cpp:8:15: error: invalid conversion from 'const char*' to 'int' [-fpermissive]
    8 |   int lives = "three";
      |               ^~~~~~~
mismatch.cpp:10:7: error: too few arguments to function 'void move(vec2&, float, float)'
   10 |   move(p, 1.0f);
      |   ~~~~^~~~~~~~~
mismatch.cpp:5:6: note: declared here
    5 | void move(vec2 &pos, float dx, float dy) { pos.x += dx; pos.y += dy; }
```

Lỗi thứ hai có `note: declared here`: nó chỉ bạn tới chỗ hàm được khai báo để so với lời gọi. Đây là mẫu chung:
**`error:` nói lời gọi sai ở đâu, `note:` nói cái nó đang so sánh với là gì.**

**Vi phạm `const`.**

File `constviol.cpp`:

```cpp
struct vec2 { float x, y; };

void reset(const vec2 &pos) {
  pos.x = 0;
}

int main() {
  vec2 p{1, 2};
  reset(p);
}
```

```
constviol.cpp:4:9: error: assignment of member 'vec2::x' in read-only object
    4 |   pos.x = 0;
      |   ~~~~~~^~~
```

Hàm hứa "chỉ đọc" (`const vec2 &`) mà lại sửa. Sửa hàm cho đúng ý định: nếu cần sửa thì bỏ `const`.

## Hai lỗi thường gặp khi dùng njin

Hai lỗi này đều xuất hiện thật khi viết các bài hướng dẫn của njin.

**"only 3 names provided for structured binding".** Duyệt `view<A, B, C>().each()` trả về **entity cộng ba
component**, tức bốn phần tử, nên `auto [e, a, b]` thiếu một tên. Tái hiện bằng `std::tuple` thường:

File `sb.cpp`:

```cpp
#include <tuple>

int main() {
  std::tuple<int, float, float, int> row{1, 2.0f, 3.0f, 4};
  auto [entity, pos, vel] = row;
  return 0;
}
```

```
sb.cpp:5:8: error: only 3 names provided for structured binding
    5 |   auto [entity, pos, vel] = row;
      |        ^~~~~~~~~~~~~~~~~~
sb.cpp:5:8: note: while 'std::tuple<int, float, float, int>' decomposes into 4 elements
```

Cách sửa: đủ số tên (nhớ tên đầu là entity), hoặc dùng dạng lambda `each([](A &a, B &b, C &c) {...})`. Xem
@ref ecs.

**"invalid use of void expression" nằm trong thư viện, không trong code của bạn.** `get` hay `try_get` trên một
component rỗng (tag, `struct player_tag {};`) không có gì để trả về. Đây là đầu ra thật, biên dịch cùng EnTT:

File `entt_tag.cpp` (cần EnTT, biên dịch với `-isystem` trỏ tới thư mục `src` của EnTT):

```cpp
#include <entt/entity/registry.hpp>

struct player_tag {};

int main() {
  entt::registry reg;
  const entt::entity e = reg.create();
  reg.emplace<player_tag>(e);
  return reg.try_get<player_tag>(e) == nullptr;
}
```

```
In file included from entt_tag.cpp:1:
.../entt/entity/registry.hpp: In instantiation of 'auto entt::basic_registry<...>::try_get(entity_type) const ...
.../entt/entity/registry.hpp:911:83:   required from 'auto entt::basic_registry<...>::try_get(entity_type) [with Type = {player_tag}
  911 |             return (const_cast<Type *>(stl::as_const(*this).template try_get<Type>(entt)), ...);
entt_tag.cpp:9:33:   required from here
    9 |   return reg.try_get<player_tag>(e) == nullptr;
      |          ~~~~~~~~~~~~~~~~~~~~~~~^~~
.../entt/entity/registry.hpp:901:80: error: invalid use of void expression
  901 |             return (cpool && cpool->contains(entt)) ? stl::addressof(cpool->get(entt)) : nullptr;
```

Dòng có chữ `error:` nằm trong `registry.hpp`, code của EnTT. Đừng sửa ở đó. Tìm dòng **`required from here`** và
chữ `.cpp` của bạn: `entt_tag.cpp:9:33`, đúng chỗ gọi `try_get` trên một tag. Cách sửa: hỏi `reg.all_of<player_tag>(e)`.

Bản thân thông báo `invalid use of void expression` xuất hiện khi bạn truyền một biểu thức `void` như một giá trị:

File `void_arg.cpp`:

```cpp
#include <utility>

void tick() {}

int main() {
  auto p = std::make_pair(1, tick());
  return p.first;
}
```

```
void_arg.cpp:6:34: error: invalid use of void expression
    6 |   auto p = std::make_pair(1, tick());
      |                              ~~~~^~
```

Hàm trả `void` thì không có giá trị để gán (`int r = tick();` báo `void value not ignored as it ought to be`).

## Lỗi trong C

C cũng dùng GCC nên đọc giống nhau. Một khác biệt đáng biết: gọi hàm chưa khai báo. Với `gcc -std=c17`:

File `implicit.c`:

```c
int main(void) {
  printf("hello");
  return 0;
}
```

```
implicit.c:2:3: error: implicit declaration of function 'printf' [-Wimplicit-function-declaration]
    2 |   printf("hello");
      |   ^~~~~~
implicit.c:1:1: note: include '<stdio.h>' or provide a declaration of 'printf'
  +++ |+#include <stdio.h>
    1 | int main(void) {
```

GCC còn chỉ luôn dòng `#include <stdio.h>` cần thêm. (Bản GCC cũ chỉ cảnh báo thay vì báo lỗi; GCC 15.2 báo lỗi.)

## Lỗi liên kết

Biên dịch xong hết mà `g++` vẫn báo lỗi, và thông báo không có số dòng: đó là lỗi liên kết.

**`undefined reference`**: chương trình gọi một hàm mà **không có định nghĩa nào** trong các file được đưa vào
liên kết. Khai báo (`int score(int lives);`) chưa đủ, phải có chỗ định nghĩa.

File `undef_main.cpp`:

```cpp
#include <cstdio>

int score(int lives);

int main() {
  std::printf("%d", score(3));
}
```

```
...ld.exe: ...undef_main.cpp:(.text+0x13): undefined reference to `score(int)'
collect2.exe: error: ld returned 1 exit status
```

`ld` là chương trình liên kết. Tên trong dấu nháy là hàm đang thiếu, **kèm kiểu tham số**: `score(int)`. Ba
nguyên nhân hay gặp:

1. Chưa viết định nghĩa, hoặc file `.cpp` chứa nó **chưa được thêm vào build** (với CMake: chưa có trong
   `add_executable`, xem @ref learn_cmake_basics).
2. Định nghĩa có nhưng **chữ ký khác** (kiểu tham số, `const`, namespace): tên `score(int)` phải khớp từng chữ.
3. Thiếu **thư viện**: hàm nằm trong thư viện chưa được liên kết.

Trường hợp 3 xảy ra thật với njin. Đây là `minimal_main.cpp` của docs, đã có đường include đúng nhưng chưa liên
kết engine:

```
...minimal_main.cpp:(.text+0xb0): undefined reference to `njin::njin_create(njin::njin_cfg const&)'
...minimal_main.cpp:(.text+0xc0): undefined reference to `njin::njin_run(njin::njin_ctx&)'
...minimal_main.cpp:(.text+0xcc): undefined reference to `njin::njin_destroy(njin::njin_ctx*)'
collect2.exe: error: ld returned 1 exit status
```

Chữ `njin::` cho thấy các hàm đó thuộc engine: cần `target_link_libraries(... njin::rt)` trong CMake. Còn nếu
thiếu cả đường include, lỗi xảy ra sớm hơn, ở giai đoạn biên dịch:

```
minimal_main.cpp:1:10: fatal error: njin.h: No such file or directory
    1 | #include <njin.h>
      |          ^~~~~~~~
compilation terminated.
```

**`multiple definition`**: **hai** file cùng định nghĩa một hàm. Hay gặp nhất khi ai đó viết thân hàm trong
header:

File `util.h`:

```cpp
#pragma once
int square(int x) { return x * x; }
```

File `a.cpp`:

```cpp
#include "util.h"
int from_a() { return square(2); }
```

File `b.cpp`:

```cpp
#include "util.h"
int from_b() { return square(3); }
int from_a();
int main() { return from_a() + from_b(); }
```

```
...b.cpp:(.text+0x0): multiple definition of `square(int)'; ...a.cpp:(.text+0x0): first defined here
collect2.exe: error: ld returned 1 exit status
```

`#pragma once` chỉ chống include hai lần **trong một file `.cpp`**. Mỗi `.cpp` vẫn nhận một bản `square` riêng, rồi
liên kết thấy hai bản. Sửa bằng `inline` (hàm nhỏ trong header) hoặc chuyển thân hàm vào một file `.cpp`. Với
`inline`, chương trình trên liên kết được và trả về 13 (4 + 9). Đó là lý do njin viết `inline constexpr` cho các hằng số trong header.

## Lỗi lúc chạy

Chương trình đã chạy được rồi mới chết. Không có số dòng trong thông báo.

**Truy cập con trỏ null (segmentation fault).**

File `crash.cpp`:

```cpp
#include <cstdio>

struct Enemy { int hp = 3; };

Enemy *find_enemy(int id) {
  return id == 1 ? new Enemy : nullptr;
}

int main() {
  Enemy *e = find_enemy(2);
  std::printf("hp = %d", e->hp);
  return 0;
}
```

Chạy trong Git Bash, chỉ có một dòng `Segmentation fault` và mã thoát 139. Trong PowerShell, cùng chương trình
cho mã thoát `-1073741819` (tức `0xC0000005`, lỗi truy cập bộ nhớ của Windows). Mã thoát không giúp gì để biết
**ở đâu**: đó là việc của `gdb` (mục sau).

**`assert` thất bại.** `assert(điều_kiện)` dừng chương trình nếu điều kiện sai, và nói luôn điều kiện nào ở dòng
nào:

File `asrt.cpp`:

```cpp
#include <cassert>
#include <vector>

int main() {
  std::vector<int> scores{10, 20};
  int index = 2;
  assert(index < static_cast<int>(scores.size()) && "index out of range");
  return scores[index];
}
```

```
Assertion failed: index < static_cast<int>(scores.size()) && "index out of range", file asrt.cpp, line 7
```

Mẹo `&& "lời nhắn"` để thêm câu chữ vào thông báo. `assert` chỉ có ở bản debug: biên dịch với `-DNDEBUG`
(bản release của njin làm vậy) thì nó biến mất. Thư viện dùng nó để bắt lỗi lập trình. Đây là thông báo thật khi gọi
`registry.get<X>(e)` trên một entity không có `X` (xem @ref ecs):

```
Assertion failed: ((contains(entt)) && ("Set does not contain entity")), file .../sparse_set.hpp, line 721
```

Thấy `Assertion failed` từ thư viện thì đọc điều kiện và tên hàm gọi tới nó, rồi tìm lời gọi trong code của mình.

## Quy trình gỡ lỗi

1. **Biên dịch với `-g -Wall -Wextra`**. `-g` cho `gdb` biết tên hàm và số dòng; `-Wall -Wextra` bật cảnh báo
   (xem quy tắc 3).
2. **Đọc lỗi đầu tiên** và dòng ngay trước nó.
3. **Thu nhỏ**: cắt chương trình còn ít dòng nhất vẫn gây lỗi. Nhiều lỗi tự lộ ra khi cắt.
4. **In ra**: `std::printf("x = %d\n", x);` trước dòng nghi ngờ. Thô sơ nhưng hiệu quả.
5. **Dùng `gdb`** khi chương trình chết mà không biết ở đâu.

`gdb` (có trong w64devkit) chạy chương trình và dừng đúng chỗ chết. Chương trình sau gọi hai lớp hàm trước khi
truy cập con trỏ null:

File `deep.cpp`:

```cpp
#include <cstdio>

struct Enemy { int hp = 3; };

int hit_points(const Enemy *e) { return e->hp; }
int report(const Enemy *e) { return hit_points(e); }

int main() {
  Enemy *target = nullptr;
  std::printf("hp = %d", report(target));
  return 0;
}
```

Biên dịch với `g++ -std=c++20 -g deep.cpp -o deep.exe`, rồi:

```
gdb -batch -ex run -ex bt -ex "print e" -ex "up 2" -ex "info locals" ./deep.exe
```

```
Thread 1 received signal SIGSEGV, Segmentation fault.
0x00007ff626c3149c in hit_points (e=0x0) at deep.cpp:5
5	int hit_points(const Enemy *e) { return e->hp; }
#0  0x00007ff626c3149c in hit_points (e=0x0) at deep.cpp:5
#1  0x00007ff626c314b8 in report (e=0x0) at deep.cpp:6
#2  0x00007ff626c314df in main () at deep.cpp:10
$1 = (const Enemy *) 0x0
#2  0x00007ff626c314df in main () at deep.cpp:10
10	  std::printf("hp = %d", report(target));
target = 0x0
```

Địa chỉ dạng `0x00007ff626c3149c` đổi giá trị mỗi lần chạy, nên đừng để ý tới chúng. Chỉ cần đọc **tên hàm, số
dòng và giá trị biến**.

| Lệnh | Việc làm |
|---|---|
| `run` | Chạy chương trình |
| `bt` | In **chuỗi lời gọi** (backtrace) tại chỗ chết: `#0` là chỗ chết, `#2` là `main` |
| `print e` | In giá trị biến. Ở đây `e` là con trỏ null (`0x0`) |
| `up 2` | Nhảy lên hai tầng trong chuỗi gọi, để xem biến của `main` |
| `info locals` | In mọi biến cục bộ của tầng đang xem: `target = 0x0` |
| `break report` | Dừng trước khi vào hàm `report` (rồi `next` để đi từng dòng) |

Đọc kết quả: chương trình chết ở `hit_points` dòng 5 vì `e` là null; nó được gọi từ `report`, được gọi từ `main`
dòng 10, nơi `target` là null từ đầu. Lỗi thật nằm ở `main`, không phải chỗ chết.

**Trình kiểm tra bộ nhớ (sanitizer).** GCC và Clang có `-fsanitize=address,undefined` bắt lỗi bộ nhớ và hành vi
không xác định ngay khi xảy ra. Trên GCC w64devkit dùng cho bài này, cờ đó **không dùng được**:

```
ld.exe: cannot find -lasan
```

Bản GCC của Linux thường có sẵn nhưng chưa được thử trong bài này. Trên Windows với w64devkit hãy dùng `-Wall`,
`assert` và `gdb`.

## Tự kiểm tra

1. Lỗi cú pháp báo ở dòng 12 nhưng dòng 12 nhìn đúng. Nên nhìn ở đâu?
2. Có 30 dòng `error:`. Đọc dòng nào trước? Vì sao?
3. `undefined reference to 'score(int)'` là lỗi biên dịch hay lỗi liên kết? Nêu hai nguyên nhân có thể.
4. `multiple definition of 'square(int)'` sinh ra khi nào, và sửa bằng cách nào?
5. Chương trình in `Segmentation fault`. Lệnh nào cho biết dòng gây lỗi, và cần biên dịch với cờ nào để lệnh
   đó in được số dòng?

## Bài tập

**Bài 1: ba lỗi biên dịch.** Chương trình sau có ba lỗi. Biên dịch, đọc thông báo, sửa từng lỗi:

File `ex1.cpp`:

```cpp
#include <cstdio>

struct Player {
  int lives = 3;
  float speed = 90.0f
};

int main() {
  Player p;
  int coins = "five";
  cout << p.lives << coins;
  return 0;
}
```

**Bài 2: lỗi liên kết.** Hai file này biên dịch được từng file, nhưng `g++ main2.cpp score2.cpp` báo
`undefined reference`. Tìm ra vì sao (gợi ý: so `total_score(...)` trong thông báo với định nghĩa) và sửa:

File `main2.cpp`:

```cpp
#include <cstdio>

int total_score(const int *values, int count);

int main() {
  int scores[] = {10, 20, 30};
  std::printf("%d", total_score(scores, 3));
}
```

File `score2.cpp`:

```cpp
unsigned total_score(const int *values, unsigned count) {
  unsigned sum = 0;
  for (unsigned i = 0; i < count; i++)
    sum += values[i];
  return sum;
}
```

**Bài 3: dùng `gdb`.** Chương trình này chết. Biên dịch với `-g`, dùng `gdb -batch -ex run -ex bt` để tìm dòng
và biến null, rồi sửa để nó in "no enemies" khi làn sóng rỗng:

File `ex3.cpp`:

```cpp
#include <cstdio>
#include <vector>

struct Enemy {
  int hp = 3;
  float x = 0;
};

Enemy *nearest(std::vector<Enemy> &wave, float x) {
  Enemy *best = nullptr;
  for (Enemy &e : wave)
    if (best == nullptr || e.x - x < best->x - x)
      best = &e;
  return best;
}

int main() {
  std::vector<Enemy> wave;
  Enemy *target = nearest(wave, 10.0f);
  std::printf("target hp = %d\n", target->hp);
}
```

## Đáp án

**Câu hỏi.**

1. Ở **dòng ngay trước** (thường là thiếu `;` hoặc `)`), vì trình biên dịch chỉ nhận ra lỗi khi đọc tới dòng sau.
   Nếu là `expected '}' at end of input` thì đọc dòng `note: to match this '{'`.
2. Dòng đầu tiên: các lỗi sau thường là hậu quả của nó.
3. Lỗi liên kết. Hàm không có định nghĩa (hoặc file chứa nó không được đưa vào build), hoặc định nghĩa có chữ ký
   khác (kiểu tham số, `const`, namespace), hoặc hàm nằm trong thư viện chưa được liên kết.
4. Khi hai file `.cpp` cùng chứa định nghĩa của một hàm, thường vì thân hàm nằm trong header. Sửa: thêm `inline`,
   hoặc chuyển thân hàm sang một file `.cpp` và để header chỉ có khai báo.
5. `gdb -batch -ex run -ex bt ./chuong_trinh`. Cần `-g` khi biên dịch.

**Bài 1.** GCC báo ba lỗi: `expected ';' at end of member declaration` ở dòng 5, `invalid conversion from
'const char*' to 'int'` ở dòng 10, `'cout' was not declared in this scope` ở dòng 11. Đây là bản sửa, chạy in `3 5`:

File `ex1_fixed.cpp`:

```cpp
#include <cstdio>

struct Player {
  int lives = 3;
  float speed = 90.0f;
};

int main() {
  Player p;
  int coins = 5;
  std::printf("%d %d\n", p.lives, coins);
  return 0;
}
```

**Bài 2.** Thông báo là ``undefined reference to `total_score(int const*, int)'``, nhưng file `score2.cpp` định
nghĩa `total_score(const int *, unsigned)`. Hai kiểu tham số khác nhau (`int` và `unsigned`) nên là hai hàm khác nhau.
Cho hai bên khớp nhau. Bản sửa của `score2.cpp` (`main2.cpp` giữ nguyên), chạy in `60`:

File `score2_fixed.cpp`:

```cpp
int total_score(const int *values, int count) {
  int sum = 0;
  for (int i = 0; i < count; i++)
    sum += values[i];
  return sum;
}
```

**Bài 3.** `gdb` báo `main () at ex3.cpp:20`, dòng `std::printf("target hp = %d\n", target->hp);`. `nearest` trả
`nullptr` khi `wave` rỗng, và `main` không kiểm tra. Bản sửa in `no enemies`:

File `ex3_fixed.cpp`:

```cpp
#include <cstdio>
#include <vector>

struct Enemy {
  int hp = 3;
  float x = 0;
};

Enemy *nearest(std::vector<Enemy> &wave, float x) {
  Enemy *best = nullptr;
  for (Enemy &e : wave)
    if (best == nullptr || e.x - x < best->x - x)
      best = &e;
  return best;
}

int main() {
  std::vector<Enemy> wave;
  Enemy *target = nearest(wave, 10.0f);
  if (target == nullptr)
    std::printf("no enemies\n");
  else
    std::printf("target hp = %d\n", target->hp);
}
```

## Bước tiếp theo

- @ref learn_cmake_basics : viết file build để biên dịch và liên kết nhiều file mà không phải gõ lệnh dài
- @ref learn_cpp_modern : nếu chưa đọc, các cú pháp như `auto [a, b]` và lambda xuất hiện nhiều trong lỗi ở trên
