#pragma once

/// @addtogroup grp_core
/// @{

/// Số phiên bản của njin, theo semver: `MAJOR.MINOR.PATCH`.
///
/// Đây là **nguồn duy nhất** của số phiên bản: CMake đọc chính các dòng
/// `#define` này (xem `CMakeLists.txt`), log lúc khởi động và njin_inspector đều
/// hiện số này. Khi phát hành, sửa ba số ở đây, ghi vào `CHANGELOG.md`, rồi đặt
/// tag `vMAJOR.MINOR.PATCH`.
///
/// - **MAJOR**: biến đổi lớn. Khi số này tăng, phần lõi của engine thay đổi và
///   có thể không tương thích với các bản cũ: game viết cho API cũ có thể phải
///   sửa.
/// - **MINOR**: tính năng mới. Engine được bổ sung tính năng nhưng vẫn tương
///   thích tốt với các bản cũ của cùng nhánh MAJOR: game hiện có vẫn build và
///   chạy như trước.
/// - **PATCH**: sửa lỗi. Chỉ vá lỗi và sửa bảo mật, không có tính năng mới.
///
/// Tăng một số thì các số bên phải về 0 (`0.2.3` thành `0.3.0`). Khi MAJOR còn
/// là 0, API chưa ổn định nên bản MINOR vẫn có thể đổi API; `CHANGELOG.md` sẽ
/// nói rõ khi có chuyện đó.
#define NJIN_VERSION_MAJOR 0 ///< Số MAJOR: tăng khi có biến đổi lớn, có thể không tương thích bản cũ.
#define NJIN_VERSION_MINOR 5 ///< Số MINOR: tăng khi thêm tính năng mới, vẫn tương thích trong nhánh.
#define NJIN_VERSION_PATCH 0 ///< Số PATCH: tăng khi chỉ sửa lỗi và bảo mật.

/// Số phiên bản dạng số để so sánh: `MAJOR * 10000 + MINOR * 100 + PATCH`.
/// Ví dụ 0.1.0 là 100. Dùng `#if NJIN_VERSION >= 200` để hỗ trợ nhiều bản engine.
#define NJIN_VERSION (NJIN_VERSION_MAJOR * 10000 + NJIN_VERSION_MINOR * 100 + NJIN_VERSION_PATCH)

/// @}

namespace njin {
/// @addtogroup grp_core
/// @{

/// Phiên bản của engine đang chạy, dạng chuỗi `"0.1.0"`.
///
/// Khác với các macro ở trên, hàm này cho biết bản của **thư viện đã liên kết**,
/// không phải bản của header lúc biên dịch game: hai số khác nhau nghĩa là game
/// và engine được build từ hai bản khác nhau.
/// @return Chuỗi tĩnh.
const char *version();
/// @}
} // namespace njin
