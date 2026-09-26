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
/// Trước 1.0, API công khai còn có thể đổi giữa hai bản MINOR; PATCH chỉ sửa lỗi.
#define NJIN_VERSION_MAJOR 0 ///< Số MAJOR: tăng khi API đổi không tương thích (từ 1.0 trở đi).
#define NJIN_VERSION_MINOR 2 ///< Số MINOR: tăng khi thêm tính năng.
#define NJIN_VERSION_PATCH 0 ///< Số PATCH: tăng khi chỉ sửa lỗi.

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
