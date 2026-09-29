# Kiến thức nền: học trước khi làm game {#learn}

Muốn làm game với njin thì cần đọc được C++, biết CMake dựng ra chương trình, và hiểu shader là gì. Nhóm 13 bài này dạy
đúng những phần đó, **từ con số không**, theo một mạch duy nhất. Không cần chọn "mức" nào: bạn bắt đầu ở bài đầu, và
bài nào đã biết thì bỏ qua.

Mọi thứ trong đây đúc kết từ chính njin: các ví dụ lấy hình dạng từ code của engine (handle, `context &`, CMake của
repo, shader hậu kỳ), cộng những pattern game nào cũng gặp. **Không bài nào cần njin**: chỉ cần trình biên dịch, và
CMake ở phần cuối. Cài chúng theo @ref setup.

## Cách học

- Mỗi bài mở đầu bằng "Bài này dạy gì" và "Cần biết trước", và kết thúc bằng **Tự kiểm tra** (câu hỏi), **Bài tập** và
  **Đáp án**. Làm bài tập trước khi xem đáp án.
- **Mọi ví dụ đã được biên dịch và chạy thật**, kết quả in trong bài là kết quả thật. Trang nào chưa thử được thứ gì
  (một trình biên dịch, một hệ điều hành) thì nói rõ.
- Gõ lại và sửa các ví dụ thay vì chỉ đọc. Lỗi bạn tự gây ra và tự sửa được dạy nhiều hơn ví dụ chạy đúng.

## 13 bài

**C: nền tảng của mọi thứ** (raylib viết bằng C, và API của njin vẫn giữ dáng C ở `const char *` và `printf`)

| Bài | Nội dung |
|---|---|
| @subpage learn_c_start | Viết, biên dịch, chạy một chương trình C: kiểu dữ liệu, `printf`, vòng lặp, hàm |
| @subpage learn_c_memory | Con trỏ, mảng, `struct`, chuỗi C, stack và heap, năm lỗi bộ nhớ kinh điển và cách bắt chúng |
| @subpage learn_c_project | Nhiều file, header, preprocessor, bốn giai đoạn từ code tới file chạy, lỗi `undefined reference` |

**C++: phần njin dùng hằng ngày**

| Bài | Nội dung |
|---|---|
| @subpage learn_cpp_from_c | Tham chiếu, `const`, namespace, `std::string`, `std::vector`, `struct` có hàm, nạp chồng, tham số mặc định |
| @subpage learn_cpp_types | Giá trị và tham chiếu, sao chép và di chuyển, RAII, quyền sở hữu, và vì sao njin dùng handle và `context &` |
| @subpage learn_cpp_modern | `auto`, structured binding, lambda, khởi tạo chỉ định, `initializer_list`, `constexpr`, `enum class`, template để **dùng** |
| @subpage learn_errors | Đọc lỗi biên dịch, lỗi liên kết và lỗi lúc chạy, và quy trình gỡ lỗi với `-Wall`, `printf`, `gdb` |

**CMake: dựng và quản lý dự án**

| Bài | Nội dung |
|---|---|
| @subpage learn_cmake_basics | `CMakeLists.txt` cho chương trình và thư viện, `PRIVATE`/`PUBLIC`/`INTERFACE`, kiểu build, bộ nhớ đệm (cache) |
| @subpage learn_cmake_projects | `FetchContent`, preset, nhiều thư mục, sao chép assets, và một cửa sổ raylib dựng bằng CMake |

**Shader: chương trình chạy trên GPU**

| Bài | Nội dung |
|---|---|
| @subpage learn_shader_start | Shader là gì, GLSL đủ dùng, một "sân chơi" raylib để viết và sửa fragment shader |
| @subpage learn_shader_sdf | SDF: vẽ hình bằng khoảng cách. Hình tròn, hộp, ghép hình, quầng sáng, bóng đổ, thanh máu không cần ảnh |
| @subpage learn_shader_patterns | Viền tối, sọc CRT, làm mờ, nháy trắng, đổi bảng màu, tan biến, viền, pixel art sắc nét, sóng gợn |

**Pattern của game**

| Bài | Nội dung |
|---|---|
| @subpage learn_game_patterns | Bước vật lý cố định, ECS, máy trạng thái, hẹn giờ, event, action thay cho phím |

## Bỏ qua được gì

Không phải mọi người cần đọc hết. Nếu bạn đã biết:

- viết chương trình C nhiều file và đọc được `undefined reference`: bắt đầu từ bài @ref learn_cpp_from_c;
- C++ cơ bản (lớp, `std::vector`, con trỏ thông minh): bắt đầu từ @ref learn_cpp_modern, và đọc @ref learn_cpp_types để
  hiểu handle của njin;
- tạo được `CMakeLists.txt` nhiều target: đọc @ref learn_cmake_projects cho `FetchContent` và preset;
- viết được fragment shader: đọc @ref learn_shader_sdf nếu chưa biết SDF, rồi @ref learn_shader_patterns và phần "Trong njin" của nó;
- ECS và vòng lặp game: @ref learn_game_patterns ngắn, và có ví dụ chạy được.

## Đã thử những gì

Trang nào cũng nói rõ chuyện này, nhưng tóm lại: mọi ví dụ được chạy trên **Windows với GCC 15.2**. Các trình dò lỗi bộ
nhớ (AddressSanitizer) chạy trong Ubuntu (WSL) vì GCC của Windows ở đây không có. Shader chạy thật trên một card
Intel Iris Xe; thông báo lỗi biên dịch shader do driver viết nên sẽ khác chữ trên card khác. MSVC, Clang và macOS chưa
thử.

## Sau các bài này

Bạn đủ nền để bắt đầu njin:

1. @ref setup : cài môi trường
2. @ref getting_started : chương trình njin đầu tiên
3. @ref first_jump (platformer) hoặc @ref first_walk (top-down) : một nhân vật chạy được trong 50 dòng
4. @ref cheatsheet : tra "muốn làm X thì dùng hàm nào"
