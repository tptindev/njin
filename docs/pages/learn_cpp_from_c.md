# Bài 4: C++ hơn C ở những gì {#learn_cpp_from_c}

**Bài này dạy gì**: những thứ C++ thêm vào C mà code njin dùng hằng ngày: tham chiếu (`&`), `const`,
namespace, `std::string`, `std::vector`, `struct` có hàm thành viên, nạp chồng hàm, tham số mặc định, `nullptr`.

**Cần biết trước**: C cơ bản: biến, hàm, `struct`, con trỏ, nhiều file với header (xem @ref learn_c_memory và
@ref learn_c_project).

Mọi ví dụ biên dịch với `g++ -std=c++20 -Wall -Wextra -Wpedantic` (GCC 15.2) và đều đã chạy thật. Đầu ra dưới đây là
đầu ra thật.

## Một chương trình gom hết

Đọc chương trình này trước, rồi ở các mục sau từng phần của nó được giải thích. Mỗi phần in một dòng
`[số]` để bạn biết nó nằm ở đâu:

@include learn_cpp_from_c.cpp

Chạy được sẽ in:

```
[1] tham chieu
a.x = 111
[2] const
length_squared = 25
[3] namespace
3 Menu
lives = 3
[4] struct
scale = 1.0, at_origin = false
[5] nap chong va tham so mac dinh
show(int) = 7
show(float) = 7.5
show(const char *) = bay
spawn(zoom=1.0, pos=0,0)
spawn(zoom=3.0, pos=0,0)
spawn(zoom=2.0, pos=10,20)
[6] nullptr
hit = 20, miss is nullptr
[7] string va vector
hero_1 (6 ky tu)
slime (5 ky tu)
bat (3 ky tu)
helper = 42
```

## 1. Tham chiếu

Trong C, để hàm sửa được biến của người gọi, bạn truyền **con trỏ**: `move_by_pointer(&a, 10)` (lấy địa chỉ) và
trong hàm viết `pos->x`. C++ thêm **tham chiếu**: `vec2 &pos`. Tham chiếu là **một tên khác của cùng một biến**:

```cpp
void move_by_ref(vec2 &pos, float dx) { pos.x += dx; }  // gọi: move_by_ref(a, 100.0f)
```

Không cần `&` lúc gọi, không cần `->` trong hàm. Trong ví dụ, `a.x` bắt đầu bằng 1, cộng 10 qua con trỏ và 100 qua
tham chiếu, ra 111.

Hai khác biệt với con trỏ đáng nhớ:

- Tham chiếu **luôn phải có đối tượng**. Không có "tham chiếu null" và không khai báo được mà không khởi tạo:

File `refnull.cpp`:

```cpp
int main() {
  int lives = 3;
  int &alias;
  int &r = lives;
  r = 5;
  return lives;
}
```

```
refnull.cpp:3:8: error: 'alias' declared as reference but not initialized
    3 |   int &alias;
      |        ^~~~~
```

- Tham chiếu **không đổi đối tượng được nữa**. Viết `r = b` **không** làm `r` chỉ sang `b`: nó chép giá trị của `b`
  vào biến mà `r` đang là tên khác của nó.

File `reseat.cpp`:

```cpp
#include <cstdio>

int main() {
  int a = 1, b = 2;
  int &r = a;
  r = b;          // KHÔNG đổi r sang b: chép b vào a
  r = 10;
  std::printf("a=%d b=%d\n", a, b);
}
```

```
a=10 b=2
```

**Trong njin**: chữ ký `void njin_run(njin_ctx &ctx)` nói "cần một context, và nó phải tồn tại". Hàm tạo thì trả
về **con trỏ**, vì đến lúc đó nó chưa có, và hàm hủy nhận con trỏ được phép null:

```cpp
njin::njin_ctx *ctx = njin::njin_create(cfg);  // con trỏ
njin::njin_run(*ctx);                          // *ctx: từ con trỏ ra "chính đối tượng", để làm tham chiếu
njin::njin_destroy(ctx);                       // nhận con trỏ; nullptr thì bị bỏ qua
```

Quy tắc dễ nhớ: **tham chiếu khi "phải có"**, **con trỏ khi "có thể không có"**.

## 2. `const`

`const` là lời hứa "không sửa cái này", và trình biên dịch kiểm tra lời hứa. Ba chỗ hay gặp:

```cpp
float length_squared(const vec2 &v);    // tham số: hàm hứa chỉ đọc v
const vec2 b{3.0f, 4.0f};               // biến: không ai sửa được b
bool at_origin() const { ... }          // hàm thành viên: hứa không sửa đối tượng
```

`const vec2 &` là kiểu tham số bạn sẽ thấy nhiều nhất: **truyền không sao chép** (như tham chiếu) mà **không cho
sửa**. Với một struct lớn, nó rẻ hơn truyền theo giá trị.

Hứa rồi mà vi phạm thì trình biên dịch báo lỗi ngay, xem thông báo thật ở @ref learn_errors (mục "Vi phạm `const`").

**Trong njin**, `const` cho bạn biết hàm có thay đổi engine hay không, ngay từ chữ ký, không cần đọc thân hàm:

```cpp
f32 time_scale(const njin_ctx &ctx);              // chỉ hỏi
void time_set_scale(njin_ctx &ctx, f32 scale);    // sẽ thay đổi
```

## 3. Namespace

Namespace là một **họ** tên: cùng một tên có thể tồn tại ở hai namespace mà không đụng nhau. C không có, nên thư viện C
phải đặt tiền tố (`SDL_Init`, `rl_...`). C++ viết `nhóm::tên`:

```cpp
namespace game {
int lives = 3;
namespace ui {
const char *title() { return "Menu"; }
}
}
// dùng: game::lives, game::ui::title()
```

`using namespace game;` cho phép bỏ tiền tố. Đặt nó trong một khối `{ }` thì chỉ có hiệu lực trong khối đó (như ví dụ ở
mục `[3]`).

**Trong njin**, mọi thứ của engine ở `njin::`. Game viết `using namespace njin;` ở **đầu file `.cpp`, trong
namespace vô danh** (mục 8), để khỏi gõ `njin::` trước mỗi tên. Ví dụ ở `src/games/pong/pong.cpp`:

```cpp
namespace pong {
namespace {
using namespace njin;
...
```

Đừng viết `using namespace` trong **header**: mọi file `#include` header đó cũng bị kéo theo, và tên có thể
đụng nhau ở chỗ bạn không ngờ.

## 4. `struct` có giá trị mặc định và hàm thành viên

`struct` trong C++ làm được nhiều hơn dữ liệu: có thể có **giá trị mặc định** cho từng trường và **hàm thành
viên**.

```cpp
struct transform {
  vec2 pos{};             // {} = mọi trường bằng 0
  float rot = 0.0f;
  float scale = 1.0f;

  void translate(float dx, float dy) { pos.x += dx; pos.y += dy; }
  bool at_origin() const { return pos.x == 0.0f && pos.y == 0.0f; }
};
```

`transform t;` tạo đối tượng với mọi trường đã có giá trị đúng, không cần hàm khởi tạo. `t.translate(5, 0)` gọi hàm
trên chính `t` và `pos` trong hàm là `t.pos`. Hàm `at_origin` có `const` sau dấu ngoặc: nó hứa không sửa `t`, nên gọi
được cả trên đối tượng `const`.

**Trong njin**, đây chính là `njin::transform` (`src/engine/api/_comps.h`), một component cơ bản, hầu như giống hệt đoạn trên
(chỉ không có hàm thành viên):

```cpp
struct transform {
  vec2 pos{};       ///< Vị trí trong thế giới.
  f32 rot = 0.0f;   ///< Góc xoay tính bằng độ, theo chiều kim đồng hồ.
  f32 scale = 1.0f; ///< Tỉ lệ. 1 là kích thước gốc.
};
```

`njin::platformer_body` là một `struct` cùng kiểu với hàng chục trường có giá trị mặc định (`run_speed = 110.0f`,
`gravity = 1000.0f`...). Vì vậy `reg.emplace<platformer_body>(player)` cho nhân vật nhảy được ngay, không cần điền gì.

## 5. Nạp chồng hàm và tham số mặc định

C++ cho phép **nhiều hàm cùng tên** nếu khác kiểu tham số (nạp chồng, overload). Trình biên dịch chọn theo đối số:

```cpp
void show(int v);          // show(7)
void show(float v);        // show(7.5f)
void show(const char *v);  // show("bay")
```

Nếu không chọn được bản nào tốt nhất thì báo lỗi. `7.5` là `double`, đổi sang `int` hay `float` đều được, nên:

File `ambig2.cpp`:

```cpp
#include <cstdio>

void show(int v) { std::printf("int %d\n", v); }
void show(float v) { std::printf("float %.1f\n", v); }

int main() {
  show(7.5);
}
```

```
ambig2.cpp:7:7: error: call of overloaded 'show(double)' is ambiguous
    7 |   show(7.5);
      |   ~~~~^~~~~~
ambig2.cpp:7:7: note: there are 2 candidates
ambig2.cpp:3:6: note: candidate 1: 'void show(int)'
ambig2.cpp:4:6: note: candidate 2: 'void show(float)'
```

Sửa bằng cách đưa đối số về đúng kiểu: `show(7.5f)`.

**Tham số mặc định** cho phép bỏ đối số cuối: `void spawn(float zoom = 1.0f, vec2 pos = {})` gọi được là
`spawn()`, `spawn(3.0f)` hay `spawn(2.0f, {10, 20})` (xem ba dòng `spawn(...)` ở đầu ra). Giá trị mặc định chỉ ghi
**một lần**, ở chỗ khai báo (trong header), không lặp lại ở phần định nghĩa:

File `defarg2.cpp`:

```cpp
void spawn(float zoom = 1.0f);

void spawn(float zoom = 1.0f) { (void)zoom; }

int main() { spawn(); }
```

```
defarg2.cpp:3:6: error: default argument given for parameter 1 of 'void spawn(float)' [-fpermissive]
    3 | void spawn(float zoom = 1.0f) { (void)zoom; }
      |      ^~~~~
defarg2.cpp:1:6: note: previous specification in 'void spawn(float)' here
```

**Trong njin**, `camera_spawn` dùng cả hai tham số mặc định, nên `camera_spawn(ctx)`, `camera_spawn(ctx, 3.0f)` và
`camera_spawn(ctx, 3.0f, {100, 50})` đều hợp lệ:

```cpp
entt::entity camera_spawn(njin_ctx &ctx, f32 zoom = 1.0f, vec2 pos = {});
```

Nạp chồng cũng có trong njin: `njin_mod_register` có ba bản, nhận một module, một danh sách module, hoặc một
`std::span` các module; `render_texture_begin` có hai bản, có hoặc không có màu xóa. Còn khi hai việc thật sự khác nhau,
engine dùng tên khác nhau: `sound_play_once`, `sound_play_loop`, `sound_play_at`.

## 6. `bool` và `nullptr`

C++ có sẵn kiểu `bool` (`true`, `false`), không cần `<stdbool.h>`. Và có `nullptr`, con trỏ null **có kiểu**, thay cho `0`
hay `NULL`. Ví dụ `[6]` dùng nó theo cách phổ biến nhất: hàm tìm kiếm trả về **con trỏ tới phần tử nếu thấy, hoặc
`nullptr` nếu không**:

```cpp
const int *find_value(const std::vector<int> &values, int wanted);
// ...
const int *miss = find_value(scores, 99);
if (miss == nullptr) { /* không có */ }
```

Đây là cách njin, và EnTT mà njin dùng, cho "có thể không có": `registry.try_get<hp>(e)` trả về `hp *`, null nếu entity
không có component đó. Cách khác là "id 0 nghĩa là không hợp lệ" của các handle, xem @ref learn_cpp_types.

## 7. `std::string` và `std::vector`

Đây là hai kiểu của **thư viện chuẩn** bạn sẽ dùng nhiều nhất, thay cho mảng ký tự và mảng cấp phát tay của C. Chúng tự
quản lý bộ nhớ: bạn không `malloc`/`free`.

```cpp
std::string name = "hero";
name += "_1";                                   // nối chuỗi
std::vector<std::string> names{name, "slime"};  // danh sách khởi tạo
names.push_back("bat");                         // thêm vào cuối, tự lớn ra
for (const std::string &n : names)              // duyệt, không sao chép từng chuỗi
  std::printf("%s (%zu ky tu)\n", n.c_str(), n.size());
```

Hai điều cần biết:

- **`.c_str()`** đổi `std::string` sang `const char *` cho các hàm kiểu C. Hầu hết hàm njin nhận đường dẫn là
  `const char *path`. Ví dụ Pong giữ đường dẫn file lưu trong một `std::string` rồi truyền `.c_str()`:

  ```cpp
  g.save_file = save_path(ctx, "best_rally.txt");            // save_path trả về std::string
  file_read(g.save_file.c_str(), text);                       // file_read nhận const char *
  ```

  Con trỏ `.c_str()` trả về chỉ đúng chừng nào chuỗi còn sống và không bị sửa: đừng giữ nó lâu, xem
  @ref learn_cpp_types.
- Vòng lặp `for (const std::string &n : names)` dùng **tham chiếu `const`**: không sao chép mỗi chuỗi, và không
  sửa. Bỏ `&` thì mỗi vòng lặp chép cả chuỗi.

`std::vector` là kiểu dữ liệu chính của njin khi cần "danh sách": ví dụ `std::vector<sys_fnc> after{}` trong
`njin::sys_desc` (danh sách system phải chạy trước).

## 8. Header, `#pragma once` và namespace vô danh

Header trong C++ giống C: khai báo ở `.h`, định nghĩa ở `.cpp`. Khác ở chỗ chống include hai lần: thay vì

```c
#ifndef NAME_H
#define NAME_H
...
#endif
```

hầu như mọi header của njin bắt đầu bằng một dòng:

```cpp
#pragma once
```

Khi định nghĩa một hàm trong header, thêm `inline`, không thì hai file `.cpp` cùng include sẽ đụng nhau lúc liên kết
(xem @ref learn_errors, mục `multiple definition`).

**Namespace vô danh** giải quyết vấn đề tương tự cho hàm chỉ dùng **trong một file**. Hai file cùng có một hàm `helper`:

File `one.cpp`:

```cpp
int helper() { return 1; }
int from_one() { return helper() * 10; }
```

File `two.cpp`:

```cpp
int helper() { return 2; }
int from_two() { return helper() * 10; }
```

File `main.cpp`:

```cpp
#include <cstdio>

int from_one();
int from_two();

int main() {
  std::printf("%d %d\n", from_one(), from_two());
}
```

Biên dịch `g++ one.cpp two.cpp main.cpp` thì liên kết báo lỗi:

```
...two.cpp:(.text+0x0): multiple definition of `helper()'; ...one.cpp:...
collect2.exe: error: ld returned 1 exit status
```

Đặt `helper` trong `namespace { ... }` ở mỗi file, không đổi gì khác:

File `one2.cpp`:

```cpp
namespace {
int helper() { return 1; }
} // namespace

int from_one() { return helper() * 10; }
```

File `two2.cpp`:

```cpp
namespace {
int helper() { return 2; }
} // namespace

int from_two() { return helper() * 10; }
```

Bây giờ `g++ one2.cpp two2.cpp main.cpp` chạy và in:

```
10 20
```

Mọi thứ trong namespace vô danh **chỉ thấy trong file chứa nó**, mỗi file có bản riêng. (Cách kiểu C là `static` trước hàm,
cũng chạy trong C++; C++ ưa namespace vô danh hơn.) Nếu một hàm trong đó không ai gọi, GCC cảnh báo
`defined but not used`, một lợi ích nữa.

**Trong njin**, các file game dùng nó ở khắp nơi. `src/games/pong/pong.cpp` bọc gần cả file (từ dòng 18 đến dòng
398) trong `namespace { ... }`: biến trạng thái `game_state g`, các hàm phụ như `make_blip` và `serve`. Còn
`src/games/platformer/play.cpp` bọc `build_player` và các hàm dựng entity khác. Nhờ vậy những tên đó không đụng nhau
giữa các file và không lộ ra ngoài file chứa chúng.

## Tự kiểm tra

1. Khác biệt giữa `void f(vec2 *p)` và `void f(vec2 &p)` khi gọi và khi dùng trong thân hàm?
2. Vì sao `const vec2 &v` là kiểu tham số phổ biến hơn `vec2 v` cho struct lớn?
3. `int &r = a; r = b;` làm gì: `r` chỉ sang `b`, hay chép `b` vào `a`?
4. Trong njin, nhìn chữ ký `f32 delta(const njin_ctx &ctx)` và `void time_set_scale(njin_ctx &ctx, f32 scale)`, bạn kết luận gì
   chỉ từ `const`?
5. Vì sao không nên viết `using namespace njin;` trong một header?

## Bài tập

**Bài 1.** Viết `void heal(int &hp, int amount, int max_hp)` cộng máu nhưng không vượt `max_hp`, và
`bool is_dead(const int &hp)`. `hp` bắt đầu bằng 8: `heal(hp, 5, 10)` rồi in kết quả.

**Bài 2.** Viết `const std::string *find_name(const std::vector<std::string> &names, const std::string &wanted)`
trả về con trỏ tới tên tìm thấy hoặc `nullptr`. Thử với `{"hero", "slime", "bat"}`, tìm `"slime"` và `"ghost"`.

**Bài 3.** Ba file `one.cpp`, `two.cpp`, `main.cpp` ở mục 8 gây lỗi `multiple definition of 'helper()'`. Sửa mà **không
đổi tên hàm** `helper`.

## Đáp án

**Câu hỏi.**

1. Con trỏ: gọi phải `f(&a)` và dùng `p->x`, có thể null. Tham chiếu: gọi `f(a)` và dùng `p.x`, luôn có đối tượng.
2. Truyền tham chiếu không sao chép cả struct, còn `const` hứa không sửa nên an toàn.
3. Chép `b` vào `a`. Tham chiếu không đổi đối tượng sau khi đã khởi tạo (xem ví dụ `a=10 b=2`).
4. `delta` chỉ **đọc** trạng thái engine; `time_set_scale` **thay đổi** nó.
5. Header bị nhiều file khác include, nên `using namespace` sẽ áp dụng cho cả những file đó, và tên có thể đụng nhau
   ở chỗ không liên quan.

**Bài 1.** Chạy in `hp = 10, dead = no` rồi `dead = yes`:

File `learn_cpp_from_c_ex1.cpp`:

```cpp
#include <cstdio>

// Hồi máu nhưng không vượt quá max_hp. hp là tham chiếu: hàm sửa thẳng biến của người gọi.
void heal(int &hp, int amount, int max_hp) {
  hp += amount;
  if (hp > max_hp)
    hp = max_hp;
}

// Chỉ đọc hp, nên const.
bool is_dead(const int &hp) { return hp <= 0; }

int main() {
  int hp = 8;
  heal(hp, 5, 10);
  std::printf("hp = %d, dead = %s\n", hp, is_dead(hp) ? "yes" : "no");
  int gone = 0;
  std::printf("dead = %s\n", is_dead(gone) ? "yes" : "no");
}
```

**Bài 2.** Chạy in `hit: slime` rồi `miss: (none)`:

File `learn_cpp_from_c_ex2.cpp`:

```cpp
#include <cstdio>
#include <string>
#include <vector>

// Trả con trỏ tới phần tử tìm thấy, hoặc nullptr.
const std::string *find_name(const std::vector<std::string> &names, const std::string &wanted) {
  for (const std::string &n : names)
    if (n == wanted)
      return &n;
  return nullptr;
}

int main() {
  const std::vector<std::string> names{"hero", "slime", "bat"};
  const std::string *hit = find_name(names, "slime");
  const std::string *miss = find_name(names, "ghost");
  std::printf("hit: %s\n", hit ? hit->c_str() : "(none)");
  std::printf("miss: %s\n", miss ? miss->c_str() : "(none)");
}
```

**Bài 3.** Đặt `helper` trong namespace vô danh ở cả `one.cpp` và `two.cpp`: chính là hai file `one2.cpp` và
`two2.cpp` ở mục 8, chạy in `10 20`. Cách khác, cũng đúng: thêm `static` trước `int helper()` ở mỗi file.

## Bước tiếp theo

- @ref learn_cpp_types : giá trị và tham chiếu, sao chép và di chuyển, RAII, con trỏ và handle
- @ref learn_errors : đọc lỗi biên dịch và lỗi liên kết
