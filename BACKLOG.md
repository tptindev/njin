# Backlog

Những việc đã nghĩ tới nhưng chưa làm. Mỗi mục ghi **hiện trạng đã kiểm chứng**, để người làm sau biết bắt đầu từ đâu
và không phải khám phá lại. Việc nào làm xong thì chuyển sang `CHANGELOG.md` khi phát hành, rồi xóa khỏi đây.

Phạm vi engine vẫn là game 2D top-down và platformer (xem README).

## Xuất bản game cho nhiều nền tảng

Ngày ghi: 2026-09-27. Gốc của mục này: câu hỏi "game chưa có export cho tablet, phone, PC?".

### Hiện trạng

| Nền tảng | Hiện trạng |
|---|---|
| PC Windows | Có. `njin_package()` (`cmake/njin.cmake`) tạo `<game>-<version>.zip` gồm exe, thư mục `assets` và file kèm theo; gắn icon và thông tin phiên bản, tắt cửa sổ console ngoài Debug. |
| PC Linux | Build được (thử trên Ubuntu 26.04 trong WSL2). `njin_package()` không có gì riêng: chưa có AppImage, `.deb`, `.desktop`. |
| PC macOS | Chưa thử build, chưa có `.app`, ký mã hay notarize. |
| Điện thoại và máy tính bảng (Android, iOS) | Chưa có. Không có mã xử lý cảm ứng, vòng đời ứng dụng hay build cho hai nền tảng này. |
| Web (WebAssembly) | Chưa có. |

Đã tìm trong mã nguồn: không có `touch`, `android`, `emscripten`, `PLATFORM_ANDROID` nào của njin. CMake của raylib liệt kê
các giá trị `PLATFORM` gồm `Web` và `Android`, nhưng njin chưa nối vào và **chưa thử** xem raylib 6.0 cùng EnTT và CMake của
njin có build được cho chúng hay không.

### Việc cần làm, từ rẻ đến đắt

1. **Đóng gói PC cho đủ ba hệ điều hành.** Mở rộng `njin_package()`: Linux (AppImage hoặc `.tar.gz` kèm script chạy, và
   file `.desktop`), macOS (`.app`, icon, thông tin phiên bản). Phần Linux kiểm tra được ngay trong WSL. Kèm trang wiki
   "phát hành game" cho từng nền tảng.
2. **Cảm ứng và nút ảo.** Thêm `njin::touch_*` (điểm chạm, bấm, kéo) và một lớp nút ảo (cần điều khiển, nút bấm) nối vào
   hệ action/axis có sẵn, để `platformer_input_map` và `topdown_input_map` chạy nguyên xi. Dùng chung cho web lẫn mobile.
   Cần thiết kế cho cả hai thể loại (top-down: cần analog; platformer: chạy trái phải và nhảy).
3. **Web bằng Emscripten.** Cách nhanh nhất để game chạy trên điện thoại và máy tính bảng mà không cần cửa hàng ứng dụng.
   Cần: build với `PLATFORM=Web`, `assets` nạp qua hệ file ảo, vòng lặp chính không chặn (`emscripten_set_main_loop`), lưu game
   vào bộ nhớ trình duyệt, và bước 2.
4. **Android**, rồi iOS. Cần Android SDK/NDK (Android) hoặc Xcode và tài khoản Apple (iOS) trên máy dựng; đóng gói `.apk` hoặc
   `.aab` và ký. Việc đầu tiên nên làm là một **bản thử nhỏ**: một cửa sổ raylib chạy được trên Android bằng CMake, để biết
   mức khó thực tế trước khi cam kết. Cần thêm: vòng đời ứng dụng (xoay màn hình, tạm dừng khi có cuộc gọi), kích thước và
   vùng an toàn (tai thỏ), `save_path()` và cách tìm `assets` không còn "cạnh file exe".

### Rủi ro và điều chưa biết

- Chưa biết raylib 6.0 + EnTT v4.0.0 build cho Android, iOS, Web ổn đến đâu với CMake và preset hiện tại.
- `save_path()` và cách nạp `assets/...` đang theo kiểu desktop.
- Shader dùng GLSL 330 (OpenGL 3.3). Android và Web dùng OpenGL ES 2/3 hoặc WebGL 2 (GLSL ES): các shader dựng sẵn của engine
  (hậu kỳ, nháy sprite, tan biến, hạt GPU) sẽ cần bản GLSL ES, hoặc phải kiểm tra lại. Chưa thử.
- Kiểm tra trên thiết bị thật cần thiết bị hoặc trình giả lập trên máy bạn: mình không tự có.
