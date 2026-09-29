# Bài 2: Bộ nhớ, con trỏ và chuỗi {#learn_c_memory}

**Bài này dạy gì:** con trỏ, mảng, `struct`, chuỗi C, stack và heap (`malloc`/`free`), và năm lỗi bộ nhớ kinh điển, kèm cách bắt chúng.

**Cần biết trước:** @ref learn_c_start (kiểu dữ liệu, `printf`, hàm, vòng lặp).

Đây là phần khó nhất của C, và cũng là phần giải thích vì sao code njin viết như nó viết: dùng handle thay vì con
trỏ, `const char *` cho đường dẫn, chuỗi định dạng có kiểm tra. Cách học tốt nhất là chạy từng ví dụ. Mọi ví dụ
dưới đây đã biên dịch với `gcc -std=c17 -Wall -Wextra` (GCC 15.2, Windows) và **cả tám ví dụ "lành" cũng đã chạy sạch
dưới AddressSanitizer và UBSan** (GCC 15.2 trong WSL Ubuntu, xem mục "Bắt lỗi bằng sanitizer").

## Bộ nhớ là một dãy ô có địa chỉ

Mỗi biến sống ở đâu đó trong bộ nhớ, và chỗ đó có một **địa chỉ** (một con số). Một **con trỏ** là biến giữ một
địa chỉ. Hai toán tử đi cặp với nhau: `&x` là "địa chỉ của `x`", `*p` là "đi tới địa chỉ `p` giữ".

@include learn_c_pointers.c

```
lives=5, *p=5, p==&lives: 1
speed=150.0
sizeof(scores)=20, so phan tu=5
khoang cach giua scores[1] va scores[0] = 4 byte
scores[2]=30, *(scores+2)=30
sum=150
nothing la NULL, khong duoc dung *nothing
```

Những điều cần rút ra:

- `*p = 5` ghi vào chính `lives`. Đây là cách một hàm **sửa biến của người gọi**: `scale(&speed, 1.5f)` truyền địa chỉ, hàm ghi qua địa chỉ đó. Nếu truyền `speed` thường, hàm chỉ nhận một bản sao.
- Mảng là các phần tử nằm **sát nhau**: `scores[1]` cách `scores[0]` đúng 4 byte, bằng `sizeof(int)`. `scores[i]` chính là `*(scores + i)`, và cộng con trỏ tính theo phần tử, không theo byte.
- `NULL` là "không trỏ tới đâu". Đọc hay ghi qua con trỏ `NULL` là lỗi. Luôn kiểm tra trước khi dùng nếu con trỏ có thể `NULL`.
- `const int *values` nghĩa là "hàm này chỉ đọc các phần tử". Ghi vào chúng là lỗi biên dịch.

### Mảng "suy biến" thành con trỏ

Đưa một mảng vào hàm, hàm chỉ nhận **địa chỉ phần tử đầu**, và không còn biết mảng dài bao nhiêu:

```c
static size_t bad_count(const int values[]) { return sizeof(values) / sizeof(values[0]); }
...
int scores[5] = {10, 20, 30, 40, 50};
printf("trong main: %zu phan tu\n", sizeof(scores) / sizeof(scores[0]));
printf("trong ham : %zu phan tu\n", bad_count(scores));
```

```
decay.c:4:60: warning: 'sizeof' on array function parameter 'values' will return size of 'const int *' [-Wsizeof-array-argument]
trong main: 5 phan tu
trong ham : 2 phan tu
```

Trong hàm, `sizeof(values)` là 8 (cỡ một con trỏ trên máy 64 bit), nên kết quả sai (2). Cảnh báo
`-Wsizeof-array-argument` bắt được. Quy tắc: **luôn truyền độ dài kèm theo mảng** (`sum(scores, 5)`).

## `struct`: gom dữ liệu

@include learn_c_struct.c

```
sau boost_copy: jump_speed=350
sau boost:      jump_speed=450
do cao nhay=101.2, sizeof(body_config)=12 byte
```

- **Truyền theo giá trị** (`boost_copy(body)`): hàm nhận một bản sao. Sửa bản sao không đụng đến `body` của người gọi, nên `jump_speed` vẫn là 350.
- **Truyền con trỏ** (`boost(&body)`): hàm sửa bản gốc. `body->jump_speed` là cách viết gọn của `(*body).jump_speed`.
- **`const body_config *`**: truyền con trỏ để khỏi sao chép cả struct, mà vẫn cam kết không sửa.
- **Khởi tạo có tên** (`.run_speed = 110.0f`): mỗi trường có tên, trường nào không ghi thì bằng 0. `#define BODY_DEFAULT {...}` cho một bộ giá trị mặc định, rồi chỉnh từng trường: `body.jump_speed = 350.0f;`.

Đây chính là cách `njin::platformer_body` được dùng. Trong C++ các giá trị mặc định nằm ngay trong struct
(`f32 run_speed = 110.0f;`, `f32 jump_speed = 300.0f;`, `f32 gravity = 1000.0f;` trong `njin_body.h`), và game viết:

```cpp
njin::platformer_body body{};
body.jump_speed = 350.0f;
```

(C++ được bài @ref learn_cpp_from_c nói tới.)

## Chuỗi C

C không có kiểu "chuỗi". Chuỗi là **một mảng `char` kết thúc bằng byte 0** (`'\0'`).

@include learn_c_strings.c

```
strlen(name)=6, sizeof(name)=16
cac byte cua name: 83 112 114 111 117 116 0 0
line="Sprout la Rung " (can 23 ky tu, buffer 16)
bi cat cut!
a == b: 0, strcmp(a, b) == 0: 1
```

Trình biên dịch còn cảnh báo về chính dòng `snprintf` đó, ngay lúc biên dịch:

```
warning: '%s' directive output truncated writing 13 bytes into a region of size between 0 and 12 [-Wformat-truncation=]
```

Cần nhớ:

| Điều | Nghĩa |
|---|---|
| `char name[16] = "Sprout"` | một mảng 16 byte của riêng bạn, sửa được. Chuỗi "Sprout" chiếm 7 byte (6 ký tự và `'\0'`); phần còn lại là 0 |
| `const char *title = "..."` | một con trỏ tới chuỗi hằng có sẵn trong chương trình. Đọc được, **không được sửa** |
| `strlen(name)` | đếm ký tự đến `'\0'` (6), khác với `sizeof(name)` (16, cỡ mảng) |
| `snprintf(buf, size, fmt, ...)` | in vào `buf` nhưng không bao giờ ghi quá `size`, luôn thêm `'\0'`, và trả về độ dài **cần có**: nếu `>= size` thì đã bị cắt |
| `a == b` với chuỗi | so **địa chỉ**, gần như luôn sai ý bạn. So nội dung bằng `strcmp(a, b) == 0` |

Hãy tránh `strcpy` và `sprintf` (không có giới hạn: chuỗi dài hơn buffer sẽ ghi tràn ra ngoài). Dùng `snprintf`
với `sizeof(buf)`.

## Stack và heap

- **Stack**: biến cục bộ (`int lives`, `char name[16]`). Tự cấp phát khi vào hàm, **tự biến mất khi ra khỏi hàm**. Nhanh, nhưng nhỏ và ngắn ngủi.
- **Heap**: vùng bạn tự xin bằng `malloc` và tự trả bằng `free`. Sống cho đến khi bạn trả. Dùng khi cần kích thước chỉ biết lúc chạy, hoặc dữ liệu sống lâu hơn hàm tạo ra nó.

@include learn_c_heap.c

```
mo rong len 4 phan tu
mo rong len 8 phan tu
10 20 30 40 50 
```

Quy tắc cho heap:

1. **Kiểm tra `malloc` có trả về `NULL` không**: hết bộ nhớ là có thật.
2. **Mỗi `malloc` có đúng một `free`** (`realloc` xin lại vùng lớn hơn và giữ nội dung; con trỏ cũ không dùng nữa).
3. Sau `free`, gán con trỏ về `NULL`, để dùng nhầm thì sập ngay chứ không âm thầm sai.
4. Gán kết quả `realloc` vào biến **tạm** (`bigger`) trước: nếu thất bại nó trả `NULL`, còn `values` cũ vẫn cần được `free`.

## Năm lỗi bộ nhớ kinh điển

Điểm chung của cả năm: chương trình vi phạm luật, và C **không dừng lại** để báo cho bạn. Hành vi lúc đó gọi
là **không xác định** (undefined behavior): có thể chạy đúng, in rác, sập, hoặc chỉ sai ở máy người khác. Dưới đây mỗi
lỗi có một chương trình nhỏ trong `docs/examples/`. Kết quả ghi bên dưới là **kết quả thật**: "thường" là GCC 15.2
trên Windows không có sanitizer, "sanitizer" là GCC 15.2 trong WSL Ubuntu với `-fsanitize=address`.

### 1. Ghi ra ngoài mảng

@include learn_c_bug_oob.c

Mảng 4 phần tử có chỉ số 0..3, mà vòng lặp ghi tới `scores[4]`.

- **Thường**: in `xong`, thoát bình thường, không có cảnh báo nào. Lỗi vẫn nằm đó, chỉ là lần này nó chưa làm hỏng thứ gì nhìn thấy được.
- **Sanitizer**:

```
ERROR: AddressSanitizer: heap-buffer-overflow on address 0x75a7cf0e0020 ...
WRITE of size 4 at 0x75a7cf0e0020 thread T0
    #0 ... in main learn_c_bug_oob.c:10
0x75a7cf0e0020 is located 0 bytes after 16-byte region [0x75a7cf0e0010,0x75a7cf0e0020)
allocated by thread T0 here:
    #1 ... in main learn_c_bug_oob.c:6
```

Báo cáo nói đúng: ghi 4 byte ở dòng 10, ngay sau vùng 16 byte xin ở dòng 6.

### 2. Dùng sau khi trả (use-after-free)

@include learn_c_bug_uaf.c

- **Thường**: GCC cảnh báo `pointer 'lives' used after 'free' [-Wuse-after-free]`, chương trình vẫn chạy và in một số rác (lần thử của mình là `lives=-73919904`; số này khác nhau giữa các lần).
- **Sanitizer**: `heap-use-after-free`, kèm ba chỗ: nơi đọc (dòng 11), nơi `free` (dòng 10), nơi `malloc` (dòng 6).

### 3. Con trỏ treo tới biến cục bộ

@include learn_c_bug_dangling.c

`label` nằm trên stack của `remember_label` và chết khi hàm kết thúc; `saved` vẫn giữ địa chỉ cũ.

- **Thường** (Windows): GCC cảnh báo `storing the address of local variable 'label' in 'saved' [-Wdangling-pointer=]`, và chương trình in ra **một dòng trống** thay vì `Level 3`.
- **Cùng chương trình, GCC trong WSL, không sanitizer**: in `Level 3` và thoát với mã 0. **Chương trình "chạy đúng" trong khi vẫn sai.** Đây là điều nguy hiểm nhất: nó hỏng ở máy khác, hoặc khi bạn thêm một dòng code.
- **Sanitizer** (cần `ASAN_OPTIONS=detect_stack_use_after_return=1` trên bản GCC này):

```
ERROR: AddressSanitizer: stack-use-after-return on address 0x74ad99ad0020 ...
READ of size 8 ...
    #1 ... in main learn_c_bug_dangling.c:14
Address 0x74ad99ad0020 is located in stack of thread T0 at offset 32 in frame
    #0 ... in remember_label learn_c_bug_dangling.c:6
  This frame has 1 object(s):
    [32, 64) 'label' (line 7) <== Memory access at offset 32 is inside this variable
```

Nếu viết trực tiếp `return label;` trong một hàm trả `const char *`, GCC bắt ngay bằng
`-Wreturn-local-addr` và còn **đổi giá trị trả về thành `NULL`**: chương trình của mình khi đó không in gì cả. Cách
đúng: trả về chuỗi hằng, xin bằng `malloc` (rồi người gọi `free`), hoặc để người gọi đưa buffer vào.

### 4. Rò rỉ (leak)

@include learn_c_bug_leak.c

- **Thường**: in `xong`, không ai biết. Rò rỉ chỉ lộ ra khi chương trình chạy lâu: game mất RAM dần, rồi chậm hoặc sập sau vài giờ.
- **Sanitizer** (LeakSanitizer, đi kèm ASan, báo lúc thoát):

```
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 48 byte(s) in 3 object(s) allocated from:
    #1 ... in spawn_particle learn_c_bug_leak.c:6
SUMMARY: AddressSanitizer: 48 byte(s) leaked in 3 allocation(s).
```

Ba lần gọi, mỗi lần 16 byte (`4 * sizeof(float)`), tổng 48.

### 5. Biến chưa gán giá trị

@include learn_c_bug_uninit.c

Biến cục bộ trong C **không tự bằng 0**: nó chứa rác còn lại trong bộ nhớ. Kết quả thật:

| Cách chạy | Kết quả |
|---|---|
| Windows, `-O0` | `bonus=32758` (rác) |
| Windows, `-O1` và `-O2` | `bonus=100`: một kết quả "đẹp", mà lẽ ra không thể có (`argc` là 1) |
| WSL, `-fsanitize=address,undefined` | `bonus=32765` (rác), không báo lỗi gì |

Kết quả `100` có thể giải thích bằng lý thuyết: dùng biến chưa gán là hành vi không xác định nên trình tối ưu được phép coi
nhánh gán như luôn xảy ra (mình không xem assembly để xác nhận). Kết quả đổi theo mức tối ưu là đủ để thấy chương trình
không còn đáng tin. GCC 15.2 không cảnh báo trong bất kỳ trường hợp nào ở trên, và ASan/UBSan không phát hiện lỗi này. Cách phòng
duy nhất chắc chắn: **luôn gán giá trị lúc khai báo** (`int bonus = 0;`). (Có công cụ khác cho lỗi này, như
MemorySanitizer của Clang hay Valgrind; mình chưa thử chúng ở đây.)

## Bắt lỗi bằng sanitizer

AddressSanitizer (ASan) chèn kiểm tra vào chương trình lúc biên dịch và báo lỗi bộ nhớ ngay tại dòng gây ra;
chương trình chạy chậm hơn và tốn thêm RAM. UBSan bắt một số hành vi không xác định khác (tràn số có dấu, chia cho 0...). Dùng khi
gỡ lỗi, không dùng cho bản phát hành:

```
gcc -std=c17 -Wall -Wextra -g -fsanitize=address,undefined chuongtrinh.c -o chuongtrinh
```

`-g` để báo cáo có tên file và số dòng. **Không phải máy nào cũng có:** GCC của w64devkit (Windows) mà mình dùng
**không** có ASan (liên kết lỗi `cannot find -lasan`). Mọi báo cáo ở trên do GCC trong WSL Ubuntu tạo ra. Trên Linux,
GCC và Clang có sẵn; trên Windows thì dùng WSL, hoặc MSVC (có `/fsanitize=address`, mình chưa thử).

## Trong njin

Mọi quy tắc trên hiện ra trong thiết kế API của njin:

- **Đường dẫn là `const char *`**, chỉ đọc: `texture_load(context &ctx, const char *path)`, `shader_load(ctx, const char *vspath, const char *fspath)`. `const` nghĩa là hàm chỉ đọc chuỗi, không sửa nó.
- **Struct đầy giá trị mặc định**: `platformer_body`, `topdown_body`, `collider`... khai báo mặc định ngay trong struct; game chỉ chỉnh vài trường.
- **Handle thay cho con trỏ.** `njin` không trả con trỏ tới tài nguyên mà trả handle: một struct chỉ chứa số `id`.

```cpp
struct shader_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};
```

Ghi chú trong `src/engine/runtime/njin_audio_impl.h` giải thích vì sao:

```
// handle id 0 is "invalid", id N maps to slots[N - 1], and slots are never
// reused, so a stale handle can never alias a newer resource.
```

Con trỏ tới tài nguyên có thể treo (đã giải phóng mà bạn vẫn giữ) hoặc trỏ sai chỗ (mảng bên trong dời chỗ khi
mọc thêm). Handle thì không: tra lại mỗi lần dùng, và handle cũ trả về "không có gì" thay vì rác. Đây là mẫu đó, bằng C:

@include learn_c_handle.c

```
a.id=1 b.id=2
sau khi huy a: sprite_get(a) la NULL
c.id=3, a van la NULL
b o (30, 40)
handle 0: NULL, khong sap
```

Chú ý bốn điều, đều theo nguyên tắc của njin: `id == 0` không hợp lệ và `sprite_get` từ chối nó; handle đã hủy bị bỏ qua nên hủy
hai lần vô hại; khi tạo thất bại (hết chỗ, như `shader_load` gặp file thiếu) thì trả handle 0 chứ không sập; và `c` **không chiếm
lại** chỗ của `a` nên handle `a` cũ không bao giờ trỏ nhầm sang sprite mới.

## Tự kiểm tra

1. `int *p = &x; *p = 7;` làm gì với `x`?
2. Vì sao `sizeof` trên tham số mảng của một hàm cho kết quả sai, và cách khắc phục?
3. Khác nhau giữa `char name[16] = "Sprout"` và `const char *title = "Sprout"`?
4. `snprintf` trả về `23` với buffer 16 byte. Điều đó nghĩa là gì?
5. Chương trình chạy đúng, không sập, không cảnh báo. Có nghĩa là nó không có lỗi bộ nhớ không?

## Bài tập

1. Viết hàm `void swap_int(int *a, int *b)` hoán đổi hai số, và gọi nó trong `main`.
2. Viết `copy_name(char *dst, size_t dst_size, const char *src)` không bao giờ ghi quá `dst_size` byte và luôn kết thúc bằng `'\0'`. Thử với buffer 6 và 16 byte, nguồn `"Sprout"`.
3. Viết một danh sách số nguyên tự lớn lên: `struct int_list { int *items; int count; int capacity; }` với `list_push` (nhân đôi `capacity` khi đầy) và `list_free`. Đẩy 10 số bình phương vào và in ra.

## Đáp án

**Tự kiểm tra.**

1. Gán `7` cho `x`: `*p` là chính `x`.
2. Mảng truyền vào hàm suy biến thành con trỏ tới phần tử đầu, nên `sizeof` cho cỡ con trỏ. Truyền thêm độ dài mảng như một tham số riêng.
3. Cái đầu là một mảng 16 byte của bạn: sửa được (`name[0] = 'X'`). Cái sau là con trỏ tới chuỗi hằng của chương trình: sửa là hành vi không xác định.
4. Cần 23 ký tự (chưa tính `'\0'`), buffer chỉ có 16 byte, nên chuỗi đã bị **cắt**. Phải kiểm tra `>= sizeof(buf)`.
5. Không. Cả năm lỗi ở trên đều có thể "chạy đúng" (ghi tràn heap, con trỏ treo in ra `Level 3`, rò rỉ...). Chỉ sanitizer, cảnh báo và đọc code mới tìm được chúng.

**Bài 1**

```c
#include <stdio.h>

static void swap_int(int *a, int *b) {
  int tmp = *a;
  *a = *b;
  *b = tmp;
}

int main(void) {
  int x = 1, y = 2;
  swap_int(&x, &y);
  printf("x=%d y=%d\n", x, y);
  return 0;
}
```

```
x=2 y=1
```

**Bài 2**

```c
#include <stdio.h>
#include <string.h>

static void copy_name(char *dst, size_t dst_size, const char *src) {
  snprintf(dst, dst_size, "%s", src);
}

int main(void) {
  char small[6];
  copy_name(small, sizeof(small), "Sprout");
  printf("small=\"%s\" (strlen=%zu)\n", small, strlen(small));
  char big[16];
  copy_name(big, sizeof(big), "Sprout");
  printf("big=\"%s\"\n", big);
  return 0;
}
```

```
small="Sprou" (strlen=5)
big="Sprout"
```

Buffer 6 byte chứa được 5 ký tự và `'\0'`, nên "Sprout" bị cắt còn "Sprou": `snprintf` cắt an toàn thay vì ghi tràn.

**Bài 3**

```c
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  int *items;
  int count;
  int capacity;
} int_list;

static int list_push(int_list *list, int value) {
  if (list->count == list->capacity) {
    int new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
    int *bigger = realloc(list->items, (size_t)new_capacity * sizeof(int));
    if (bigger == NULL)
      return 0; // danh sách cũ vẫn nguyên vẹn
    list->items = bigger;
    list->capacity = new_capacity;
  }
  list->items[list->count++] = value;
  return 1;
}

static void list_free(int_list *list) {
  free(list->items);
  *list = (int_list){0};
}

int main(void) {
  int_list list = {0};
  for (int i = 1; i <= 10; i++)
    list_push(&list, i * i);
  for (int i = 0; i < list.count; i++)
    printf("%d ", list.items[i]);
  printf("\n(count=%d, capacity=%d)\n", list.count, list.capacity);
  list_free(&list);
  return 0;
}
```

```
1 4 9 16 25 36 49 64 81 100 
(count=10, capacity=16)
```

Ba đáp án này cũng đã chạy sạch dưới `-fsanitize=address,undefined`.

## Bước tiếp theo

@ref learn_c_project : chia chương trình ra nhiều file, và hiểu vì sao có lỗi "undefined reference".
