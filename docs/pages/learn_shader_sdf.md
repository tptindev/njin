# Bài 11: SDF: vẽ hình bằng khoảng cách {#learn_shader_sdf}

**Bài này dạy gì:** SDF (signed distance function, hàm khoảng cách có dấu) là cách shader **vẽ hình bằng công thức**
thay vì bằng ảnh: hình tròn, hộp, hộp bo góc, đoạn thẳng, rồi ghép, khoét, làm viền, làm bóng, làm quầng sáng. Cuối
bài là thanh máu và vòng hồi chiêu vẽ hoàn toàn bằng shader, không cần một file ảnh nào.

**Cần biết trước:** @ref learn_shader_start : GLSL cơ bản, uniform, và sân chơi (`learn_shader_playground.c`). Mọi ví
dụ ở đây chạy trong sân chơi đó, chế độ 1.

## Ý tưởng: hỏi "điểm này cách hình bao xa?"

Để vẽ một hình tròn trong shader, câu hỏi đơn giản nhất là "điểm này nằm trong hay ngoài hình?", trả lời đúng hoặc
sai. Câu trả lời đúng/sai cho ra mép **răng cưa**, và không nói gì về những điểm gần mép. SDF hỏi khác đi:

> Từ điểm này đến **mép hình** gần nhất là bao xa, và điểm ở **trong** hay **ngoài**?

Câu trả lời là một con số **có dấu**:

| Giá trị `d` | Nghĩa |
|---|---|
| `d < 0` | điểm nằm **bên trong** hình, `-d` là khoảng cách đến mép |
| `d = 0` | điểm nằm **đúng trên** mép |
| `d > 0` | điểm nằm **bên ngoài** hình, `d` là khoảng cách đến mép |

Bài @ref learn_shader_start đã dùng ý này mà chưa gọi tên: `length(p)` là khoảng cách từ điểm `p` đến gốc toạ độ, và trừ
đi bán kính `r` thì được SDF của hình tròn:

```glsl
float sd_circle(vec2 p, float r) {
  return length(p) - r;
}
```

Một con số như vậy cho ta nhiều hơn "trong hay ngoài": biết `d` là biết **làm mờ mép** (theo `d`), **vẽ viền** (`|d|`
nhỏ), **phát sáng** (`d` dương nhỏ), **đổ bóng** (dời hình đi rồi dùng lại `d`), và **ghép hình** (kết hợp các `d`),
tất cả từ cùng một con số.

## Nhìn thấy trường khoảng cách

Cách hiểu SDF nhanh nhất là **vẽ chính `d` thành màu**. Ví dụ dưới đây vẽ SDF của một hình tròn quanh chuột (chế độ 1;
tâm hình tròn đi theo chuột):

@include learn_shader_sdf_field.fs

@image html learn_shader_sdf_field.png "Cam là bên trong (d < 0), xanh là bên ngoài (d > 0). Vòng trắng là d = 0, chính là mép hình tròn. Các vòng đồng tâm là những chỗ có cùng khoảng cách."

Đọc từng dòng:

- `float d = sd_circle(p - center, 0.2);` : `p - center` dời hình đến `center`. **Dời hình = trừ vị trí khỏi điểm đang xét**,
  nghe ngược nhưng đúng: thay vì dời hình sang phải, ta coi điểm đang xét dịch sang trái.
- `col *= 1.0 - exp(-6.0 * abs(d));` : sát mép (`|d|` nhỏ) thì tối, xa dần thì sáng lên. Hàm `exp(-k * x)` là một cách
  đơn giản để có đường cong giảm nhanh; bạn sẽ dùng lại nó cho quầng sáng.
- `col *= 0.8 + 0.2 * cos(150.0 * d);` : các **đường đồng mức**. `cos` của `d` lặp lại theo khoảng cách, nên mỗi
  vòng là một khoảng cách cố định đến mép. Đây là cách quen thuộc để gỡ lỗi một SDF: nếu các vòng không đều hay
  đứt quãng thì công thức của bạn sai.
- `mix(col, vec3(1.0), 1.0 - smoothstep(0.0, 0.01, abs(d)))` : tô trắng **đúng chỗ `d = 0`**. Đây chính là hình tròn.

@note Khi một SDF trông sai, đừng đoán: hiện `d` thành màu như trên. Bài @ref learn_shader_patterns có mục "Hiện một
số thành màu để gỡ lỗi", cùng một ý.

## Từ khoảng cách thành hình: `fill`

Muốn tô hình thì đổi `d` thành độ phủ từ 0 đến 1. Bài trước dùng `smoothstep(0.28, 0.30, d)` với hai số cố định, và
mép chỉ đẹp ở đúng một kích thước: phóng to thì mép mờ đi, thu nhỏ thì mép răng cưa. Cách chuẩn dùng `fwidth`:

```glsl
float fill(float d) {
  float aa = fwidth(d);                     // d đổi bao nhiêu khi đi sang pixel bên cạnh
  return 1.0 - smoothstep(-aa, aa, d);      // 1 trong hình, 0 ngoài, mờ đúng khoảng một pixel ở mép
}
```

`fwidth(d)` cho biết `d` thay đổi bao nhiêu từ pixel này sang pixel kề, tức "một pixel dài bao nhiêu trong hệ toạ
độ của `d`". Mép mượt đúng một pixel dù hình lớn hay nhỏ, và không phải chỉnh số thủ công.

@note `fwidth` (và các hàm đạo hàm khác như `dFdx`, `dFdy`) so các pixel kề nhau, nên theo đặc tả GLSL kết quả **không
xác định** khi gọi trong nhánh `if` mà các pixel kề nhau đi các hướng khác nhau. Tính `aa` ở đầu hàm, ngoài mọi `if`,
như trên. Mình chưa thử trường hợp lỗi này, nên đây là lời khuyên theo đặc tả chứ không phải kết quả đo.

Từ nay, mọi hình đều vẽ bằng cùng một khuôn: `col = mix(col, màu, fill(d))`, và mỗi hình mới đè lên hình trước.

## Các hình cơ bản

@include learn_shader_sdf_shapes.fs

@image html learn_shader_sdf_shapes.png "Hình tròn, hộp, hộp bo góc, đoạn thẳng có đầu tròn. Mép mọi hình đều mượt."

Mỗi hình là một hàm nhận **điểm `p` (đã dời về gốc)** và trả về `d`:

- **Hình tròn**: `length(p) - r`.
- **Hộp** `sd_box(p, b)`, với `b` là **nửa** kích thước. `abs(p) - b` gấp bốn góc phần tư thành một, nên chỉ cần xét
  một góc. `q` âm ở cả hai trục nghĩa là ở trong hộp (khoảng cách âm là `max(q.x, q.y)`); `q` dương nghĩa là ở
  ngoài, và khoảng cách đến hộp là độ dài phần dương của `q` (`length(max(q, 0.0))`), chính xác cả ở góc.
- **Hộp bo góc**: một mẹo dùng cho **mọi** hình. Cắt hộp nhỏ đi `r` ở mỗi phía rồi **trừ `r` khỏi khoảng cách**:
  `sd_box(p, b - r) - r`. Trừ một số khỏi `d` là **phồng** hình ra đều mọi phía một đoạn `r`, và góc nhọn thành cung
  tròn. Vì vậy hộp bo góc chính là hộp nhỏ được phồng ra.
- **Đoạn thẳng** `sd_segment`: tìm điểm gần nhất **trên đoạn** (`h` kẹp trong 0 đến 1, đó là vị trí chiếu của `p`
  lên đường thẳng), đo khoảng cách đến nó, rồi trừ nửa bề dày. Đầu tròn có được miễn phí.

Vài phép biến đổi dùng cho mọi SDF:

| Muốn | Làm | Ví dụ |
|---|---|---|
| Dời hình | `d = sd_x(p - vị_trí)` | `sd_circle(p - vec2(0.3, 0.0), 0.1)` |
| Phồng hình ra `r` | `d - r` | hộp bo góc |
| Chỉ lấy **viền** dày `w` | `abs(d) - w` | vòng tròn rỗng ở bài tập và ở vòng hồi chiêu |
| Phóng hình to `s` lần | chia `p` cho `s`, rồi nhân `d` với `s` | nhân lại `d` để nó vẫn là khoảng cách thật (theo định nghĩa, chưa đo) |

## Ghép hình bằng cách ghép khoảng cách

Đây là chỗ SDF mạnh nhất: ghép hai hình chỉ là ghép hai con số.

| Phép | Công thức | Nghĩa |
|---|---|---|
| Hợp | `min(a, b)` | trong `a` **hoặc** trong `b` |
| Giao | `max(a, b)` | trong `a` **và** trong `b` |
| Trừ | `max(a, -b)` | trong `a`, **trừ** phần trong `b` |
| Hợp mềm | `smin(a, b, k)` | như hợp, nhưng hai hình **dính** vào nhau như giọt nước |

@include learn_shader_sdf_combine.fs

@image html learn_shader_sdf_combine.png "Ba thời điểm khác nhau (mỗi hàng một lần): hợp (vàng), giao (xanh dương), trừ (xanh lục), hợp mềm (hồng). Hai vòng tròn chạy lại gần rồi xa nhau."

Hợp mềm là điều khó nhất nếu vẽ bằng ảnh: lại gần thì hai vòng hồng dính và **thắt eo** ở giữa (hàng thứ hai trong ảnh), đến gần
hơn nữa thì thành một khối. `k` là độ "dính": `k` lớn thì dính từ xa.

@note Về toán, `min` của hai SDF vẫn là SDF chính xác **ở bên ngoài**, còn `max` (giao, trừ) và hợp mềm cho ra một
số **đúng dấu** (trong hay ngoài luôn đúng) nhưng không còn đúng từng đơn vị khoảng cách. Vẽ hình thì đủ; nếu dùng `d` để
làm quầng sáng hay bóng rất mềm thì hình quầng sáng có thể hơi méo ở chỗ hai hình gặp nhau.

## Dùng trong game: giao diện không cần ảnh

Thanh máu, viền nút, vòng hồi chiêu, tất cả là hình đơn giản, và SDF vẽ chúng **ở mọi độ phân giải, mọi kích cỡ, không
cần ảnh**. Ví dụ này vẽ thanh máu và vòng hồi chiêu, đầy 70% (`mouse.x` là lượng đầy; thử di chuột):

@include learn_shader_sdf_ui.fs

@image html learn_shader_sdf_ui.png "Thanh máu (đầy 70%, có quầng sáng đỏ nhẹ) và vòng hồi chiêu đi theo chiều kim đồng hồ từ đỉnh."

**Thanh máu.** `frame` là hộp bo góc ngoài, `inner` là hộp nhỏ hơn bên trong. Phần máu đầy là **giao** của `inner`
với "bên trái đường cắt": `max(inner, cut)`, trong đó `cut = q.x - vị_trí_đường_cắt` âm ở bên trái đường cắt. Đổi lượng
máu chỉ là dời đường cắt, hình vẫn bo góc đúng.

**Vòng hồi chiêu.** `abs(length(c) - 0.17) - 0.028` là một **vòng tròn dày** (viền của hình tròn bán kính 0,17). Muốn
tô một phần vòng theo góc thì cần góc của điểm: `atan(c.x, -c.y)` cho 0 ở đỉnh và tăng theo chiều kim đồng hồ, chia
cho `2π` và lấy phần lẻ (`fract`) thì được 0 đến 1. Điểm nào có góc nhỏ hơn `amount` thì đã hồi xong.

### Một lỗi tôi gặp khi viết ví dụ này

Bản đầu của thanh máu cộng quầng sáng bằng
`col += màu * 0.25 * exp(-40.0 * max(frame, 0.0))`. Đo pixel thật thì phần máu đỏ ra `(255, 76, 92)` chứ không phải
`(230, 64, 77)` như tính, và phần lòng thanh (lẽ ra tối) cũng sáng lên `(70, 25, 36)`. Nguyên nhân: **bên trong** hình
`frame` âm, `max(frame, 0.0)` bằng 0, và `exp(0)` bằng **1**, nên quầng sáng được cộng đủ mạnh vào cả bên trong.
Quầng sáng chỉ nên có ở bên ngoài, nên bản đúng nhân thêm `(1.0 - fill(frame))`. Bài học chung: **hàm của `d` cho quầng
sáng và bóng phải được kiểm tra cả ở phía `d` âm**, không chỉ phía dương mình đang nhìn.

## Trong njin

njin dùng đúng loại shader này, và ví dụ ở trên chạy nguyên xi:

@include learn_shader_sdf_njin.cpp

@image html learn_shader_sdf_njin.png "Cùng shader thanh máu, chạy trong njin: một ảnh kéo giãn thành khung 400 x 225, shader tự tạo hình lên đó."

Các điểm cần nhớ (đã chạy thử):

- Shader **không dùng màu của ảnh**, chỉ dùng toạ độ `fragTexCoord` chạy 0 đến 1 trên ảnh đang vẽ. Nên bất kỳ ảnh nào
  kéo giãn ra đúng kích cỡ khung cần là đủ; đổi kích cỡ khung là đổi `scale` của njin::texture_draw_desc.
- `resolution` là kích thước của **khung vẽ**, không phải của cửa sổ, vì nó dùng để giữ hình tròn không bị dẹt. Đặt
  bằng njin::shader_set_vec2() **trước** njin::shader_begin().
- Vẽ trong `phase_post_render` cho giao diện (không đi qua camera), hoặc `phase_render` cho vật trong thế giới.
- Thấy shader hậu kỳ có sẵn của njin dùng đúng ý tưởng này: vignette của njin::post_fx đo khoảng cách từ tâm màn hình
  (`length(...)` trong `src/engine/runtime/modules/post_fx.cpp`) rồi `smoothstep`, tức là SDF của một điểm.
- njin **chưa có** hàm vẽ SDF dựng sẵn, cũng chưa hỗ trợ font SDF (đã tìm trong mã nguồn): với thanh máu, viền nút, chữ,
  ảnh vẫn là cách thường dùng; SDF hợp nhất cho hình đơn giản cần nhìn sắc ở mọi kích cỡ và cần hiệu ứng theo khoảng
  cách (viền, sáng, bóng).

## Khi nào nên dùng, khi nào thôi

**Nên**: hình hình học đơn giản (tròn, hộp, vòng, đoạn thẳng), giao diện kiểu thanh và vòng, sóng xung kích, quầng
sáng, vùng tầm đánh, bất cứ thứ gì cần **mép mượt ở mọi kích cỡ** hoặc **tham số hoá bằng số** (lượng máu, tiến độ).

**Không nên**: hình phức tạp như nhân vật, cây, chữ tay vẽ. Chúng có sẵn trong ảnh, và vẽ bằng công thức chỉ tốn công. Giá
mỗi pixel cũng tỉ lệ với **số hình** trong shader (mỗi hình là một lần tính `d` cho mọi pixel của khung), nên đừng nhét
hàng chục hình vào một shader phủ cả màn hình. Nếu chỉ cần vài hình, giữ khung vẽ nhỏ (như 400 x 225 ở trên) thì chi phí là
số pixel của khung, không phải của cả màn hình.

## Tự kiểm tra

1. `d = -0.05` ở một điểm nghĩa là gì? Còn `d = 0.3`?
2. Vì sao hộp bo góc là `sd_box(p, b - r) - r` chứ không phải `sd_box(p, b) - r`?
3. Vì sao dùng `fwidth(d)` tốt hơn `smoothstep(0.28, 0.30, d)` với hai số cố định?
4. Muốn vẽ **viền** của một hình (chỉ đường bao, ruột rỗng) thì dùng công thức nào?
5. Hợp mềm khác hợp thường ở điểm nào khi hai hình ở gần nhau?

## Bài tập

1. **Sóng xung kích**: một vòng tròn mỏng nở ra từ giữa rồi mờ dần, lặp lại (dùng `time`).
2. **Dấu cộng**: vẽ một dấu cộng ở giữa bằng phép ghép hình.
3. **Bóng đổ**: một hộp bo góc sáng trên nền sáng, có bóng mềm dời xuống dưới bên phải.

## Đáp án

**Tự kiểm tra**

1. `-0.05`: điểm ở **trong** hình, cách mép `0,05`. `0.3`: điểm ở **ngoài** hình, cách mép `0,3`.
2. `sd_box(p, b)` đã là hộp đầy đủ kích thước; trừ `r` phồng nó ra thành hộp **to hơn** `r` mỗi phía. Muốn giữ đúng kích
   thước `b`, phải thu hộp lại `r` trước (`b - r`), rồi phồng ra `r`: hình cuối vẫn vừa khung `b`, chỉ góc tròn.
3. Hai số cố định là **khoảng cách trong đơn vị của hình**, nên mép chỉ mượt đúng ở một kích cỡ: phóng to thì mép bị mờ
   rộng ra, thu nhỏ thì mép răng cưa. `fwidth(d)` đo "một pixel dài bao nhiêu" nên tự thích ứng.
4. `abs(d) - w`: gần bằng 0 chỉ ở sát mép, và `w` là nửa bề dày viền.
5. Hợp thường `min(a, b)` giữ hai hình nguyên vẹn và chỉ chạm nhau ở một góc nhọn. Hợp mềm làm khoảng giữa hai hình
   **đầy lên** thành cổ nối tròn, nên chúng dính như giọt nước và tách ra rồi cũng thắt eo dần.

**Bài tập 1: sóng xung kích**

@include learn_shader_sdf_answer_ring.fs

@image html learn_shader_sdf_ring.png "Hai thời điểm: vòng nở ra và mờ đi."

Bán kính tăng theo `fract(time * 0.4)`, `abs(length(p) - radius) - 0.015` là vòng dày `0,03`, và độ đậm nhân với
`1 - life`. Mình đo trên ảnh chụp: vòng sáng đạt cực đại ở bán kính 0,177 rồi 0,335 (hai thời điểm), tròn đều ở cả bốn hướng.

**Bài tập 2: dấu cộng**

@include learn_shader_sdf_answer_cross.fs

@image html learn_shader_sdf_cross.png "Hợp của hộp nằm ngang và hộp đứng."

Kiểm tra trên ảnh: tâm và bốn cánh có màu, bốn góc trống.

**Bài tập 3: bóng đổ**

@include learn_shader_sdf_answer_shadow.fs

@image html learn_shader_sdf_shadow.png "Bóng nằm dưới bên phải, mờ dần ra ngoài."

Bóng là **cùng hình đã dời**, nhưng thay vì mép một pixel thì dùng `smoothstep(-0.03, 0.06, d)`: dải mờ rộng nhiều lần. Đây
chính là lợi ích của biết `d`: mép mềm bao nhiêu tuỳ ý. Đo trên ảnh: cạnh dưới phải của hộp tối hơn nền xa
`(122, 133, 156)` so với `(140, 153, 178)`, và phía đối diện (trên trái) sáng bằng nền.

Mọi ví dụ và đáp án trong bài này đã chạy trong sân chơi, ảnh chụp được đối chiếu với giá trị tính tay tại những điểm
cố định (màu tâm mỗi hình, góc bị cắt của hộp bo góc, mép mượt, phần đầy của thanh máu và vòng). Ví dụ chạy trong njin là
`learn_shader_sdf_njin.cpp`, dùng chính shader `learn_shader_sdf_ui.fs`. Chỉ thử trên một card đồ họa (Intel Iris Xe).

## Bước tiếp theo

@ref learn_shader_patterns : các mẫu shader hay gặp (hậu kỳ, nháy trắng, viền, tan biến, pixel art) và cách chúng ứng
với njin, trong đó có `sprite_dissolve` đã dựng sẵn.
