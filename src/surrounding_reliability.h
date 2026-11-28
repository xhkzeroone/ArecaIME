#pragma once

namespace areca {

// Ô có dùng được surrounding không, đánh giá ở lượt sửa đầu tiên của ô.
struct SurroundingReliabilityState {
  bool known = false;
  bool reliable = false;
  bool forceForwardBackspace = false;
  // Đã thấy ứng dụng làm theo một lệnh xoá surrounding trong ô này.
  bool deleteConfirmed = false;

  void reset() {
    known = false;
    reliable = false;
    forceForwardBackspace = false;
    deleteConfirmed = false;
  }
};

} // namespace areca
