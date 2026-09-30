#include "gen.h"

// Names: of the kinds, for the HUD, and the lists streets, districts and
// shops are named from.

namespace sandtable::city {

// --- Names -------------------------------------------------------------------

const char *district_name(district_kind k) {
  static const char *names[] = {"Phố cổ", "Chợ", "Xóm lao động", "Phố đêm", "Bến cảng", "Xưởng", "Khu mới"};
  return names[static_cast<i32>(k)];
}

const char *building_name(building_kind k) {
  static const char *names[] = {"Nhà ống", "Nhà", "Chung cư", "Kho", "Xưởng",
                                "Chợ", "Chùa", "Trường học", "Khách sạn"};
  return names[static_cast<i32>(k)];
}

const char *business_name(business_kind k) {
  static const char *names[] = {"Cà phê",   "Quán ăn",  "Nhà hàng", "Tạp hóa",  "Nhà thuốc", "Tiệm vàng", "Cầm đồ",
                                "Karaoke",  "Bar",      "Bi-a",     "Massage",  "Nhà nghỉ",  "Khách sạn", "Sửa xe",
                                "Cây xăng", "Chợ",      "Kho hàng", "Xưởng",    "Sòng bạc"};
  return names[static_cast<i32>(k)];
}

const char *spot_name(spot_kind k) {
  static const char *names[] = {"Bãi đất trống", "Bãi xe", "Sân bóng", "Sân chợ", "Bãi container",
                                "Công viên",     "Bến",    "Cầu",      "Cuối hẻm", "Bùng binh"};
  return names[static_cast<i32>(k)];
}

// --- Name lists -------------------------------------------------------------------

const char *const street_names[] = {
    "Lê Lợi",          "Nguyễn Huệ",     "Trần Hưng Đạo",  "Hai Bà Trưng",   "Lý Thường Kiệt", "Nguyễn Trãi",
    "Phan Đình Phùng", "Điện Biên Phủ",  "Võ Văn Tần",     "Nam Kỳ Khởi Nghĩa", "Lê Duẩn",     "Pasteur",
    "Hàm Nghi",        "Tôn Đức Thắng",  "Nguyễn Du",      "Bà Triệu",       "Quang Trung",    "Lê Thánh Tôn",
    "Hàng Bạc",        "Hàng Đào",       "Hàng Mã",        "Hàng Buồm",      "Tạ Hiện",        "Mã Mây",
    "Nguyễn Thái Học", "Cống Quỳnh",     "Bùi Viện",       "Đề Thám",        "Phạm Ngũ Lão",   "Trần Quang Khải",
    "Nguyễn Văn Cừ",   "Lạc Long Quân",  "Âu Cơ",          "Phan Xích Long", "Hoàng Diệu",     "Tôn Thất Thuyết",
    "Lê Văn Sỹ",       "Trường Chinh",   "Cộng Hòa",       "Hoàng Văn Thụ",  "Phan Chu Trinh", "Ngô Quyền",
    "Yersin",          "Calmette",       "Ký Con",         "Nguyễn Cư Trinh", "Cô Giang",      "Cô Bắc",
    "Trần Đình Xu",    "Hồ Tùng Mậu",    "Chu Văn An",     "Lý Tự Trọng",    "Mạc Đĩnh Chi",   "Đinh Tiên Hoàng",
    "Lê Hồng Phong",   "Sư Vạn Hạnh",    "Ba Tháng Hai",   "Nguyễn Trung Trực", "Thủ Khoa Huân", "Lê Thị Riêng",
    "Nguyễn Công Trứ", "Huỳnh Thúc Kháng", "Tản Đà",       "Châu Văn Liêm",  "Hải Thượng Lãn Ông", "Phùng Hưng",
    "Bến Bình Đông",   "Xóm Củi",        "Nguyễn Tri Phương", "Lý Thái Tổ",  "Vĩnh Viễn",      "Sư Thiện Chiếu",
};

const char *const district_names[][6] = {
    {"Phố Cổ", "Hàng Ngang", "Phố Hàng", "Chợ Cũ", "Hàng Gai", "Phố Khách"},
    {"Bến Thành", "Chợ Lớn", "Chợ Đũi", "Bàn Cờ", "Chợ Vườn Chuối", "Chợ Thiếc"},
    {"Xóm Chiếu", "Xóm Củi", "Xóm Mới", "Cầu Muối", "Xóm Đình", "Vườn Chuối"},
    {"Phố Tây", "Phố Đêm", "Đèn Đỏ", "Xóm Đèn", "Phố Nhậu", "Bãi Sậy"},
    {"Bến Nghé", "Khánh Hội", "Bến Vân Đồn", "Bến Ghe", "Bến Củi", "Cảng Cũ"},
    {"Xóm Lò", "Xưởng Cũ", "Bình Điền", "Lò Gốm", "Xóm Rèn", "Lò Heo"},
    {"Khu Mới", "Phú Mỹ", "Tân Cảng", "Sala", "Nam Viên", "Thảo Điền"},
};

const char *const owner_names[] = {
    "Hương",    "Lan",      "Tuấn",      "Hùng",      "Phát Đạt", "Hoàng Gia", "Thanh Xuân", "Bảo Ngọc",
    "Mỹ Linh",  "Sài Gòn",  "Hà Nội",    "Kim Long",  "Bình Minh", "Hải Âu",   "Thành Công", "Đông Á",
    "Ngọc Lan", "Như Ý",    "Phúc Lộc",  "Minh Châu", "Út Hiền",  "Cô Ba",     "Chú Tư",     "Bà Năm",
    "Anh Ba",   "Tân Tân",  "Hồng Phúc", "Vạn Lộc",   "Kim Thành", "Trung Nghĩa", "Thu Hà",  "Ông Sáu",
    "Dì Bảy",   "Hai Lúa",  "Mười Tám",  "Tư Mập",    "Năm Cam",  "Bảy Viễn",  "Kim Hoa",    "Phước Lộc",
};

const char *const food_names[] = {"Cơm tấm", "Phở", "Bún bò", "Hủ tiếu", "Bánh mì", "Quán nhậu", "Lẩu dê", "Ốc"};

const i32 street_name_count = static_cast<i32>(sizeof(street_names) / sizeof(street_names[0]));
const i32 owner_name_count = static_cast<i32>(sizeof(owner_names) / sizeof(owner_names[0]));
const i32 food_name_count = static_cast<i32>(sizeof(food_names) / sizeof(food_names[0]));

} // namespace sandtable::city
