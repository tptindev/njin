# Bài 13: Các pattern của game {#learn_game_patterns}

**Bài này dạy gì:** sáu ý tưởng gần như game nào cũng dùng, viết bằng C++ thuần để bạn thấy chúng thật ra chỉ là vài
chục dòng: vòng lặp với bước vật lý cố định, ECS, máy trạng thái, event, hẹn giờ, và action thay cho phím.
**Cần biết trước:** đọc và chạy được một chương trình C++ có `struct`, `std::vector` và lambda (xem @ref learn_cpp_from_c
và @ref learn_cpp_modern).

Mỗi mục có một chương trình ngắn chạy thật, rồi phần **Trong njin** chỉ ra ý tưởng đó nằm ở đâu trong engine. Khi
bắt đầu njin, bạn sẽ gặp lại từng thứ ở đây.

## 1. Vòng lặp và bước vật lý cố định

Game là một vòng lặp: đọc input, cập nhật thế giới, vẽ, rồi lặp lại. Câu hỏi là mỗi lần cập nhật thì thế giới tiến
bao nhiêu thời gian. Cách đơn giản nhất là bằng thời gian của frame vừa rồi (`dt`). Cách này có một lỗi khó thấy:
**kết quả phụ thuộc FPS**. Chương trình dưới đây cho một nhân vật nhảy lên, ở bốn mức FPS, bằng hai cách.

@include learn_gp_loop.cpp

Chạy (`g++ -std=c++20 -Wall -Wextra -Wpedantic`):

```
   FPS  bước = frame   bước cố định
    20  37.50             42.50
    30  40.00             42.50
    60  42.50             42.50
   144  43.96             42.50
```

Cùng một cú nhảy, cột trái cao từ 37,5 đến 44 pixel tùy máy (ở 240 FPS là 44,38). Nguyên nhân: mỗi bước là một xấp xỉ, và bước
càng dài thì sai càng nhiều. Cột phải luôn 42,5: vật lý chỉ chạy theo **bước cố định** (`1/60` giây), còn số bước
mỗi frame thì đổi. Vòng `while` gom thời gian trong `accumulator` rồi chạy đủ số bước. `max_steps` chặn một
frame quá dài (ví dụ dừng ở breakpoint) để game không phải chạy dồn hàng trăm bước.

@note Frame nhanh hơn 60 FPS thì vài frame liền không có bước vật lý nào, nên vị trí trên màn hình đứng yên rồi
giật. Cách chữa là **nội suy**: vẽ ở giữa vị trí trước và sau, theo phần nhịp còn dư `alpha = accumulator / fixed_dt`.
Đây là bài tập 2 ở cuối.

**Trong njin:**
- `njin::phase_fixed_update` chạy đúng vòng `while` này, mặc định 60 lần mỗi giây (`config::fixed_hz`), tối đa 8 nhịp
  mỗi frame (`src/engine/runtime/njin.cpp`). Trong phase đó, `njin::delta()` luôn bằng một nhịp cố định.
- `njin::fixed_alpha()` chính là `alpha` ở trên. Xem @ref game_loop và @ref time.

## 2. Entity, component, system

Cách nghĩ theo kế thừa ("Quái là một Nhân vật là một Vật thể") gãy nhanh khi game lớn: một cái bẫy vừa có máu vừa
không di chuyển thì nằm ở đâu? ECS đảo lại: một **entity** chỉ là một con số định danh, **component** là dữ liệu
gắn vào nó, **system** là một hàm duyệt những entity có đúng các component nó cần. Muốn một vật có máu, bạn
gắn `health` cho nó, không viết lớp mới.

@include learn_gp_ecs.cpp

Chạy:

```
entity 1: (5.0, 0.0) có máu
entity 2: (4.0, 4.0)
entity 3: (5.5, 0.0) có máu [quái]
```

Hòn đá (entity 2) có vị trí nhưng không có `velocity`, nên `move_system` bỏ qua nó. Không ai phải viết `if (là_đá)`.
`enemy_tag` không chứa dữ liệu nào: nó chỉ để **đánh dấu**.

@note Chương trình này giữ kho component trong một biến `static` cho ngắn, nên hai `registry` sẽ dùng chung kho. Một
ECS thật giữ kho bên trong registry, và lưu component liền nhau trong bộ nhớ để duyệt nhanh.

**Trong njin:** njin dùng thư viện **EnTT**, `njin::world(ctx)` trả về `entt::registry`. Cách dùng y hệt:
`registry.create()`, `registry.emplace<T>(e, ...)`, `registry.view<A, B>().each(...)`. Xem @ref ecs.

## 3. Máy trạng thái

Nhân vật đang đứng yên, chạy hay nhảy? Dùng một `enum class` và **một chỗ duy nhất** đổi trạng thái, để việc
"khi vào" và "khi ra" khỏi trạng thái có nơi để đặt (phát tiếng, đổi animation, đặt lại bộ đếm).

@include learn_gp_states.cpp

Chạy:

```
  idle -> run (sau 0.25 giây)
  [0.25] bắt đầu chạy
  [0.75] hẹn giờ trong hẹn giờ: 0.25 giây nữa nhảy
  run -> jump (sau 0.75 giây)
  [1.00] nhảy!
  jump -> run (sau 0.25 giây)
```

Đừng rải `state = state::run;` khắp code: mọi lần đổi đi qua `change()`, nên log (dòng có mũi tên) và mọi hiệu ứng
đi kèm đều nằm ở một chỗ.

**Trong njin:** cả game cũng là một máy trạng thái: menu, màn chơi, game over là các **scene**
(`njin::scene_register()`, `njin::scene_set()`, xem @ref scenes). Animation chuyển idle, run, jump là một
đồ thị trạng thái (`njin::anim_graph_create()`, xem @ref animation).

## 4. Hẹn giờ

"Sau 2 giây thì hồi sinh", "cứ mỗi 3 giây sinh một con quái". Đừng tự giữ một biến đếm cho mỗi việc. Một danh sách
hẹn giờ (nửa dưới của chương trình trên) lo hết. Có hai điểm dễ sai và chương trình đã tránh:

- hàm được gọi có thể **tạo hẹn giờ mới** (dòng "hẹn giờ trong hẹn giờ"), nên phải gom hàm đã đến hạn ra một danh sách
  riêng, rồi mới gọi;
- đếm giờ bằng số thực cộng dồn thì lệch (`0.05` cộng nhiều lần không ra đúng `0.25`), nên ví dụ dùng `dt = 1/16`.

**Trong njin:** `njin::timer_after()` và `njin::timer_every()` làm đúng việc này. Hàm chạy trong `phase_update`, và chạy
được cả những việc thêm hay hủy entity. Xem @ref screen_timers.

## 5. Event

Khi quái chết, cần cộng điểm, phát tiếng, thả vật phẩm. Nếu code của quái gọi từng thứ đó, quái phải biết tất cả.
Với event, quái chỉ **báo** "tôi chết rồi"; ai quan tâm thì đăng ký nghe.

@include learn_gp_events.cpp

Chạy:

```
trước flush: điểm = 0
  âm thanh: quái 1 chết
  âm thanh: quái 2 chết
sau flush:   điểm = 350
bấm W:       jump = có
bấm mũi tên trái: jump = không
sau khi đổi phím: jump = có
```

Điểm là `0` trước `flush()` và `350` sau đó: event được **xếp hàng** rồi phát ở một chỗ cố định của frame. Nhờ vậy
gọi `enqueue` giữa lúc đang duyệt danh sách quái không làm hỏng danh sách đó.

**Trong njin:** `njin::events()` là một `entt::dispatcher`. `enqueue` xếp hàng và event được phát ngay sau
`phase_post_update`; `trigger` phát ngay lập tức. Xem @ref ecs.

## 6. Action thay cho phím

Nửa sau của chương trình trên. Code game hỏi `pressed("jump")`, không hỏi `phím Space`. Bảng nối "jump" với các phím
nằm ở một chỗ: thêm phím W, thêm tay cầm, hay cho người chơi đổi phím đều chỉ là sửa bảng đó, và không dòng logic
nào đổi.

**Trong njin:** `njin::action_define(ctx, "jump", {njin::key_space, njin::key_w, njin::pad_face_down})`, rồi
`njin::action_pressed()`. Xem @ref input.

## Một mẫu nữa: handle thay cho con trỏ

Ở nhiều chỗ của njin bạn sẽ thấy một `struct` chỉ chứa một con số `id`, trong đó `id == 0` nghĩa là "không hợp lệ"
(`njin::timer_handle`, `njin::texture_handle`, `njin::sound_handle`...). Engine giữ tài nguyên thật ở trong, còn game
chỉ cầm số. Không có con trỏ treo, và hủy tài nguyên rồi thì số cũ chỉ thành "không hợp lệ" chứ không làm chương trình
sập. Bài @ref learn_cpp_types dựng một kho như vậy.

## Tự kiểm tra

1. Vì sao vật lý dùng bước cố định mà vẽ thì không?
2. `max_steps` trong vòng lặp cố định để làm gì? Chuyện gì xảy ra nếu bỏ nó, và game bị đứng nửa giây?
3. Trong ECS, muốn một vật vừa di chuyển vừa có máu thì bạn làm gì? Còn nếu dùng kế thừa?
4. Vì sao `enqueue` an toàn hơn gọi thẳng người nghe khi đang duyệt danh sách quái?
5. Vì sao đếm giờ bằng cách cộng `0.05f` nhiều lần dễ lệch?

## Bài tập

1. Trong `learn_gp_loop.cpp`, thêm FPS `240` vào danh sách. Cột "bước cố định" có đổi không? Vì sao?
2. Thêm **nội suy** vào vòng lặp cố định: lưu `prev_y` trước mỗi bước, và mỗi frame tính vị trí vẽ
   `prev_y + (y - prev_y) * alpha`. Đếm xem ở 144 FPS, `y` đổi ở bao nhiêu frame và vị trí vẽ đổi ở bao nhiêu frame.
3. Thêm vào `registry` của mini ECS một hàm `each<A>(f)` duyệt entity có component `A`. Rồi dùng nó để đếm số entity có
   `health` và tổng máu.

## Đáp án

**Tự kiểm tra**

1. Vật lý cần cho ra cùng kết quả ở mọi FPS và không để vật xuyên tường khi FPS tụt, nên phải đi theo bước cố định.
   Vẽ chỉ cần kịp và mượt: vẽ mỗi frame, kèm nội suy nếu cần.
2. Nó chặn số bước vật lý tối đa trong một frame. Không có nó, sau khi game đứng nửa giây (30 bước dồn lại), frame kế
   tiếp phải chạy cả 30 bước, mất nhiều thời gian hơn, làm frame sau nữa lại dồn thêm: game càng lúc càng chậm.
3. ECS: gắn cả `velocity` và `health` cho entity. Kế thừa: phải có một lớp thừa kế cả hai (đa kế thừa) hoặc một lớp mới cho
   riêng tổ hợp đó, và số tổ hợp tăng rất nhanh.
4. `enqueue` chỉ ghi vào hàng đợi, nên danh sách quái và danh sách người nghe không đổi lúc đang duyệt. Việc phát
   event diễn ra sau, ở một chỗ cố định của frame.
5. `0.05` không biểu diễn chính xác được trong số thực nhị phân, nên sai số nhỏ cộng dồn qua nhiều lần. Dùng giá trị
   chia chính xác (như `1/16`), đếm bằng số nguyên, hoặc so sánh có dung sai.

**Bài tập**

1. Cột "bước cố định" không đổi, vẫn `42.50`, còn cột "bước = frame" thành `44.38`. Số bước vật lý chỉ phụ thuộc thời gian trôi qua (`floor(t / (1/60))`), không phụ thuộc
   cách chia thời gian đó thành các frame. FPS chỉ đổi số bước chạy trong mỗi frame.
2. Chương trình đầy đủ:

   @include learn_gp_alpha.cpp

   In ra `frame: 72, y đổi ở 28 frame, draw_y đổi ở 69 frame, draw_y luôn nằm giữa prev và y: đúng`. Vị trí vật lý `y`
   chỉ đổi ở 28 trong 72 frame (các frame còn lại không có bước nào), nên nhìn giật. Vị trí vẽ đổi ở hầu như mọi frame
   và luôn nằm giữa hai vị trí vật lý. Đó là cái giá của nội suy: vị trí vẽ trễ một bước.
3. Thêm vào `struct registry`:

   ```cpp
   template <class A, class F> void each(F f) {
     for (const entity e : alive)
       if (has<A>(e))
         f(e, get<A>(e));
   }
   ```

   Dùng: `reg.each<health>([&](entity, health &h) { count++; total += h.hp; });`. Với ba entity (hai có máu 5 và 2, một không
   có) in ra `2 entity có máu, tổng 7 máu`.

Mọi đáp án trên đã biên dịch và chạy với `g++ -std=c++20 -Wall -Wextra -Wpedantic`.

## Bước tiếp theo

Bạn đã đi hết phần kiến thức nền. Từ đây:

- @ref setup : cài môi trường
- @ref getting_started : chương trình njin đầu tiên
- @ref first_jump và @ref first_walk : một nhân vật chạy được trong 50 dòng
