# Bài 5: Giá trị, tham chiếu và quyền sở hữu {#learn_cpp_types}

**Bài này dạy gì**: sao chép hay dùng chung, di chuyển, RAII, ai sở hữu một tài nguyên và nó sống bao lâu; vì sao
njin dùng handle (id) thay cho con trỏ, và vì sao mọi hàm của engine nhận một `context &`.

**Cần biết trước**: tham chiếu, `const`, `std::vector` và `std::string`, xem @ref learn_cpp_from_c.

Phần lớn lỗi khó tìm trong C++ đến từ hai câu hỏi: **ai sở hữu cái này**, và **nó còn sống không**. Bài này trả lời
hai câu hỏi đó bằng ví dụ chạy được, rồi cho thấy engine njin trả lời chúng như thế nào. Mọi ví dụ biên dịch với
`g++ -std=c++20 -Wall -Wextra -Wpedantic` (GCC 15.2), không có cảnh báo, và đầu ra dưới đây là đầu ra thật.

## Một chương trình gom sáu ý

@include learn_cpp_types.cpp

Đầu ra:

```
[1] gia tri va tham chieu
a = 1,50  copy = 100,2
[2] sao chep va di chuyen
copy dung bo nho moi: yes
moved lay bo nho cu:  yes
a.size() sau khi di chuyen = 0
[3] RAII
 play(true):
  load hit.wav
  unload hit.wav
 play(false):
  load hit.wav
  load bgm.ogg
  playing
  unload bgm.ogg
  unload hit.wav
[4] so huu
  enemy 50 appears
  boss is null, owner has hp 50
  enemy 50 gone
  total hp = 7
[5] tham chieu treo
capacity 3 -> 192
phan tu dau da doi cho: yes
scores[index] = 10
[6] co the khong co
optional: 2, empty
pointer:  20, nullptr
handle:   id 0 is invalid
```

## 1. Giá trị và tham chiếu

`vec2 copy = a;` **chép** `a` sang một biến mới: từ đây hai biến độc lập. `vec2 &alias = a;` **không chép**: `alias`
là một tên khác của chính `a`. Ví dụ `[1]` sửa `copy.x` và `alias.y`: `a` chỉ đổi `y` (thành 50), còn `copy.x`
(thành 100) không ảnh hưởng tới `a`.

Cùng quy tắc với **tham số hàm**. `void f(vec2 v)` nhận một bản sao (sửa `v` không ảnh hưởng người gọi);
`void f(vec2 &v)` nhận chính biến (sửa là sửa thật); `void f(const vec2 &v)` nhận chính biến nhưng chỉ được đọc. Chọn
theo câu hỏi: hàm có cần sửa biến của người gọi không? Có: `&`. Không, và kiểu lớn: `const &`. Không, và kiểu nhỏ
như `int`, `float`, `vec2`: nhận theo giá trị.

## 2. Sao chép và di chuyển

Sao chép `std::vector` chép **mọi phần tử** sang vùng nhớ mới. Với vector 1000 số thì còn nhẹ, với vector 1 triệu
phần tử thì đắt. **Di chuyển** (move) thì khác: vector mới **lấy luôn vùng nhớ** của vector cũ, không chép gì.

```cpp
std::vector<int> copy  = a;             // chép: bộ nhớ mới
std::vector<int> moved = std::move(a);  // lấy bộ nhớ của a
```

Ví dụ `[2]` kiểm chứng điều đó: `copy` dùng bộ nhớ khác `a` (`yes`), còn `moved` dùng **đúng vùng nhớ** mà `a` từng
có (`yes`). `std::move` không tự di chuyển gì cả: nó chỉ nói "cho phép lấy tài nguyên của biến này", việc lấy là của
hàm khởi tạo.

Sau khi bị di chuyển, `a` ở trạng thái hợp lệ nhưng **không xác định**: kết quả chạy ở đây là `size() == 0`, nhưng
đừng dựa vào giá trị đó, hãy coi `a` như không còn dùng nữa. Bạn không cần tự viết hàm di chuyển; nhớ hai điều: (1) truyền
và trả về `std::vector`, `std::string` theo giá trị thường rẻ vì đã được di chuyển, (2) `std::move` một biến rồi
dùng lại nó là một lỗi.

## 3. RAII: tài nguyên gắn với đời sống của biến

**RAII** (Resource Acquisition Is Initialization) nghĩa là: lấy tài nguyên trong **hàm khởi tạo**, trả nó trong **hàm
hủy** (`~Tên()`), mà trình biên dịch **luôn gọi** khi biến ra khỏi phạm vi, kể cả khi hàm thoát sớm. Ví dụ `[3]`:

```cpp
bool play(bool fail_early) {
  Sound hit("hit.wav");
  if (fail_early)
    return false;              // ~Sound vẫn chạy: "unload hit.wav"
  Sound bgm("bgm.ogg");
  std::puts("  playing");
  return true;
}                              // hủy ngược thứ tự: bgm rồi hit
```

Trong đầu ra: `play(true)` tải `hit.wav` rồi dỡ nó ngay, dù hàm thoát sớm. `play(false)` dỡ `bgm.ogg` **trước**
`hit.wav`, ngược thứ tự tạo. Không cần `goto cleanup` như C, và không thể quên `free`.

`Sound` khai báo `Sound(const Sound &) = delete;`: **cấm sao chép**. Lý do: nếu hai đối tượng cùng giữ một tài
nguyên, cả hai sẽ giải phóng nó, tức giải phóng hai lần. Thử sao chép sẽ không biên dịch được:

File `copyfail.cpp`:

```cpp
#include <cstdio>

struct Sound {
  Sound() { std::puts("load"); }
  ~Sound() { std::puts("unload"); }
  Sound(const Sound &) = delete;
  Sound &operator=(const Sound &) = delete;
};

int main() {
  Sound a;
  Sound b = a;
}
```

```
copyfail.cpp:12:13: error: use of deleted function 'Sound::Sound(const Sound&)'
   12 |   Sound b = a;
      |             ^
copyfail.cpp:6:3: note: declared here
    6 |   Sound(const Sound &) = delete;
      |   ^~~~~
```

**Trong njin**: `audio_store` (`src/engine/runtime/njin_audio_impl.h`) giữ mọi âm thanh đã nạp. Nó làm đúng như trên:
hàm hủy giải phóng chúng, và sao chép bị cấm với chính lý do đó:

```cpp
struct audio_store {
  std::vector<sound_slot> sounds;
  ...
  audio_store() = default;
  ~audio_store();
  // Copying would free the same audio buffers twice.
  audio_store(const audio_store &) = delete;
  audio_store &operator=(const audio_store &) = delete;
};
```

## 4. Sở hữu: `unique_ptr`, hoặc tốt hơn là giá trị

Con trỏ thô (`Enemy *`) không nói ai phải `delete` nó. **`std::unique_ptr<T>`** nói rõ: đúng một chủ sở hữu, và khi
chủ đó bị hủy thì đối tượng cũng bị hủy. Nó không sao chép được, chỉ **chuyển** quyền bằng `std::move`:

```cpp
auto boss = std::make_unique<Enemy>(50);
std::unique_ptr<Enemy> owner = std::move(boss);  // boss thành null, owner giữ Enemy
```

Đầu ra `[4]` cho thấy `boss is null, owner has hp 50`, và `Enemy` bị hủy **đúng một lần**, khi `owner` ra khỏi khối.
Sao chép thì bị chặn:

File `uniquecopy.cpp`:

```cpp
#include <memory>

struct Enemy { int hp = 3; };

int main() {
  auto a = std::make_unique<Enemy>();
  auto b = a;
}
```

```
uniquecopy.cpp:7:12: error: use of deleted function 'std::unique_ptr<_Tp, _Dp>::unique_ptr(const std::unique_ptr<_Tp, _Dp>&) [with _Tp = Enemy; _Dp = std::default_delete<Enemy>]'
    7 |   auto b = a;
      |            ^
```

Nhưng phần lớn thời gian **bạn không cần con trỏ nào cả**. Ba lựa chọn, theo thứ tự nên thử:

1. **Giá trị**: `std::vector<Slime> wave(3);`. Vector sở hữu các phần tử, hủy chúng khi vector bị hủy; bạn không
   thấy con trỏ nào. Đây là mặc định.
2. **Id hoặc handle** (mục 7): khi thứ được tham chiếu có thể bị xóa, hoặc bị dời chỗ.
3. **`unique_ptr`**: khi cần một đối tượng riêng lẻ, sống lâu hơn phạm vi tạo ra nó, hoặc có kiểu chỉ biết lúc chạy.

**Trong njin**, không có `unique_ptr` nào trong header công khai của engine (`src/engine/api`): entity là id, tài nguyên
là handle, và dữ liệu nằm trong `std::vector` hay trong registry của EnTT.

## 5. Tham chiếu treo (dangling)

Một tham chiếu hay con trỏ **treo** khi thứ nó trỏ tới đã hết sống. Dùng nó là hành vi không xác định: có thể chạy
đúng vài lần rồi sai, có thể chết ngay. Trình biên dịch bắt được một số ca đơn giản:

File `dangling_ref.cpp`:

```cpp
#include <cstdio>

const int &first_score() {
  int local = 7;
  return local;
}

int main() {
  std::printf("%d\n", first_score());
}
```

```
dangling_ref.cpp:5:10: warning: reference to local variable 'local' returned [-Wreturn-local-addr]
    5 |   return local;
      |          ^~~~~
dangling_ref.cpp:4:7: note: declared here
```

Nhưng nhiều ca thì **không có cảnh báo nào**. Ví dụ điển hình: `const char *name = std::string("hero").c_str();`. Chuỗi
tạm bị hủy ngay ở cuối câu lệnh, `name` treo từ dòng kế tiếp. GCC 15.2 với `-Wall -Wextra -Wpedantic` im lặng khi biên
dịch dòng này, nên phải tự nhớ quy tắc: con trỏ từ `.c_str()` chỉ dùng được khi chuỗi còn sống và chưa bị sửa.

**Trường hợp thật, hay gặp nhất trong game: vector cấp phát lại.** `std::vector` giữ phần tử liền nhau. Khi đầy, nó cấp
phát vùng nhớ lớn hơn, chép phần tử sang đó, và **giải phóng vùng cũ**: mọi con trỏ và tham chiếu tới phần tử đều
treo. Ví dụ `[5]` (không đọc con trỏ cũ, chỉ so sánh địa chỉ):

```
capacity 3 -> 192
phan tu dau da doi cho: yes
scores[index] = 10
```

Sức chứa nhảy từ 3 lên 192 qua nhiều lần cấp phát, và phần tử đầu **đã đổi chỗ**: một `int &` giữ từ trước khi đẩy
thêm phần tử sẽ trỏ vào vùng nhớ đã giải phóng. Cách sửa là giữ **chỉ số** (hoặc id) rồi lấy lại phần tử mỗi lần
cần: `scores[index]`. (Con số 192 phụ thuộc thư viện chuẩn; cái không đổi là sức chứa tăng và địa chỉ đổi.)

**Đây chính là điều trang @ref ecs cảnh báo**: "Đừng giữ tham chiếu tới component qua nhiều frame: thêm hoặc bớt component
có thể làm dữ liệu dời chỗ. Lấy lại từ registry mỗi frame." Registry của EnTT không phải một `std::vector` đơn giản, nên
mình đo thử trên EnTT v4.0.0 mà njin đang dùng, thay vì đoán:

File `entt_move.cpp` (cần EnTT, biên dịch với `-isystem` trỏ tới thư mục `src` của EnTT):

```cpp
#include <cstdint>
#include <cstdio>
#include <entt/entity/registry.hpp>

struct hp { int value = 3; };

int main() {
  entt::registry reg;
  const entt::entity first = reg.create();
  reg.emplace<hp>(first, hp{10});
  const auto a0 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(first));

  // 1. Thêm thật nhiều component cùng kiểu.
  for (int i = 0; i < 100000; i++)
    reg.emplace<hp>(reg.create(), hp{i});
  const auto a1 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(first));
  std::printf("after adding 100000 hp: first moved = %s\n", a0 != a1 ? "yes" : "no");

  // 2. Bớt component của một entity khác.
  const entt::entity second = reg.create();
  reg.emplace<hp>(second, hp{5});
  const entt::entity third = reg.create();
  reg.emplace<hp>(third, hp{6});
  const auto b0 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(third));
  reg.remove<hp>(second);
  const auto b1 = reinterpret_cast<std::uintptr_t>(&reg.get<hp>(third));
  std::printf("after removing another entity's hp: third moved = %s, value still %d\n",
              b0 != b1 ? "yes" : "no", reg.get<hp>(third).value);
}
```

```
after adding 100000 hp: first moved = no
after removing another entity's hp: third moved = yes, value still 6
```

Kết quả bất ngờ và đáng nhớ: **thêm** component cùng kiểu không làm phần tử cũ dời chỗ (khác `std::vector`), nhưng **bớt**
component của một entity thì làm component của entity *khác* dời chỗ: EnTT lấp chỗ trống bằng phần tử cuối rồi thu
ngắn mảng. Một `hp &` giữ cho `third` từ trước lời gọi `remove` giờ trỏ vào chỗ không còn thuộc về nó. Giá trị vẫn đúng
(`value still 6`) khi lấy lại bằng `reg.get`. Vì vậy quy tắc an toàn không đổi và không phụ thuộc chi tiết bên trong: game
njin **lưu `entt::entity` (một id) chứ không lưu `transform &`**, và gọi `reg.get<transform>(e)` mỗi khi cần.

## 6. Ba cách nói "có thể không có"

Một hàm tìm kiếm có thể không tìm thấy. Có ba cách trả lời trong C++, cả ba xuất hiện ở ví dụ `[6]`:

| Cách | Ví dụ | Khi nào |
|---|---|---|
| Con trỏ, `nullptr` là "không có" | `const int *find_ptr(...)` | Kết quả là một phần tử nằm sẵn trong container. Kiểu của EnTT: `try_get<T>(e)` |
| `std::optional<T>` | `std::optional<int> find_index(...)` | Kết quả là một **giá trị** mới tính ra. `has_value()`, `value_or(x)` |
| Handle với `id == 0` | `struct sound_handle { unsigned id = 0; }` | Định danh cho tài nguyên. Cách của njin |

Ví dụ `find_index` trả `std::optional<int>`: `2` khi thấy, "rỗng" khi không, không cần giá trị đặc biệt kiểu `-1`. Ba
cách này không hơn nhau; chọn theo chỗ dùng. njin dùng loại thứ ba cho tài nguyên, xem tiếp.

## 7. Handle: kho slot và id không bao giờ dùng lại

Tài nguyên như âm thanh, texture, shader nằm trong engine. Game không được cầm con trỏ tới chúng (mục 5: con trỏ tới
phần tử vector có thể treo). Thay vào đó `sound_load` trả về một **handle**: một `struct` chỉ chứa một số `id`. Trong
`src/engine/api/_types.h`:

```cpp
struct sound_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};
```

`id == 0` là "không có" (nạp lỗi, file thiếu). Phía trong, engine giữ một `std::vector` các **slot**, và id N là slot
`N - 1`. Quy tắc quan trọng, viết ngay đầu `njin_audio_impl.h`: **slot không bao giờ được dùng lại**, nên một handle cũ
không thể vô tình trỏ vào tài nguyên mới. Đây là kho thu nhỏ theo đúng cách đó:

@include learn_cpp_handles.cpp

Đầu ra:

```
hit.id = 1, bgm.id = 2
coin.id = 3 (khong dung lai id cua hit)
hit con hop le: no
bgm con hop le: yes
coin volume = 1.0
slot cua bgm da doi cho: yes
bgm van dung duoc qua handle: yes
```

Đọc kết quả:

- Sau `sound_unload(hit)`, nạp `coin` nhận `id 3`, **không** chiếm lại `id 1`. Handle cũ `hit` tra ra `nullptr`, và các
  hàm nhận handle xấu (`sound_set_volume` với `hit` hay với `{}`) **bỏ qua** thay vì gây lỗi.
- Sau khi nạp thêm 100 âm thanh, slot của `bgm` **đã dời chỗ** (vector cấp phát lại), một con trỏ giữ từ trước sẽ treo.
  Nhưng handle `bgm` vẫn dùng được vì nó là một số, tra lại mỗi lần.

Hàm tra cứu của njin y hệt bản thu nhỏ trên:

```cpp
inline sound_slot *sound_slot_of(audio_store &store, sound_handle handle) {
  if (handle.id == 0 || handle.id > store.sounds.size())
    return nullptr;
  sound_slot &slot = store.sounds[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}
```

Đây là lý do tài liệu API của njin ghi "handle id 0 là không hợp lệ, mọi hàm nhận handle không hợp lệ đều không làm gì",
và đây là cái giá: mỗi lần dùng phải tra một lần. Đổi lại game không bao giờ phải nghĩ tới vòng đời của tài nguyên
bên trong engine.

Con trỏ mà `sound_slot_of` trả về cũng chỉ dùng **ngay**, không lưu: đó chính là quy tắc "lấy lại mỗi lần" ở mục 5.

## 8. Vì sao njin truyền `context &` thay vì dùng biến toàn cục

Trạng thái của engine (cửa sổ, kho tài nguyên, thời gian, registry...) nằm trong một đối tượng, `context`, và mọi hàm
của engine nhận nó. Header `njin_ctx.h` ghi:

```cpp
// Opaque: created with create (njin.h), only accessed through the
// functions below.
struct context;
```

và mọi system của game là `void system(context &ctx)`. So sánh hai cách giữ trạng thái:

@include learn_cpp_context.cpp

Đầu ra:

```
toan cuc: frame = 2, 2 su kien
game:   frame = 3, su kien cuoi = 30
replay: frame = 1, su kien cuoi = 10
```

Biến toàn cục (`g_frame`) chỉ có **một** bộ trạng thái cho cả chương trình. `Ctx` thì tạo bao nhiêu cũng được, mỗi
cái độc lập: `game` và `replay` chạy song song mà không ảnh hưởng nhau. Truyền context còn làm **phụ thuộc hiện rõ ngay
trong chữ ký** hàm (`last_event(const Ctx &)` nói nó đọc context, không sửa), và dễ kiểm thử hơn, vì tạo một `Ctx` mới
cho mỗi bài thử.

Đây là đánh đổi chứ không phải luật: với game nhỏ, biến toàn cục đơn giản hơn. Game của njin cũng dùng biến toàn cục cho trạng
thái **riêng của game** (`game_state g;` trong `src/games/pong/pong.cpp`), còn những gì **engine** sở hữu thì nằm trong
`context`, cái giá là viết thêm `ctx` ở đầu mỗi hàm. Để ý thêm: `context` được khai báo mà không định nghĩa trong header công khai
("opaque"). Game không thể tự tạo hay đọc trường bên trong, chỉ dùng được các hàm engine cho, nên engine đổi cách lưu mà
không phá game.

## Tự kiểm tra

1. `void f(vec2 v)`, `void f(vec2 &v)` và `void f(const vec2 &v)` khác nhau thế nào khi hàm sửa `v.x`?
2. Sau `auto b = std::move(a);` (với `a` là `std::vector`), bạn được làm gì với `a`?
3. Vì sao `Sound(const Sound &) = delete;` hợp lý cho một lớp giữ tài nguyên?
4. Đoạn này sai ở đâu? `int &first = scores[0]; scores.push_back(5); first = 1;`
5. Vì sao handle của njin dùng id 0 làm "không hợp lệ", và vì sao slot không được dùng lại?

## Bài tập

**Bài 1.** Đoán đầu ra rồi chạy để kiểm tra:

```cpp
#include <cstdio>

struct vec2 { float x, y; };

void bump(vec2 v) { v.x += 1; }
void bump_ref(vec2 &v) { v.x += 1; }

int main() {
  vec2 a{0, 0};
  bump(a);
  bump_ref(a);
  bump_ref(a);
  std::printf("%.0f\n", a.x);
}
```

**Bài 2.** Viết `struct Scope` in `enter <tên>` ở hàm khởi tạo và `leave <tên>` ở hàm hủy. Tạo hai `Scope` lồng nhau
(`outer` rồi `inner` trong một khối `{ }` bên trong) và đoán thứ tự dòng in.

**Bài 3.** Viết `unsigned find_id(const std::vector<std::string> &names, const std::string &wanted)` trả về `id` theo kiểu
njin: vị trí cộng 1 nếu thấy, **0** nếu không. Thử với `{"hero", "slime", "bat"}`, tìm `"bat"` và `"ghost"`.

## Đáp án

**Câu hỏi.**

1. `v` theo giá trị: sửa bản sao, người gọi không thấy. `vec2 &`: sửa chính biến của người gọi. `const vec2 &`: không
   biên dịch được, vì `const` cấm sửa.
2. Coi `a` như đã bỏ. Nó hợp lệ để hủy hoặc gán lại, nhưng đừng đọc giá trị, vì trạng thái không xác định.
3. Nếu sao chép được thì hai đối tượng cùng giữ một tài nguyên và cùng giải phóng nó, tức hai lần.
4. `first` là tham chiếu tới phần tử của vector. `push_back` có thể làm vector cấp phát lại, khi đó `first` treo, và `first = 1`
   ghi vào bộ nhớ đã giải phóng. Sửa: giữ chỉ số `0`, ghi `scores[0] = 1`.
5. Id 0 cho phép dùng `{}` (mặc định) làm "không có" mà không cần giá trị đặc biệt khác. Slot không dùng lại để handle
   cũ không bao giờ trỏ nhầm sang tài nguyên mới nạp sau đó.

**Bài 1.** In `2`. `bump(a)` chỉ sửa bản sao. Hai lần `bump_ref` mỗi lần cộng 1 vào `a.x` thật.

**Bài 2.** Hủy theo thứ tự ngược lại lúc tạo. Đây là một bản (có thêm dòng `between` giữa hai lần hủy để thấy rõ
`inner` bị hủy ở dấu `}` của khối):

File `learn_cpp_types_ex2.cpp`:

```cpp
#include <cstdio>

struct Scope {
  const char *name;
  explicit Scope(const char *n) : name(n) { std::printf("enter %s\n", name); }
  ~Scope() { std::printf("leave %s\n", name); }
};

int main() {
  Scope outer("outer");
  {
    Scope inner("inner");
  }
  std::puts("between");
}
```

Chạy in `enter outer`, `enter inner`, `leave inner`, `between`, `leave outer`: `inner` bị hủy ở dấu `}` của khối, còn
`outer` ở cuối `main`.

**Bài 3.** Chạy in `bat -> 3` rồi `ghost -> 0`:

File `learn_cpp_types_ex3.cpp`:

```cpp
#include <cstdio>
#include <string>
#include <vector>

// Trả id kiểu njin: vị trí + 1 nếu thấy, 0 nếu không (0 nghĩa là không hợp lệ).
unsigned find_id(const std::vector<std::string> &names, const std::string &wanted) {
  for (std::size_t i = 0; i < names.size(); i++)
    if (names[i] == wanted)
      return static_cast<unsigned>(i + 1);
  return 0;
}

int main() {
  const std::vector<std::string> names{"hero", "slime", "bat"};
  std::printf("bat -> %u\n", find_id(names, "bat"));
  std::printf("ghost -> %u\n", find_id(names, "ghost"));
}
```

## Bước tiếp theo

- @ref learn_cpp_modern : `auto`, lambda, structured binding, `constexpr`, những thứ làm code njin đọc gọn
- @ref learn_errors : đọc lỗi biên dịch và lỗi liên kết
